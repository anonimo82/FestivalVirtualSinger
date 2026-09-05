#include "sample_engine.h"

#include <alsa/asoundlib.h>
#include <wx/ffile.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
float DecodeSample(const unsigned char* data, unsigned short format, unsigned short bits)
{
    if (format == 1) {
        if (bits == 8) return (static_cast<int>(data[0]) - 128) / 128.0f;
        if (bits == 16) { short v=static_cast<short>(data[0]|(data[1]<<8)); return v/32768.0f; }
        if (bits == 24) { int v=data[0]|(data[1]<<8)|(data[2]<<16); if(v&0x00800000)v|=0xff000000; return v/8388608.0f; }
        if (bits == 32) { int v=static_cast<int>(data[0])|(static_cast<int>(data[1])<<8)|(static_cast<int>(data[2])<<16)|(static_cast<int>(data[3])<<24); return v/2147483648.0f; }
    } else if (format == 3) {
        if (bits == 32) { float v=0; std::memcpy(&v,data,4); return v; }
        if (bits == 64) { double v=0; std::memcpy(&v,data,8); return static_cast<float>(v); }
    }
    return 0.0f;
}
short ToPcm16(float value) { value=static_cast<float>(std::tanh(value)); return static_cast<short>(value*32767.0f); }
}

SampleEngine::SampleEngine()
 : m_stopRequested(false),m_running(false),m_pcm(NULL),m_outputSampleRate(0),m_nextVoiceId(1),m_lastVoiceId(0) {}
SampleEngine::~SampleEngine(){ Shutdown(); }

bool SampleEngine::OpenDevice(unsigned int rate, wxString* error)
{
    snd_pcm_t* pcm=NULL;
    int rc=snd_pcm_open(&pcm,"default",SND_PCM_STREAM_PLAYBACK,0);
    if(rc<0){ if(error)*error=wxString::Format("Unable to open ALSA device: %s",snd_strerror(rc)); return false; }
    rc=snd_pcm_set_params(pcm,SND_PCM_FORMAT_S16_LE,SND_PCM_ACCESS_RW_INTERLEAVED,
                          OutputChannels,rate,1,50000);
    if(rc<0){ if(error)*error=wxString::Format("Unable to configure ALSA device: %s",snd_strerror(rc)); snd_pcm_close(pcm); return false; }
    m_pcm=pcm; m_outputSampleRate=rate; return true;
}
void SampleEngine::CloseDevice()
{
    snd_pcm_t* pcm=static_cast<snd_pcm_t*>(m_pcm);
    if(pcm){ snd_pcm_drop(pcm); snd_pcm_close(pcm); }
    m_pcm=NULL; m_outputSampleRate=0; m_running=false;
}
bool SampleEngine::Start(unsigned int rate, wxString* error)
{
    if(!rate){if(error)*error="Invalid output sample rate.";return false;}
    if(m_running && m_outputSampleRate==rate)return true;
    if(m_running)Shutdown();
    if(!OpenDevice(rate,error))return false;
    m_stopRequested=false; m_running=true; m_thread=std::thread(&SampleEngine::Run,this); return true;
}
void SampleEngine::Shutdown()
{
    m_stopRequested=true;
    if(m_thread.joinable())m_thread.join();
    CloseDevice();
    std::lock_guard<std::mutex> lock(m_lock); m_voices.clear(); m_lastVoiceId=0;
}
void SampleEngine::Run()
{
    std::vector<short> buffer(BufferFrames*OutputChannels);
    snd_pcm_t* pcm=static_cast<snd_pcm_t*>(m_pcm);
    while(!m_stopRequested){
        FillBuffer(buffer.data(),BufferFrames);
        snd_pcm_sframes_t n=snd_pcm_writei(pcm,buffer.data(),BufferFrames);
        if(n<0){ n=snd_pcm_recover(pcm,static_cast<int>(n),1); if(n<0)break; }
    }
}

bool SampleEngine::DecodeWav(const SamplePoolItem& source,DecodedAudio* decoded,wxString* error) const
{
    if(!decoded)return false; wxFFile f(source.filePath,"rb");
    if(!f.IsOpened()||!f.Seek(static_cast<wxFileOffset>(source.wav.dataOffset),wxFromStart)){if(error)*error="Unable to open the slice source WAV.";return false;}
    const unsigned int bps=source.wav.bitsPerSample/8,bpf=bps*source.wav.channelCount;
    if(!bps||!bpf){if(error)*error="Invalid WAV frame size.";return false;}
    std::vector<unsigned char> raw(static_cast<size_t>(source.wav.dataBytes));
    if(!raw.empty()&&f.Read(raw.data(),raw.size())!=raw.size()){if(error)*error="The slice source WAV is truncated.";return false;}
    decoded->sampleRate=source.wav.sampleRate;decoded->channels=source.wav.channelCount;decoded->frameCount=source.wav.frameCount;
    decoded->samples.resize(static_cast<size_t>(decoded->frameCount*decoded->channels));
    for(unsigned long long fr=0;fr<decoded->frameCount;++fr)for(unsigned int ch=0;ch<decoded->channels;++ch)
        decoded->samples[static_cast<size_t>(fr*decoded->channels+ch)]=DecodeSample(&raw[static_cast<size_t>(fr*bpf+ch*bps)],source.wav.audioFormat,source.wav.bitsPerSample);
    return true;
}
bool SampleEngine::NoteOn(const SamplePoolItem&s,const AudioSlice&sl,int note,PlayMode mode,wxString*e){return NoteOn(s,sl,note,127,mode,NULL,e);}
bool SampleEngine::NoteOn(const SamplePoolItem&s,const AudioSlice&sl,int note,int velocity,PlayMode mode,unsigned long*id,wxString*e)
{
    DecodedAudio d;if(!DecodeWav(s,&d,e))return false;if(!Start(44100,e))return false;std::lock_guard<std::mutex> lock(m_lock);
    if(mode==ModeLegato)for(size_t i=m_voices.size();i>0;--i){Voice&v=m_voices[i-1];if(v.active&&v.slice.id==sl.id){v.midiNote=note;v.step=(double(v.audio.sampleRate)/m_outputSampleRate)*std::pow(2.0,(note-v.slice.rootMidiNote)/12.0);v.gain=std::max(0.f,std::min(1.f,velocity/127.f));v.noteHeld=true;v.releasePending=false;v.immediateFade=false;v.fadeRemaining=0;m_lastVoiceId=v.id;if(id)*id=v.id;return true;}}
    RemoveInactiveVoices();if(m_voices.size()>=MaxVoices)m_voices.erase(m_voices.begin());Voice v;v.id=m_nextVoiceId++;if(!m_nextVoiceId)m_nextVoiceId=1;BeginVoice(v,d,sl,note,velocity);m_lastVoiceId=v.id;m_voices.push_back(v);if(id)*id=v.id;return true;
}
void SampleEngine::BeginVoice(Voice&v,const DecodedAudio&d,const AudioSlice&s,int note,int velocity){v.audio=d;v.slice=s;v.midiNote=note;v.position=double(s.startFrame);v.step=(double(d.sampleRate)/m_outputSampleRate)*std::pow(2.0,(note-s.rootMidiNote)/12.0);v.gain=std::max(0.f,std::min(1.f,velocity/127.f));v.active=true;v.noteHeld=true;v.releasePending=false;v.enteredLoop=false;v.immediateFade=false;v.fadeRemaining=0;}
SampleEngine::Voice* SampleEngine::FindVoice(unsigned long id){for(auto&v:m_voices)if(v.id==id)return &v;return NULL;}
const SampleEngine::Voice* SampleEngine::FindVoice(unsigned long id)const{for(const auto&v:m_voices)if(v.id==id)return &v;return NULL;}
void SampleEngine::NoteOff(){NoteOff(m_lastVoiceId);}
void SampleEngine::NoteOff(unsigned long id){std::lock_guard<std::mutex>lock(m_lock);Voice*v=FindVoice(id);if(v&&v->active){v->noteHeld=false;if(!v->enteredLoop){v->immediateFade=true;v->fadeRemaining=128;}else v->releasePending=true;}}
void SampleEngine::EndVoiceImmediately(Voice&v){v.active=false;v.noteHeld=false;v.releasePending=false;v.immediateFade=false;v.fadeRemaining=0;}
void SampleEngine::StopAll(){std::lock_guard<std::mutex>lock(m_lock);for(auto&v:m_voices)EndVoiceImmediately(v);m_voices.clear();m_lastVoiceId=0;}
bool SampleEngine::IsVoiceActive()const{std::lock_guard<std::mutex>lock(m_lock);for(const auto&v:m_voices)if(v.active)return true;return false;}
bool SampleEngine::IsVoiceActive(unsigned long id)const{std::lock_guard<std::mutex>lock(m_lock);const Voice*v=FindVoice(id);return v&&v->active;}
float SampleEngine::ReadInterpolated(const Voice&v,unsigned int ch)const{if(!v.active||v.audio.samples.empty()||!v.audio.frameCount)return 0;unsigned long long a=static_cast<unsigned long long>(v.position);if(a>=v.audio.frameCount)a=v.audio.frameCount-1;unsigned long long b=std::min<unsigned long long>(a+1,v.audio.frameCount-1);double t=v.position-a;unsigned int c=v.audio.channels==1?0:std::min<unsigned int>(ch,v.audio.channels-1);float x=v.audio.samples[static_cast<size_t>(a*v.audio.channels+c)],y=v.audio.samples[static_cast<size_t>(b*v.audio.channels+c)];return static_cast<float>(x+(y-x)*t);}
void SampleEngine::RemoveInactiveVoices(){m_voices.erase(std::remove_if(m_voices.begin(),m_voices.end(),[](const Voice&v){return !v.active;}),m_voices.end());}
void SampleEngine::FillBuffer(short*out,unsigned int frames){std::lock_guard<std::mutex>lock(m_lock);for(unsigned int n=0;n<frames;++n){float mix[2]={0,0};for(auto&v:m_voices){if(!v.active)continue;float fg=1;if(v.immediateFade){fg=v.fadeRemaining/128.f;if(v.fadeRemaining)--v.fadeRemaining;if(!v.fadeRemaining){EndVoiceImmediately(v);continue;}}for(unsigned int c=0;c<2;++c)mix[c]+=ReadInterpolated(v,c)*v.gain*fg*.70f;v.position+=v.step;if(v.slice.loopEnabled&&v.position>=v.slice.loopInFrame)v.enteredLoop=true;if(v.slice.loopEnabled&&v.enteredLoop&&v.position>=v.slice.loopOutFrame){if(v.noteHeld)v.position=v.slice.loopInFrame+(v.position-v.slice.loopOutFrame);else if(v.releasePending)v.releasePending=false;}if(v.position>=v.slice.endFrame||v.position>=v.audio.frameCount)EndVoiceImmediately(v);}for(unsigned int c=0;c<2;++c)out[n*2+c]=ToPcm16(mix[c]);}RemoveInactiveVoices();}
