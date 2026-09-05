#include "tone_preview.h"
#include "song_model.h"
#include <alsa/asoundlib.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
const double PI_VALUE=3.14159265358979323846;
double MidiFrequency(int midi){return 440.0*std::pow(2.0,(midi-69.0)/12.0);}
double Clamp(double v,double a,double b){return std::max(a,std::min(b,v));}
}

TonePreview::TonePreview():m_stopRequested(false),m_pcm(NULL){}
TonePreview::~TonePreview(){Stop();}
bool TonePreview::Play(const wxString& pitch,double beats,double bpm)
{
    double safe=bpm>1?bpm:120, duration=std::max(0.0625,beats)*60.0/safe;
    return OpenAndWrite(MidiFrequency(PitchToMidi(pitch)),Clamp(duration,.08,.65));
}
void TonePreview::Stop(){m_stopRequested=true;if(m_thread.joinable())m_thread.join();m_stopRequested=false;m_pcm=NULL;}
bool TonePreview::OpenAndWrite(double f,double d){Stop();m_stopRequested=false;m_thread=std::thread(&TonePreview::Worker,this,f,d);return true;}
void TonePreview::Worker(double frequency,double duration)
{
    snd_pcm_t* pcm=NULL;if(snd_pcm_open(&pcm,"default",SND_PCM_STREAM_PLAYBACK,0)<0)return;
    {std::lock_guard<std::mutex>lock(m_mutex);m_pcm=pcm;}
    const unsigned rate=44100;
    if(snd_pcm_set_params(pcm,SND_PCM_FORMAT_S16_LE,SND_PCM_ACCESS_RW_INTERLEAVED,2,rate,1,50000)<0){snd_pcm_close(pcm);m_pcm=NULL;return;}
    const size_t count=static_cast<size_t>(std::max(1.0,duration*rate));std::vector<short>s(count*2);
    size_t attack=static_cast<size_t>(std::min(.015,duration*.25)*rate),release=static_cast<size_t>(std::min(.035,duration*.35)*rate);
    for(size_t i=0;i<count;++i){double env=1;if(attack&&i<attack)env=double(i)/attack;if(release&&i+release>=count)env=std::min(env,double(count-i)/release);short sample=static_cast<short>(.22*32767.0*env*std::sin(2*PI_VALUE*frequency*i/rate));s[i*2]=sample;s[i*2+1]=sample;}
    size_t pos=0;while(pos<count&&!m_stopRequested){snd_pcm_sframes_t n=snd_pcm_writei(pcm,s.data()+pos*2,count-pos);if(n<0){n=snd_pcm_recover(pcm,static_cast<int>(n),1);if(n<0)break;}else pos+=static_cast<size_t>(n);}
    if(m_stopRequested)snd_pcm_drop(pcm);else snd_pcm_drain(pcm);snd_pcm_close(pcm);std::lock_guard<std::mutex>lock(m_mutex);m_pcm=NULL;
}
