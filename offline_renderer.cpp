#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "offline_renderer.h"
#include "event_timing.h"
#include <wx/ffile.h>
#include <wx/filename.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace
{
    const long kPPQ = 480;
    const unsigned int kOutputRate = 44100;

    struct AudioData
    {
        unsigned int rate, channels;
        unsigned long long frames;
        std::vector<float> data;
        AudioData() : rate(0), channels(0), frames(0) {}
    };

    float Decode(const unsigned char* p, unsigned short format, unsigned short bits)
    {
        if (format == 1)
        {
            if (bits == 8) return (static_cast<int>(p[0]) - 128) / 128.0f;
            if (bits == 16) { short v = static_cast<short>(p[0] | (p[1] << 8)); return v / 32768.0f; }
            if (bits == 24) { int v = p[0] | (p[1] << 8) | (p[2] << 16); if (v & 0x800000) v |= 0xff000000; return v / 8388608.0f; }
            if (bits == 32) { int v = p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24); return v / 2147483648.0f; }
        }
        if (format == 3)
        {
            if (bits == 32) { float v; std::memcpy(&v,p,4); return v; }
            if (bits == 64) { double v; std::memcpy(&v,p,8); return static_cast<float>(v); }
        }
        return 0.0f;
    }

    bool Load(const SamplePoolItem& item, AudioData* out, wxString* error)
    {
        wxFFile f(item.filePath, wxT("rb"));
        if (!f.IsOpened() || !f.Seek(static_cast<wxFileOffset>(item.wav.dataOffset), wxFromStart))
        { if(error)*error=wxT("Unable to open source WAV: ")+item.filePath; return false; }
        const unsigned int bps=item.wav.bitsPerSample/8, bpf=bps*item.wav.channelCount;
        if (!bps || !bpf) { if(error)*error=wxT("Invalid WAV format."); return false; }
        std::vector<unsigned char> raw(static_cast<size_t>(item.wav.dataBytes));
        if (!raw.empty() && f.Read(&raw[0],raw.size()) != raw.size())
        { if(error)*error=wxT("Truncated WAV: ")+item.filePath; return false; }
        out->rate=item.wav.sampleRate; out->channels=item.wav.channelCount; out->frames=item.wav.frameCount;
        out->data.resize(static_cast<size_t>(out->frames*out->channels));
        for (unsigned long long fr=0;fr<out->frames;++fr)
            for (unsigned int ch=0;ch<out->channels;++ch)
                out->data[static_cast<size_t>(fr*out->channels+ch)] = Decode(&raw[static_cast<size_t>(fr*bpf+ch*bps)],item.wav.audioFormat,item.wav.bitsPerSample);
        return true;
    }

    const AudioSlice* FindSlice(const SliceModel& model, const wxString& id)
    { for(size_t i=0;i<model.GetCount();++i){const AudioSlice* s=model.GetAt(i);if(s&&s->id==id)return s;} return NULL; }
    const SamplePoolItem* FindSample(const SamplePool& pool, const wxString& id)
    { for(size_t i=0;i<pool.GetCount();++i){const SamplePoolItem* s=pool.GetAt(i);if(s&&s->id==id)return s;} return NULL; }

    float Read(const AudioData& a, double frame, unsigned int outCh)
    {
        if (a.frames==0 || a.data.empty()) return 0.0f;
        if (frame<0) frame=0; if (frame>a.frames-1) frame=static_cast<double>(a.frames-1);
        const unsigned long long i0=static_cast<unsigned long long>(frame), i1=std::min<unsigned long long>(i0+1,a.frames-1);
        const double t=frame-i0; const unsigned int ch=a.channels==1?0:std::min<unsigned int>(outCh,a.channels-1);
        const float x=a.data[static_cast<size_t>(i0*a.channels+ch)], y=a.data[static_cast<size_t>(i1*a.channels+ch)];
        return static_cast<float>(x+(y-x)*t);
    }

    void Put32(wxFFile& f, unsigned int v){ unsigned char b[4]={(unsigned char)v,(unsigned char)(v>>8),(unsigned char)(v>>16),(unsigned char)(v>>24)};f.Write(b,4); }
    void Put16(wxFFile& f, unsigned short v){ unsigned char b[2]={(unsigned char)v,(unsigned char)(v>>8)};f.Write(b,2); }

    float PeakOf(const std::vector<float>& values)
    {
        float peak = 0.0f;
        for (size_t i = 0; i < values.size(); ++i) peak = std::max(peak, static_cast<float>(std::fabs(values[i])));
        return peak;
    }
}

bool OfflineSlicerRenderer::Render(const wxString& path,const SamplePool& samples,const SliceModel& slices,
                                   const std::vector<SliceRollEvent>& events,double bpm,wxString* error)
{
    if (events.empty()) { if(error)*error=wxT("The Slicer Piano Roll contains no events."); return false; }
    if (bpm<=0.0) { if(error)*error=wxT("Invalid BPM."); return false; }
    long endTick=0; for(size_t i=0;i<events.size();++i) endTick=std::max(endTick,events[i].startTick+events[i].durationTicks);
    const double totalSec=static_cast<double>(endTick)/kPPQ*60.0/bpm;
    const size_t outFrames=static_cast<size_t>(std::ceil(totalSec*kOutputRate))+1;
    std::vector<float> mix(outFrames*2,0.0f);
    std::map<wxString,AudioData> cache;
    const wxString diagnosticPath = path + wxT(".diagnostic.txt");
    wxFFile diagnostic(diagnosticPath, wxT("wb"));
    if (diagnostic.IsOpened())
    {
        diagnostic.Write(wxString::Format(wxT("M11 offline renderer diagnostic\r\nOutput: %s\r\nBPM: %.6f\r\nEvents: %lu\r\nEnd tick: %ld\r\nOutput frames: %lu\r\n\r\n"),
            path.c_str(), bpm, static_cast<unsigned long>(events.size()), endTick,
            static_cast<unsigned long>(outFrames)));
    }

    for(size_t ei=0;ei<events.size();++ei)
    {
        const SliceRollEvent& e=events[ei]; const AudioSlice* sl=FindSlice(slices,e.sliceId);
        if(!sl)
        {
            if (diagnostic.IsOpened()) diagnostic.Write(wxString::Format(wxT("EVENT %lu id=%s SKIPPED missing slice %s\r\n"), static_cast<unsigned long>(ei), e.id.c_str(), e.sliceId.c_str()));
            continue;
        }
        const SamplePoolItem* src=FindSample(samples,sl->sourceId);
        if(!src || !wxFileExists(src->filePath)) { if(error)*error=wxT("Missing source WAV for event: ")+e.id; return false; }
        if(cache.find(src->id)==cache.end()){AudioData a;if(!Load(*src,&a,error))return false;cache[src->id]=a;}
        const AudioData& a=cache[src->id];
        const SliceEventTiming timing=SliceEventTimingCalculator::Calculate(*sl,a.rate,e.midiNote,e.durationTicks,kPPQ,bpm);
        if(!timing.valid)
        {
            if (diagnostic.IsOpened()) diagnostic.Write(wxString::Format(wxT("EVENT %lu id=%s SKIPPED invalid timing\r\n"), static_cast<unsigned long>(ei), e.id.c_str()));
            continue;
        }
        const size_t begin=static_cast<size_t>(std::floor((static_cast<double>(e.startTick)/kPPQ*60.0/bpm)*kOutputRate+0.5));
        const size_t count=static_cast<size_t>(std::floor(timing.eventSeconds*kOutputRate+0.5));
        const double rate=timing.playbackRate;
        const float normalizedVelocity = std::max(0.0f, std::min(1.0f, static_cast<float>(e.velocity) / 127.0f));
        // Velocity is an event-local gain.  Do not normalize the final file to
        // its peak, otherwise every render is brought back to full scale and
        // velocity differences disappear.
        const float gain = normalizedVelocity;
        if (diagnostic.IsOpened())
        {
            diagnostic.Write(wxString::Format(wxT("EVENT %lu id=%s slice=%s startTick=%ld durationTicks=%ld midi=%d velocity=%d normalizedGain=%.8f beginFrame=%lu frameCount=%lu playbackRate=%.8f attack=%.8f loop=%.8f residual=%.8f tailStart=%.8f eventSeconds=%.8f\r\n"),
                static_cast<unsigned long>(ei), e.id.c_str(), e.sliceId.c_str(), e.startTick, e.durationTicks, e.midiNote, e.velocity, gain,
                static_cast<unsigned long>(begin), static_cast<unsigned long>(count), rate, timing.attackSeconds, timing.loopSeconds,
                timing.residualSeconds, timing.tailStartSeconds, timing.eventSeconds));
        }
        std::vector<float> eventBuffer(count * 2, 0.0f);
        for(size_t n=0;n<count && begin+n<outFrames;++n)
        {
            const double t=static_cast<double>(n)/kOutputRate; double pos=sl->startFrame;
            if(!sl->loopEnabled || timing.earlyStop)
                pos=sl->startFrame+t*a.rate*rate;
            else if(t<timing.attackSeconds)
                pos=sl->startFrame+t*a.rate*rate;
            else if(t>=timing.tailStartSeconds)
                pos=sl->loopOutFrame+(t-timing.tailStartSeconds)*a.rate*rate;
            else
            {
                const double loopFrames=static_cast<double>(sl->loopOutFrame-sl->loopInFrame);
                const double progressed=(t-timing.attackSeconds)*a.rate*rate;
                pos=sl->loopInFrame+std::fmod(std::max(0.0,progressed),loopFrames);
            }
            if(pos>=sl->endFrame || pos>=a.frames) continue;
            for(unsigned int ch=0;ch<2;++ch)
                eventBuffer[n*2+ch] = Read(a,pos,ch) * gain;
        }
        if (diagnostic.IsOpened())
            diagnostic.Write(wxString::Format(wxT("EVENT %lu renderedPeak=%.8f\r\n"), static_cast<unsigned long>(ei), PeakOf(eventBuffer)));
        // Mix each event only after it has been rendered into its own buffer.
        // This keeps overlapping events independent and additive.
        for (size_t n=0; n<count && begin+n<outFrames; ++n)
            for (unsigned int ch=0; ch<2; ++ch)
                mix[(begin+n)*2+ch] += eventBuffer[n*2+ch];
    }

    if (diagnostic.IsOpened())
    {
        diagnostic.Write(wxString::Format(wxT("\r\nMIX peakBeforeMaster=%.8f\r\n"), PeakOf(mix)));
        diagnostic.Flush();
    }

    wxFFile f(path,wxT("wb")); if(!f.IsOpened()){if(error)*error=wxT("Unable to create output WAV.");return false;}
    const unsigned int dataBytes=static_cast<unsigned int>(outFrames*2*2);
    f.Write("RIFF",4);Put32(f,36+dataBytes);f.Write("WAVEfmt ",8);Put32(f,16);Put16(f,1);Put16(f,2);Put32(f,kOutputRate);Put32(f,kOutputRate*4);Put16(f,4);Put16(f,16);f.Write("data",4);Put32(f,dataBytes);
    // Use fixed headroom, not peak normalization.  Fixed gain preserves both
    // MIDI velocity and the additive contribution of overlapping events.
    // Saturate only samples that genuinely exceed the output range.
    const float master = 0.80f;
    for(size_t i=0;i<mix.size();++i)
    {
        float v = mix[i] * master;
        if (v > 0.999f) v = 0.999f;
        if (v < -0.999f) v = -0.999f;
        const short q=static_cast<short>(v*32767.0f);
        Put16(f,static_cast<unsigned short>(q));
    }
    f.Close();
    if (diagnostic.IsOpened())
    {
        diagnostic.Write(wxString::Format(wxT("Master gain: %.8f\r\nRender complete: yes\r\n"), master));
        diagnostic.Close();
    }
    return true;
}
