#include "sample_engine.h"

#include <wx/ffile.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    float DecodeSample(const unsigned char* data, unsigned short format,
                       unsigned short bits)
    {
        if (format == 1)
        {
            if (bits == 8)
                return (static_cast<int>(data[0]) - 128) / 128.0f;
            if (bits == 16)
            {
                short value = static_cast<short>(data[0] | (data[1] << 8));
                return value / 32768.0f;
            }
            if (bits == 24)
            {
                int value = data[0] | (data[1] << 8) | (data[2] << 16);
                if (value & 0x00800000) value |= 0xff000000;
                return value / 8388608.0f;
            }
            if (bits == 32)
            {
                int value = static_cast<int>(data[0]) |
                            (static_cast<int>(data[1]) << 8) |
                            (static_cast<int>(data[2]) << 16) |
                            (static_cast<int>(data[3]) << 24);
                return value / 2147483648.0f;
            }
        }
        else if (format == 3)
        {
            if (bits == 32)
            {
                float value = 0.0f;
                std::memcpy(&value, data, sizeof(value));
                return value;
            }
            if (bits == 64)
            {
                double value = 0.0;
                std::memcpy(&value, data, sizeof(value));
                return static_cast<float>(value);
            }
        }
        return 0.0f;
    }

    short ToPcm16(float value)
    {
        // Soft limiting preserves summed voices without abrupt digital clipping.
        value = static_cast<float>(std::tanh(value));
        return static_cast<short>(value * 32767.0f);
    }
}

SampleEngine::SampleEngine()
    : m_thread(NULL), m_stopEvent(NULL), m_waveOut(NULL),
      m_outputSampleRate(0), m_nextVoiceId(1), m_lastVoiceId(0)
{
    InitializeCriticalSection(&m_lock);
    std::memset(m_headers, 0, sizeof(m_headers));
}

SampleEngine::~SampleEngine()
{
    Shutdown();
    DeleteCriticalSection(&m_lock);
}

bool SampleEngine::Start(unsigned int outputSampleRate, wxString* errorMessage)
{
    if (outputSampleRate == 0)
    {
        if (errorMessage) *errorMessage = wxT("Invalid output sample rate.");
        return false;
    }
    if (m_waveOut && m_outputSampleRate == outputSampleRate)
        return true;
    if (m_waveOut)
        Shutdown();
    if (!OpenDevice(outputSampleRate, errorMessage))
        return false;
    m_stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!m_stopEvent)
    {
        if (errorMessage) *errorMessage = wxT("Unable to create the audio engine stop event.");
        CloseDevice();
        return false;
    }
    m_thread = CreateThread(NULL, 0, &SampleEngine::ThreadEntry, this, 0, NULL);
    if (!m_thread)
    {
        if (errorMessage) *errorMessage = wxT("Unable to start the audio engine thread.");
        CloseHandle(m_stopEvent); m_stopEvent = NULL;
        CloseDevice();
        return false;
    }
    return true;
}

void SampleEngine::Shutdown()
{
    if (m_stopEvent) SetEvent(m_stopEvent);
    if (m_thread)
    {
        WaitForSingleObject(m_thread, 3000);
        CloseHandle(m_thread);
        m_thread = NULL;
    }
    if (m_stopEvent)
    {
        CloseHandle(m_stopEvent);
        m_stopEvent = NULL;
    }
    CloseDevice();
    EnterCriticalSection(&m_lock);
    m_voices.clear();
    m_lastVoiceId = 0;
    LeaveCriticalSection(&m_lock);
}

bool SampleEngine::OpenDevice(unsigned int outputSampleRate, wxString* errorMessage)
{
    WAVEFORMATEX format;
    std::memset(&format, 0, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = OutputChannels;
    format.nSamplesPerSec = outputSampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    MMRESULT result = waveOutOpen(&m_waveOut, WAVE_MAPPER, &format, 0, 0,
                                  CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR)
    {
        m_waveOut = NULL;
        if (errorMessage) *errorMessage = wxT("Unable to open the WinMM waveOut device.");
        return false;
    }
    m_outputSampleRate = outputSampleRate;
    return true;
}

void SampleEngine::CloseDevice()
{
    if (!m_waveOut) return;
    waveOutReset(m_waveOut);
    for (int i = 0; i < BufferCount; ++i)
    {
        if (m_headers[i].dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(m_waveOut, &m_headers[i], sizeof(WAVEHDR));
    }
    waveOutClose(m_waveOut);
    m_waveOut = NULL;
    m_outputSampleRate = 0;
}

DWORD WINAPI SampleEngine::ThreadEntry(LPVOID parameter)
{
    return static_cast<SampleEngine*>(parameter)->Run();
}

DWORD SampleEngine::Run()
{
    for (int i = 0; i < BufferCount; ++i)
    {
        m_buffers[i].resize(BufferFrames * OutputChannels);
        FillBuffer(&m_buffers[i][0], BufferFrames);
        m_headers[i].lpData = reinterpret_cast<LPSTR>(&m_buffers[i][0]);
        m_headers[i].dwBufferLength = static_cast<DWORD>(m_buffers[i].size() * sizeof(short));
        waveOutPrepareHeader(m_waveOut, &m_headers[i], sizeof(WAVEHDR));
        waveOutWrite(m_waveOut, &m_headers[i], sizeof(WAVEHDR));
    }

    while (WaitForSingleObject(m_stopEvent, 2) == WAIT_TIMEOUT)
    {
        for (int i = 0; i < BufferCount; ++i)
        {
            if ((m_headers[i].dwFlags & WHDR_DONE) != 0)
            {
                FillBuffer(&m_buffers[i][0], BufferFrames);
                m_headers[i].dwFlags &= ~WHDR_DONE;
                waveOutWrite(m_waveOut, &m_headers[i], sizeof(WAVEHDR));
            }
        }
    }
    return 0;
}

bool SampleEngine::DecodeWav(const SamplePoolItem& source,
                             DecodedAudio* decoded,
                             wxString* errorMessage) const
{
    if (!decoded) return false;
    wxFFile file(source.filePath, wxT("rb"));
    if (!file.IsOpened() || !file.Seek(static_cast<wxFileOffset>(source.wav.dataOffset), wxFromStart))
    {
        if (errorMessage) *errorMessage = wxT("Unable to open the slice source WAV.");
        return false;
    }
    const unsigned int bytesPerSample = source.wav.bitsPerSample / 8;
    const unsigned int bytesPerFrame = bytesPerSample * source.wav.channelCount;
    if (!bytesPerSample || !bytesPerFrame)
    {
        if (errorMessage) *errorMessage = wxT("Invalid WAV frame size.");
        return false;
    }
    std::vector<unsigned char> raw(static_cast<size_t>(source.wav.dataBytes));
    if (!raw.empty() && file.Read(&raw[0], raw.size()) != raw.size())
    {
        if (errorMessage) *errorMessage = wxT("The slice source WAV is truncated.");
        return false;
    }
    decoded->sampleRate = source.wav.sampleRate;
    decoded->channels = source.wav.channelCount;
    decoded->frameCount = source.wav.frameCount;
    decoded->samples.resize(static_cast<size_t>(decoded->frameCount * decoded->channels));
    for (unsigned long long frame = 0; frame < decoded->frameCount; ++frame)
    {
        const unsigned char* frameData = &raw[static_cast<size_t>(frame * bytesPerFrame)];
        for (unsigned int channel = 0; channel < decoded->channels; ++channel)
        {
            decoded->samples[static_cast<size_t>(frame * decoded->channels + channel)] =
                DecodeSample(frameData + channel * bytesPerSample,
                             source.wav.audioFormat, source.wav.bitsPerSample);
        }
    }
    return true;
}

bool SampleEngine::NoteOn(const SamplePoolItem& source,
                          const AudioSlice& slice,
                          int midiNote,
                          PlayMode mode,
                          wxString* errorMessage)
{
    return NoteOn(source, slice, midiNote, 127, mode, NULL, errorMessage);
}

bool SampleEngine::NoteOn(const SamplePoolItem& source,
                          const AudioSlice& slice,
                          int midiNote,
                          int velocity,
                          PlayMode mode,
                          unsigned long* voiceId,
                          wxString* errorMessage)
{
    DecodedAudio decoded;
    if (!DecodeWav(source, &decoded, errorMessage)) return false;

    // A fixed device rate permits simultaneous voices from WAV files with
    // different native sample rates. Each voice compensates in its step.
    const unsigned int deviceRate = 44100;
    if (!Start(deviceRate, errorMessage)) return false;

    EnterCriticalSection(&m_lock);

    if (mode == ModeLegato)
    {
        for (size_t i = m_voices.size(); i > 0; --i)
        {
            Voice& existing = m_voices[i - 1];
            if (existing.active && existing.slice.id == slice.id)
            {
                existing.midiNote = midiNote;
                existing.step = (static_cast<double>(existing.audio.sampleRate) /
                                 static_cast<double>(m_outputSampleRate)) *
                                std::pow(2.0, (midiNote - existing.slice.rootMidiNote) / 12.0);
                existing.gain = std::max(0.0f, std::min(1.0f, velocity / 127.0f));
                existing.noteHeld = true;
                existing.releasePending = false;
                existing.immediateFade = false;
                existing.fadeRemaining = 0;
                m_lastVoiceId = existing.id;
                if (voiceId) *voiceId = existing.id;
                LeaveCriticalSection(&m_lock);
                return true;
            }
        }
    }

    RemoveInactiveVoices();
    if (m_voices.size() >= MaxVoices)
        m_voices.erase(m_voices.begin()); // deterministic oldest-voice stealing

    Voice voice;
    voice.id = m_nextVoiceId++;
    if (m_nextVoiceId == 0) m_nextVoiceId = 1;
    BeginVoice(voice, decoded, slice, midiNote, velocity);
    m_lastVoiceId = voice.id;
    m_voices.push_back(voice);
    if (voiceId) *voiceId = voice.id;
    LeaveCriticalSection(&m_lock);
    return true;
}

void SampleEngine::BeginVoice(Voice& voice,
                              const DecodedAudio& decoded,
                              const AudioSlice& slice,
                              int midiNote,
                              int velocity)
{
    voice.audio = decoded;
    voice.slice = slice;
    voice.midiNote = midiNote;
    voice.position = static_cast<double>(slice.startFrame);
    voice.step = (static_cast<double>(decoded.sampleRate) /
                  static_cast<double>(m_outputSampleRate)) *
                 std::pow(2.0, (midiNote - slice.rootMidiNote) / 12.0);
    voice.gain = std::max(0.0f, std::min(1.0f, velocity / 127.0f));
    voice.active = true;
    voice.noteHeld = true;
    voice.releasePending = false;
    voice.enteredLoop = false;
    voice.immediateFade = false;
    voice.fadeRemaining = 0;
}

SampleEngine::Voice* SampleEngine::FindVoice(unsigned long voiceId)
{
    for (size_t i = 0; i < m_voices.size(); ++i)
        if (m_voices[i].id == voiceId) return &m_voices[i];
    return NULL;
}

const SampleEngine::Voice* SampleEngine::FindVoice(unsigned long voiceId) const
{
    for (size_t i = 0; i < m_voices.size(); ++i)
        if (m_voices[i].id == voiceId) return &m_voices[i];
    return NULL;
}

void SampleEngine::NoteOff()
{
    NoteOff(m_lastVoiceId);
}

void SampleEngine::NoteOff(unsigned long voiceId)
{
    EnterCriticalSection(&m_lock);
    Voice* voice = FindVoice(voiceId);
    if (voice && voice->active)
    {
        voice->noteHeld = false;
        if (!voice->enteredLoop)
        {
            voice->immediateFade = true;
            voice->fadeRemaining = 128;
        }
        else
        {
            voice->releasePending = true;
        }
    }
    LeaveCriticalSection(&m_lock);
}

void SampleEngine::EndVoiceImmediately(Voice& voice)
{
    voice.active = false;
    voice.noteHeld = false;
    voice.releasePending = false;
    voice.immediateFade = false;
    voice.fadeRemaining = 0;
}

void SampleEngine::StopAll()
{
    EnterCriticalSection(&m_lock);
    for (size_t i = 0; i < m_voices.size(); ++i)
        EndVoiceImmediately(m_voices[i]);
    m_voices.clear();
    m_lastVoiceId = 0;
    LeaveCriticalSection(&m_lock);
}

bool SampleEngine::IsVoiceActive() const
{
    EnterCriticalSection(&m_lock);
    bool active = false;
    for (size_t i = 0; i < m_voices.size(); ++i)
        if (m_voices[i].active) { active = true; break; }
    LeaveCriticalSection(&m_lock);
    return active;
}

bool SampleEngine::IsVoiceActive(unsigned long voiceId) const
{
    EnterCriticalSection(&m_lock);
    const Voice* voice = FindVoice(voiceId);
    const bool active = voice && voice->active;
    LeaveCriticalSection(&m_lock);
    return active;
}

float SampleEngine::ReadInterpolated(const Voice& voice, unsigned int channel) const
{
    if (!voice.active || voice.audio.samples.empty() || voice.audio.frameCount == 0)
        return 0.0f;
    unsigned long long frame0 = static_cast<unsigned long long>(voice.position);
    if (frame0 >= voice.audio.frameCount) frame0 = voice.audio.frameCount - 1;
    unsigned long long frame1 = std::min<unsigned long long>(frame0 + 1, voice.audio.frameCount - 1);
    const double fraction = voice.position - frame0;
    const unsigned int sourceChannel = voice.audio.channels == 1 ? 0 :
        std::min<unsigned int>(channel, voice.audio.channels - 1);
    const float a = voice.audio.samples[static_cast<size_t>(frame0 * voice.audio.channels + sourceChannel)];
    const float b = voice.audio.samples[static_cast<size_t>(frame1 * voice.audio.channels + sourceChannel)];
    return static_cast<float>(a + (b - a) * fraction);
}

void SampleEngine::RemoveInactiveVoices()
{
    for (size_t i = 0; i < m_voices.size(); )
    {
        if (!m_voices[i].active) m_voices.erase(m_voices.begin() + i);
        else ++i;
    }
}

void SampleEngine::FillBuffer(short* output, unsigned int frameCount)
{
    EnterCriticalSection(&m_lock);
    for (unsigned int outFrame = 0; outFrame < frameCount; ++outFrame)
    {
        float mixed[OutputChannels] = { 0.0f, 0.0f };

        for (size_t vi = 0; vi < m_voices.size(); ++vi)
        {
            Voice& voice = m_voices[vi];
            if (!voice.active) continue;

            float fadeGain = 1.0f;
            if (voice.immediateFade)
            {
                fadeGain = voice.fadeRemaining / 128.0f;
                if (voice.fadeRemaining > 0) --voice.fadeRemaining;
                if (voice.fadeRemaining == 0)
                {
                    EndVoiceImmediately(voice);
                    continue;
                }
            }

            for (unsigned int channel = 0; channel < OutputChannels; ++channel)
                mixed[channel] += ReadInterpolated(voice, channel) * voice.gain * fadeGain * 0.70f;

            voice.position += voice.step;

            if (voice.slice.loopEnabled && voice.position >= voice.slice.loopInFrame)
                voice.enteredLoop = true;

            if (voice.slice.loopEnabled && voice.enteredLoop && voice.position >= voice.slice.loopOutFrame)
            {
                if (voice.noteHeld)
                {
                    const double overflow = voice.position - voice.slice.loopOutFrame;
                    voice.position = voice.slice.loopInFrame + overflow;
                }
                else if (voice.releasePending)
                {
                    voice.releasePending = false;
                    // Continue from loopOut through the natural tail.
                }
            }

            if (voice.position >= voice.slice.endFrame || voice.position >= voice.audio.frameCount)
                EndVoiceImmediately(voice);
        }

        for (unsigned int channel = 0; channel < OutputChannels; ++channel)
            output[outFrame * OutputChannels + channel] = ToPcm16(mixed[channel]);
    }
    RemoveInactiveVoices();
    LeaveCriticalSection(&m_lock);
}
