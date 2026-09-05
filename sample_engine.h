#pragma once

#include "sample_pool.h"
#include "slice_model.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

class SampleEngine
{
public:
    enum PlayMode { ModeRetrigger = 0, ModeLegato = 1 };

    SampleEngine();
    ~SampleEngine();

    bool Start(unsigned int outputSampleRate, wxString* errorMessage);
    void Shutdown();

    bool NoteOn(const SamplePoolItem& source,
                const AudioSlice& slice,
                int midiNote,
                PlayMode mode,
                wxString* errorMessage);

    bool NoteOn(const SamplePoolItem& source,
                const AudioSlice& slice,
                int midiNote,
                int velocity,
                PlayMode mode,
                unsigned long* voiceId,
                wxString* errorMessage);

    void NoteOff();
    void NoteOff(unsigned long voiceId);
    void StopAll();

    bool IsRunning() const { return m_running.load(); }
    bool IsVoiceActive() const;
    bool IsVoiceActive(unsigned long voiceId) const;

private:
    struct DecodedAudio
    {
        unsigned int sampleRate;
        unsigned int channels;
        unsigned long long frameCount;
        std::vector<float> samples;
        DecodedAudio() : sampleRate(0), channels(0), frameCount(0) {}
    };

    struct Voice
    {
        unsigned long id;
        DecodedAudio audio;
        AudioSlice slice;
        bool active;
        bool noteHeld;
        bool releasePending;
        bool enteredLoop;
        bool immediateFade;
        unsigned int fadeRemaining;
        double position;
        double step;
        int midiNote;
        float gain;

        Voice()
            : id(0), active(false), noteHeld(false), releasePending(false),
              enteredLoop(false), immediateFade(false), fadeRemaining(0),
              position(0.0), step(1.0), midiNote(60), gain(1.0f) {}
    };

    void Run();
    bool OpenDevice(unsigned int outputSampleRate, wxString* errorMessage);
    void CloseDevice();
    bool DecodeWav(const SamplePoolItem& source,
                   DecodedAudio* decoded,
                   wxString* errorMessage) const;
    void FillBuffer(short* output, unsigned int frameCount);
    float ReadInterpolated(const Voice& voice, unsigned int channel) const;
    void BeginVoice(Voice& voice,
                    const DecodedAudio& decoded,
                    const AudioSlice& slice,
                    int midiNote,
                    int velocity);
    void EndVoiceImmediately(Voice& voice);
    Voice* FindVoice(unsigned long voiceId);
    const Voice* FindVoice(unsigned long voiceId) const;
    void RemoveInactiveVoices();

    mutable std::mutex m_lock;
    std::thread m_thread;
    std::atomic<bool> m_stopRequested;
    std::atomic<bool> m_running;
    void* m_pcm;

    enum { OutputChannels = 2, BufferFrames = 256, MaxVoices = 32 };
    unsigned int m_outputSampleRate;
    std::vector<Voice> m_voices;
    unsigned long m_nextVoiceId;
    unsigned long m_lastVoiceId;
};
