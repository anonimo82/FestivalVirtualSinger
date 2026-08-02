#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#pragma once

#include "sample_pool.h"
#include "slice_model.h"

#include <windows.h>
#include <mmsystem.h>
#include <vector>

class SampleEngine
{
public:
    enum PlayMode
    {
        ModeRetrigger = 0,
        ModeLegato = 1
    };

    SampleEngine();
    ~SampleEngine();

    bool Start(unsigned int outputSampleRate, wxString* errorMessage);
    void Shutdown();

    // Backward-compatible audition entry point (full velocity).
    bool NoteOn(const SamplePoolItem& source,
                const AudioSlice& slice,
                int midiNote,
                PlayMode mode,
                wxString* errorMessage);

    // Realtime sequencer entry point. Returns an independent voice id.
    bool NoteOn(const SamplePoolItem& source,
                const AudioSlice& slice,
                int midiNote,
                int velocity,
                PlayMode mode,
                unsigned long* voiceId,
                wxString* errorMessage);

    // Backward-compatible Note Off: releases the most recently started voice.
    void NoteOff();
    void NoteOff(unsigned long voiceId);
    void StopAll();

    bool IsRunning() const { return m_waveOut != NULL; }
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

    static DWORD WINAPI ThreadEntry(LPVOID parameter);
    DWORD Run();
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

    mutable CRITICAL_SECTION m_lock;
    HANDLE m_thread;
    HANDLE m_stopEvent;
    HWAVEOUT m_waveOut;

    enum { OutputChannels = 2, BufferFrames = 256, BufferCount = 3, MaxVoices = 32 };
    unsigned int m_outputSampleRate;
    WAVEHDR m_headers[BufferCount];
    std::vector<short> m_buffers[BufferCount];

    std::vector<Voice> m_voices;
    unsigned long m_nextVoiceId;
    unsigned long m_lastVoiceId;
};
