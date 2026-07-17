#include "tone_preview.h"
#include "song_model.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    const double PI_VALUE = 3.14159265358979323846;

    double MidiFrequency(int midi)
    {
        return 440.0 * std::pow(2.0, (static_cast<double>(midi) - 69.0) / 12.0);
    }

    double Clamp(double value, double minimum, double maximum)
    {
        return std::max(minimum, std::min(maximum, value));
    }
}

TonePreview::TonePreview()
    : m_waveOut(NULL),
      m_prepared(false)
{
    std::memset(&m_header, 0, sizeof(m_header));
}

TonePreview::~TonePreview()
{
    Stop();
}

bool TonePreview::Play(const wxString& pitch, double beats, double bpm)
{
    const int midi = PitchToMidi(pitch);
    const double frequency = MidiFrequency(midi);

    const double safeBpm = bpm > 1.0 ? bpm : 120.0;
    const double musicalDuration = std::max(0.0625, beats) * 60.0 / safeBpm;

    // Editing feedback must remain immediate and must not occupy the audio
    // device for the entire note when a long event is being resized.
    const double previewDuration = Clamp(musicalDuration, 0.08, 0.65);

    return OpenAndWrite(frequency, previewDuration);
}

void TonePreview::Stop()
{
    if (m_waveOut == NULL)
        return;

    waveOutReset(m_waveOut);

    if (m_prepared)
    {
        waveOutUnprepareHeader(m_waveOut, &m_header, sizeof(m_header));
        m_prepared = false;
    }

    waveOutClose(m_waveOut);
    m_waveOut = NULL;
    m_samples.clear();
    std::memset(&m_header, 0, sizeof(m_header));
}

bool TonePreview::OpenAndWrite(double frequency, double durationSeconds)
{
    Stop();

    const DWORD sampleRate = 22050;
    const size_t sampleCount = static_cast<size_t>(
        std::max(1.0, durationSeconds * static_cast<double>(sampleRate)));

    m_samples.resize(sampleCount);

    const double attackSeconds = std::min(0.015, durationSeconds * 0.25);
    const double releaseSeconds = std::min(0.035, durationSeconds * 0.35);
    const size_t attackSamples = static_cast<size_t>(attackSeconds * sampleRate);
    const size_t releaseSamples = static_cast<size_t>(releaseSeconds * sampleRate);
    const double amplitude = 0.22 * 32767.0;

    for (size_t i = 0; i < sampleCount; ++i)
    {
        double envelope = 1.0;

        if (attackSamples > 0 && i < attackSamples)
            envelope = static_cast<double>(i) / attackSamples;

        if (releaseSamples > 0 && i + releaseSamples >= sampleCount)
        {
            const size_t remaining = sampleCount - i;
            envelope = std::min(
                envelope,
                static_cast<double>(remaining) / releaseSamples);
        }

        const double phase =
            2.0 * PI_VALUE * frequency *
            static_cast<double>(i) / sampleRate;

        m_samples[i] = static_cast<short>(
            amplitude * envelope * std::sin(phase));
    }

    WAVEFORMATEX format;
    std::memset(&format, 0, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = sampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign =
        static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    MMRESULT result = waveOutOpen(
        &m_waveOut, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);

    if (result != MMSYSERR_NOERROR)
    {
        m_waveOut = NULL;
        m_samples.clear();
        return false;
    }

    std::memset(&m_header, 0, sizeof(m_header));
    m_header.lpData = reinterpret_cast<LPSTR>(&m_samples[0]);
    m_header.dwBufferLength =
        static_cast<DWORD>(m_samples.size() * sizeof(short));

    result = waveOutPrepareHeader(m_waveOut, &m_header, sizeof(m_header));
    if (result != MMSYSERR_NOERROR)
    {
        Stop();
        return false;
    }

    m_prepared = true;
    result = waveOutWrite(m_waveOut, &m_header, sizeof(m_header));

    if (result != MMSYSERR_NOERROR)
    {
        Stop();
        return false;
    }

    return true;
}
