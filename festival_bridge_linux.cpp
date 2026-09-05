#include "festival_bridge.h"

#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/process.h>
#include <wx/utils.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

wxDEFINE_EVENT(EVT_FESTIVAL_STATUS, wxThreadEvent);

namespace {
wxString ShellQuote(const wxString& s)
{
    wxString r="'";
    for(wxUniChar c:s){if(c=='\'')r += "'\\''";else r += c;}
    r += "'"; return r;
}
wxString TempSchemePath()
{
    wxString p=wxFileName::CreateTempFileName("festival_virtual_singer_");
    return p;
}
wxString NormalizeDirectSchemeForLinux(const wxString& input)
{
    wxString scheme = input;
    scheme.Replace(wxT("\n@@FESTIVAL_STEP@@\n"), wxT("\n"));
    scheme.Replace(wxT("@@FESTIVAL_STEP@@"), wxT("\n"));

    // The Windows COM backend used (audio_mode 'shutup) before playback.
    // Native Festival on Linux errors if shutup is requested while not in
    // async mode, so omit it here and keep synchronous playback.
    scheme.Replace(wxT("(audio_mode 'shutup)\n"), wxT(""));
    scheme.Replace(wxT("(audio_mode 'shutup)"), wxT(""));
    return scheme;
}

bool RunFestivalScheme(const wxString& inputScheme, wxString* error)
{
    const wxString scheme = NormalizeDirectSchemeForLinux(inputScheme);
    const wxString path = TempSchemePath();

    {
        std::ofstream f(path.ToStdString().c_str());
        if (!f)
        {
            if (error) *error = "Unable to create temporary Festival script.";
            return false;
        }

        // singing-mode.scm defines singing_init_func/singing_exit_func.
        // Debian/Ubuntu Festival installs it on Festival's load-path, so
        // require resolves the correct distribution-specific location.
        f << "(require 'singing-mode)\n";
        f << scheme.ToStdString();
        f << "\n";
    }

    // Keep stderr/stdout in a sidecar file long enough to report the actual
    // Festival/Scheme error to the GUI instead of only returning exit 255.
    const wxString logPath = path + wxT(".log");
    const wxString cmd =
        "festival -b " + ShellQuote(path) +
        " >" + ShellQuote(logPath) + " 2>&1";

    const int rc = std::system(cmd.ToUTF8().data());

    wxString details;
    {
        std::ifstream log(logPath.ToStdString().c_str());
        if (log)
        {
            std::ostringstream ss;
            ss << log.rdbuf();
            details = wxString::FromUTF8(ss.str());
            details.Trim(true);
            details.Trim(false);
        }
    }

    wxRemoveFile(path);
    wxRemoveFile(logPath);

    if (rc != 0)
    {
        int exitCode = rc;
        if (WIFEXITED(rc))
            exitCode = WEXITSTATUS(rc);
        else if (WIFSIGNALED(rc))
            exitCode = 128 + WTERMSIG(rc);

        if (error)
        {
            *error = wxString::Format("Festival exited with code %d", exitCode);
            if (!details.IsEmpty())
                *error += wxT(": ") + details;
        }
        return false;
    }

    return true;
}
wxString VoiceCommand(const wxString& voice)
{
    wxString v=SanitizeFestivalVoice(voice);
    return v.IsEmpty()?wxString():("(voice_"+v+")\n");
}
}

FestivalBridge::FestivalBridge():m_eventTarget(NULL),m_started(false){}
FestivalBridge::~FestivalBridge(){Shutdown();}
void FestivalBridge::Start(wxEvtHandler* target){if(m_started)return;m_eventTarget=target;m_started=true;m_worker=std::thread(&FestivalBridge::WorkerMain,this);}
void FestivalBridge::Shutdown(){if(!m_started)return;Command c;c.kind=Command_Shutdown;Push(c,false,true);if(m_worker.joinable())m_worker.join();m_started=false;m_eventTarget=NULL;}
void FestivalBridge::TestConnection(const wxString& voice){Command c;c.kind=Command_Test;c.voice=voice;c.label="Test Festival connection";Push(c,true,true);}
void FestivalBridge::PreviewPhoneme(const wxString& voice,const SingingEvent&e,double bpm){Command c;c.kind=Command_Phoneme;c.voice=voice;c.scheme=BuildDirectSingingScheme(std::vector<SingingEvent>(1,e),bpm);c.label="Phoneme preview";Push(c,true,false);}
void FestivalBridge::PlaySong(const SingingSong&s){Command c;c.kind=Command_Song;c.voice=s.voice;c.scheme=BuildDirectSingingScheme(s.events,s.bpm);c.label="Sequence playback";Push(c,true,false);}
void FestivalBridge::RenderSong(const SingingSong&s,const wxString&path){Command c;c.kind=Command_Render;c.voice=s.voice;c.outputPath=path;c.scheme=BuildDirectSingingRenderScheme(s.events,s.bpm,path);c.label="WAV export: "+wxFileName(path).GetFullName();Push(c,false,false);}
void FestivalBridge::Stop(){Command c;c.kind=Command_Stop;c.label="Stop";Push(c,false,true);}
void FestivalBridge::Push(const Command&c,bool replace,bool priority){if(!m_started)return;{std::lock_guard<std::mutex>lock(m_mutex);if(priority){m_commands.clear();m_commands.push_front(c);}else{if(replace)for(auto it=m_commands.begin();it!=m_commands.end();)if(it->kind==c.kind)it=m_commands.erase(it);else++it;m_commands.push_back(c);}}m_condition.notify_one();}
void FestivalBridge::PostStatus(int code,const wxString&message){if(!m_eventTarget)return;wxThreadEvent*e=new wxThreadEvent(EVT_FESTIVAL_STATUS);e->SetInt(code);e->SetString(message);wxQueueEvent(m_eventTarget,e);}
void FestivalBridge::WorkerMain()
{
    if(std::system("command -v festival >/dev/null 2>&1")!=0)PostStatus(FestivalStatus_Error,"Festival executable not found. Install the 'festival' package.");
    else PostStatus(FestivalStatus_Ready,"Festival native Linux backend ready.");
    while(true){Command c;{std::unique_lock<std::mutex>lock(m_mutex);m_condition.wait(lock,[this]{return !m_commands.empty();});c=m_commands.front();m_commands.pop_front();}if(c.kind==Command_Shutdown)break;
        if(c.kind==Command_Stop){std::system("pkill -TERM -x festival >/dev/null 2>&1");PostStatus(FestivalStatus_Ready,"Stop completed.");continue;}
        PostStatus(FestivalStatus_Playing,c.label+"...");
        wxString scheme;
        if(c.kind==Command_Test)scheme=VoiceCommand(c.voice)+"(SayText \"Festival connected successfully\")\n";
        else scheme=VoiceCommand(c.voice)+c.scheme+"\n";
        wxString error;bool ok=RunFestivalScheme(scheme,&error);
        if(ok&&c.kind==Command_Render&&(!wxFileExists(c.outputPath)||wxFileName(c.outputPath).GetSize().GetValue()<=44)){ok=false;error="Festival did not create a valid WAV file.";}
        if(ok)PostStatus(FestivalStatus_Ready,c.kind==Command_Render?"Audio exported: "+c.outputPath:c.label+" completed.");
        else PostStatus(FestivalStatus_Error,c.label+" failed: "+error);
    }
}
