#pragma once

#include <wx/string.h>
#include <wx/msw/wrapwin.h>
#include <mmsystem.h>
#include <vector>

class TonePreview
{
public:
    TonePreview();
    ~TonePreview();

    // Plays a pure sine tone directly through waveOut.
    // No Festival voice, phoneme, WAV file, or external player is involved.
    bool Play(const wxString& pitch, double beats, double bpm);
    void Stop();

private:
    TonePreview(const TonePreview&);
    TonePreview& operator=(const TonePreview&);

    bool OpenAndWrite(double frequency, double durationSeconds);

    HWAVEOUT m_waveOut;
    WAVEHDR m_header;
    std::vector<short> m_samples;
    bool m_prepared;
};
