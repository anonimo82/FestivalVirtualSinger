#include "auto_slicer.h"
#include <wx/ffile.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    double Decode(const unsigned char* p, unsigned short format, unsigned short bits)
    {
        if (format == 3 && bits == 32) { float v; memcpy(&v,p,4); return v; }
        if (format == 3 && bits == 64) { double v; memcpy(&v,p,8); return v; }
        if (bits == 8) return (static_cast<int>(p[0]) - 128) / 128.0;
        if (bits == 16) { short v = static_cast<short>(p[0] | (p[1] << 8)); return v / 32768.0; }
        if (bits == 24) { int v = p[0] | (p[1]<<8) | (p[2]<<16); if (v & 0x800000) v |= ~0xffffff; return v / 8388608.0; }
        if (bits == 32) { int v; memcpy(&v,p,4); return v / 2147483648.0; }
        return 0.0;
    }

    bool LoadMono(const SamplePoolItem& source, std::vector<float>* samples, wxString* error)
    {
        if (!samples) return false;
        wxFFile file(source.filePath, wxT("rb"));
        if (!file.IsOpened()) { if(error)*error=wxT("Unable to open WAV."); return false; }
        if (!file.Seek(static_cast<wxFileOffset>(source.wav.dataOffset), wxFromStart)) { if(error)*error=wxT("Unable to seek WAV data."); return false; }
        const unsigned int bps = source.wav.bitsPerSample / 8;
        const unsigned int bpf = bps * source.wav.channelCount;
        if (!bps || !bpf) { if(error)*error=wxT("Invalid WAV frame size."); return false; }
        std::vector<unsigned char> raw(source.wav.dataBytes);
        if (!raw.empty() && file.Read(&raw[0], raw.size()) != raw.size()) { if(error)*error=wxT("Truncated WAV data."); return false; }
        samples->resize(static_cast<size_t>(source.wav.frameCount));
        for (size_t f=0; f<samples->size(); ++f)
        {
            double sum=0.0; const unsigned char* frame=&raw[f*bpf];
            for (unsigned int c=0;c<source.wav.channelCount;++c) sum += Decode(frame+c*bps, source.wav.audioFormat, source.wav.bitsPerSample);
            (*samples)[f]=static_cast<float>(sum/source.wav.channelCount);
        }
        return true;
    }
}

bool AutoSlicer::Uniform(const SamplePoolItem& source, const AutoSliceSettings& settings,
                         std::vector<unsigned long long>* boundaries, wxString* errorMessage)
{
    if (!boundaries || source.wav.frameCount < 2 || settings.uniformDivisions < 1)
    { if(errorMessage)*errorMessage=wxT("Invalid uniform slicing settings."); return false; }
    boundaries->clear(); boundaries->push_back(0);
    for (int i=1;i<settings.uniformDivisions;++i)
        boundaries->push_back(source.wav.frameCount * static_cast<unsigned long long>(i) / settings.uniformDivisions);
    boundaries->push_back(source.wav.frameCount);
    return true;
}

bool AutoSlicer::Transients(const SamplePoolItem& source, const AutoSliceSettings& settings,
                            std::vector<unsigned long long>* boundaries, wxString* errorMessage)
{
    std::vector<float> samples;
    if (!LoadMono(source, &samples, errorMessage)) return false;
    if (samples.size()<128) { if(errorMessage)*errorMessage=wxT("WAV is too short for transient analysis."); return false; }
    const size_t block=64;
    const size_t count=(samples.size()+block-1)/block;
    std::vector<double> env(count,0.0), novelty(count,0.0);
    for(size_t b=0;b<count;++b){ size_t first=b*block,last=std::min(samples.size(),first+block); double peak=0.0; for(size_t i=first;i<last;++i) peak=std::max(peak,std::fabs(static_cast<double>(samples[i]))); env[b]=peak; }
    double maximum=0.0;
    for(size_t b=1;b<count;++b){ novelty[b]=std::max(0.0,env[b]-env[b-1]); maximum=std::max(maximum,novelty[b]); }
    boundaries->clear(); boundaries->push_back(0);
    if(maximum>0.0){
        const double threshold=maximum*std::max(0.0,std::min(1.0,settings.sensitivity));
        const unsigned long long minGap=static_cast<unsigned long long>(source.wav.sampleRate*settings.minimumGapMs/1000.0);
        unsigned long long last=0;
        for(size_t b=2;b+1<count;++b){
            if(novelty[b]>=threshold && novelty[b]>=novelty[b-1] && novelty[b]>novelty[b+1]){
                unsigned long long frame=static_cast<unsigned long long>(b*block);
                if(frame>last+minGap && frame+minGap<source.wav.frameCount){ boundaries->push_back(frame); last=frame; }
            }
        }
    }
    boundaries->push_back(source.wav.frameCount);
    std::sort(boundaries->begin(),boundaries->end());
    boundaries->erase(std::unique(boundaries->begin(),boundaries->end()),boundaries->end());
    return true;
}
