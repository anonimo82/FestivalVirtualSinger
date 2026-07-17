#pragma once

#include "song_model.h"

#include <wx/event.h>
#include <wx/string.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

wxDECLARE_EVENT(EVT_FESTIVAL_STATUS, wxThreadEvent);

enum FestivalStatusCode
{
    FestivalStatus_Info = 0,
    FestivalStatus_Ready = 1,
    FestivalStatus_Playing = 2,
    FestivalStatus_Error = 3
};

class FestivalBridge
{
public:
    FestivalBridge();
    ~FestivalBridge();

    void Start(wxEvtHandler* eventTarget);
    void Shutdown();

    // Audible proof that the Festival COM engine is connected.
    void TestConnection(const wxString& voice);


    // Real-time Festival singing preview of only the selected event.
    void PreviewPhoneme(const wxString& voice,
                        const SingingEvent& event,
                        double bpm);

    // Direct Festival playback of the complete singing sequence.
    void PlaySong(const SingingSong& song);

    // Render the complete singing sequence directly to a WAV file.
    void RenderSong(const SingingSong& song,
                    const wxString& waveFilePath);

    void Stop();

private:
    enum CommandKind
    {
        Command_Test,
        Command_Phoneme,
        Command_Song,
        Command_Render,
        Command_Stop,
        Command_Shutdown
    };

    struct Command
    {
        CommandKind kind;
        wxString voice;
        wxString scheme;
        wxString label;
        wxString outputPath;
    };

    FestivalBridge(const FestivalBridge&);
    FestivalBridge& operator=(const FestivalBridge&);

    void Push(const Command& command,
              bool replaceSameKind,
              bool highPriority);
    void WorkerMain();
    void PostStatus(int code, const wxString& message);

    wxEvtHandler* m_eventTarget;
    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<Command> m_commands;
    bool m_started;
};
