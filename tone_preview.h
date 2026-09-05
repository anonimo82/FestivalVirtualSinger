#pragma once

#include <wx/string.h>
#include <atomic>
#include <mutex>
#include <thread>

class TonePreview
{
public:
    TonePreview();
    ~TonePreview();

    bool Play(const wxString& pitch, double beats, double bpm);
    void Stop();

private:
    TonePreview(const TonePreview&);
    TonePreview& operator=(const TonePreview&);

    bool OpenAndWrite(double frequency, double durationSeconds);
    void Worker(double frequency, double durationSeconds);

    std::mutex m_mutex;
    std::thread m_thread;
    std::atomic<bool> m_stopRequested;
    void* m_pcm;
};
