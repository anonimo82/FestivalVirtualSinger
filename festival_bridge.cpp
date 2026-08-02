#include "festival_bridge.h"

#include <windows.h>
#include <objbase.h>
#include <oleauto.h>

#include <wx/filename.h>
#include <wx/ffile.h>
#include <wx/filefn.h>

#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

wxDEFINE_EVENT(EVT_FESTIVAL_STATUS, wxThreadEvent);

namespace
{
    const CLSID CLSID_FestivalTTSEngine =
    { 0xde59c381, 0x1fb0, 0x40f3,
      { 0x96, 0x88, 0xdb, 0xea, 0xbe, 0x7a, 0x94, 0x66 } };

    const IID IID_IFestivalTTSEngine =
    { 0x0014fb73, 0xcdd9, 0x4bd2,
      { 0x83, 0xd1, 0x07, 0x47, 0xa0, 0x12, 0xdb, 0x58 } };

    struct IFestivalTTSEngineLocal : public IDispatch
    {
        virtual HRESULT __stdcall raw_say_text(BSTR inText) = 0;
        virtual HRESULT __stdcall raw_eval_command(BSTR inCommand) = 0;
        virtual HRESULT __stdcall raw_text_to_wave(BSTR inText,
                                                    BSTR inFile) = 0;
    };

    typedef HRESULT (STDAPICALLTYPE* DllGetClassObjectProc)(
        REFCLSID rclsid,
        REFIID riid,
        LPVOID* ppv);

    wxString HResultHex(HRESULT hr)
    {
        return wxString::Format(
            wxT("0x%08lX"),
            static_cast<unsigned long>(hr));
    }

    wxString HResultMessage(HRESULT hr)
    {
        // S_FALSE (1) is a successful HRESULT. Passing the value 1
        // directly to FormatMessage makes Windows display the unrelated
        // Win32 message "Funzione non corretta".
        if (hr == S_OK)
            return wxT("Operation completed (S_OK, 0x00000000)");

        if (hr == S_FALSE)
            return wxString(wxT("FestivalTTSCOM: Festival command failed (S_FALSE, 0x00000001)"));

        wchar_t* buffer = NULL;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                            FORMAT_MESSAGE_FROM_SYSTEM |
                            FORMAT_MESSAGE_IGNORE_INSERTS;

        FormatMessageW(
            flags,
            NULL,
            static_cast<DWORD>(hr),
            0,
            reinterpret_cast<wchar_t*>(&buffer),
            0,
            NULL);

        wxString result;
        if (buffer != NULL)
        {
            result = buffer;
            LocalFree(buffer);
            result.Trim(true);
            result.Trim(false);
        }

        if (result.IsEmpty())
        {
            result = wxString::Format(
                wxT("HRESULT 0x%08lX"),
                static_cast<unsigned long>(hr));
        }
        else
        {
            result << wxString::Format(
                wxT(" (0x%08lX)"),
                static_cast<unsigned long>(hr));
        }

        return result;
    }

    wxString ApplicationDirectory()
    {
        wchar_t path[MAX_PATH] = { 0 };
        GetModuleFileNameW(NULL, path, MAX_PATH);
        return wxFileName(path).GetPath();
    }

    void PrependEnvironmentPath(const wxString& directory)
    {
        wchar_t oldPath[32768] = { 0 };
        GetEnvironmentVariableW(
            L"PATH",
            oldPath,
            static_cast<DWORD>(sizeof(oldPath) / sizeof(oldPath[0])));

        wxString value = directory;
        if (oldPath[0] != 0)
            value << wxT(";") << oldPath;

        SetEnvironmentVariableW(L"PATH", value.wc_str());
    }

    void ConfigureLocalFestivalEnvironment(const wxString& appDir)
    {
        const wxString localRoot =
            appDir + wxT("\\festival_runtime\\festival_home");
        const wxString festivalHome =
            localRoot + wxT("\\festival");
        const wxString speechTools =
            localRoot + wxT("\\speech_tools");

        if (!wxDirExists(localRoot))
            return;

        SetEnvironmentVariableW(L"FESTIVAL_HOME", festivalHome.wc_str());
        SetEnvironmentVariableW(L"ESTDIR", speechTools.wc_str());
        SetEnvironmentVariableW(L"EST_HOME", speechTools.wc_str());
        SetEnvironmentVariableW(
            L"FESTIVAL_WINDOWS_ROOT",
            localRoot.wc_str());

        PrependEnvironmentPath(localRoot);
        PrependEnvironmentPath(festivalHome);
    }

    class ScopedBstr
    {
    public:
        explicit ScopedBstr(const wxString& text)
            : m_value(SysAllocString(text.wc_str()))
        {
        }

        ~ScopedBstr()
        {
            if (m_value != NULL)
                SysFreeString(m_value);
        }

        operator BSTR() const
        {
            return m_value;
        }

        bool IsOk() const
        {
            return m_value != NULL;
        }

    private:
        BSTR m_value;
    };

    wxString ResolveFestivalLibDirectory()
    {
        const wxString appDir = ApplicationDirectory();
        std::vector<wxString> candidates;

        candidates.push_back(
            appDir +
            wxT("\\festival_runtime\\festival_home\\festival\\lib"));
        candidates.push_back(
            appDir +
            wxT("\\festival_runtime\\festival_home\\lib"));
        candidates.push_back(wxT("C:\\festival\\festival\\lib"));
        candidates.push_back(wxT("C:\\festival\\lib"));

        wchar_t festivalHome[32768] = { 0 };
        const DWORD length = GetEnvironmentVariableW(
            L"FESTIVAL_HOME",
            festivalHome,
            static_cast<DWORD>(
                sizeof(festivalHome) / sizeof(festivalHome[0])));

        if (length > 0 &&
            length < sizeof(festivalHome) / sizeof(festivalHome[0]))
        {
            candidates.push_back(
                wxString(festivalHome) + wxT("\\lib"));
        }

        const wxString diagnosticPath =
            appDir + wxT("\\festival_path_diagnostics.txt");
        wxFFile diagnostic(diagnosticPath, wxT("w"));

        if (diagnostic.IsOpened())
        {
            diagnostic.Write(wxT("ApplicationDirectory = "));
            diagnostic.Write(appDir);
            diagnostic.Write(wxT("\r\n"));
        }

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            const wxString modeFile =
                candidates[i] + wxT("\\singing-mode.scm");
            const wxString dtdFile =
                candidates[i] + wxT("\\Singing.v0_1.dtd");

            const bool directoryExists = wxDirExists(candidates[i]);
            const bool modeExists = wxFileExists(modeFile);
            const bool dtdExists = wxFileExists(dtdFile);

            if (diagnostic.IsOpened())
            {
                diagnostic.Write(wxT("\r\nCandidate "));
                diagnostic.Write(wxString::Format(
                    wxT("%u"),
                    static_cast<unsigned int>(i + 1)));
                diagnostic.Write(wxT("\r\nDirectory = "));
                diagnostic.Write(candidates[i]);
                diagnostic.Write(wxT("\r\nDirectoryExists = "));
                diagnostic.Write(directoryExists ? wxT("1") : wxT("0"));
                diagnostic.Write(wxT("\r\nMode = "));
                diagnostic.Write(modeFile);
                diagnostic.Write(wxT("\r\nModeExists = "));
                diagnostic.Write(modeExists ? wxT("1") : wxT("0"));
                diagnostic.Write(wxT("\r\nDTD = "));
                diagnostic.Write(dtdFile);
                diagnostic.Write(wxT("\r\nDTDExists = "));
                diagnostic.Write(dtdExists ? wxT("1") : wxT("0"));
                diagnostic.Write(wxT("\r\n"));
            }

            if (modeExists && dtdExists)
            {
                if (diagnostic.IsOpened())
                {
                    diagnostic.Write(wxT("\r\nSELECTED = "));
                    diagnostic.Write(candidates[i]);
                    diagnostic.Write(wxT("\r\n"));
                }

                return candidates[i];
            }
        }

        if (diagnostic.IsOpened())
            diagnostic.Write(wxT("\r\nSELECTED = NONE\r\n"));

        return wxEmptyString;
    }

    bool IsStrictSuccess(HRESULT hr)
    {
        // FestivalTTSCOM returns S_OK only when the underlying Festival
        // function reports TRUE. S_FALSE therefore represents a real
        // Festival evaluation failure and must not be ignored.
        return hr == S_OK;
    }

    void AppendSingingTrace(const wxString& line)
    {
        const wxString path =
            ApplicationDirectory() + wxT("\\festival_singing_trace.txt");
        wxFFile file(path, wxT("a"));

        if (!file.IsOpened())
            return;

        SYSTEMTIME time;
        GetLocalTime(&time);

        file.Write(wxString::Format(
            wxT("[%02u:%02u:%02u.%03u] "),
            static_cast<unsigned int>(time.wHour),
            static_cast<unsigned int>(time.wMinute),
            static_cast<unsigned int>(time.wSecond),
            static_cast<unsigned int>(time.wMilliseconds)));
        file.Write(line);
        file.Write(wxT("\r\n"));
    }

    class FestivalComEngine
    {
    public:
        FestivalComEngine()
            : m_module(NULL),
              m_engine(NULL),
              m_initialized(false),
              m_runtimePrepared(false)
        {
        }

        ~FestivalComEngine()
        {
            if (m_engine != NULL)
            {
                m_engine->Release();
                m_engine = NULL;
            }

            if (m_module != NULL)
            {
                FreeLibrary(m_module);
                m_module = NULL;
            }

            if (m_initialized)
                CoUninitialize();
        }

        HRESULT Initialize(wxString* details)
        {
            HRESULT hr = CoInitializeEx(
                NULL,
                COINIT_APARTMENTTHREADED);

            if (hr == RPC_E_CHANGED_MODE)
                hr = CoInitialize(NULL);

            if (FAILED(hr))
            {
                if (details != NULL)
                    *details = wxT("CoInitialize: ") + HResultMessage(hr);
                return hr;
            }

            m_initialized = true;

            const wxString appDir = ApplicationDirectory();
            ConfigureLocalFestivalEnvironment(appDir);

            std::vector<wxString> candidates;
            candidates.push_back(
                appDir + wxT("\\festival_runtime\\FestivalTTSCOM.dll"));
            candidates.push_back(
                appDir +
                wxT("\\festival_runtime\\festival_home\\visualstudio")
                wxT("\\FestivalTTSCOM\\Release\\FestivalTTSCOM.dll"));
            candidates.push_back(
                wxT("C:\\festival\\visualstudio\\FestivalTTSCOM")
                wxT("\\Release\\FestivalTTSCOM.dll"));

            for (size_t i = 0; i < candidates.size(); ++i)
            {
                if (!wxFileExists(candidates[i]))
                    continue;

                hr = LoadFromDll(candidates[i]);
                if (SUCCEEDED(hr))
                {
                    hr = ConfigureWindowsAudio();
                    if (hr != S_OK)
                    {
                        if (details != NULL)
                        {
                            *details =
                                wxT("DLL loaded, but Festival audio configuration failed: ") +
                                HResultMessage(hr);
                        }
                        return hr;
                    }

                    if (details != NULL)
                    {
                        *details =
                            wxT("Festival loaded from: ") + candidates[i];
                    }
                    return S_OK;
                }
            }

            // Final attempt: use an already registered 32-bit COM server.
            hr = CoCreateInstance(
                CLSID_FestivalTTSEngine,
                NULL,
                CLSCTX_INPROC_SERVER,
                IID_IFestivalTTSEngine,
                reinterpret_cast<void**>(&m_engine));

            if (FAILED(hr) && details != NULL)
            {
                *details =
                    wxT("Neither the local DLL nor the registered COM server is available: ") +
                    HResultMessage(hr);
            }
            else if (SUCCEEDED(hr))
            {
                hr = ConfigureWindowsAudio();
                if (details != NULL)
                {
                    if (hr == S_OK)
                    {
                        *details =
                            wxT("Festival loaded through registered COM.");
                    }
                    else
                    {
                        *details =
                            wxT("COM loaded, but Festival audio configuration failed: ") +
                            HResultMessage(hr);
                    }
                }
            }

            return hr;
        }

        HRESULT SayText(const wxString& voice,
                        const wxString& text)
        {
            HRESULT hr = SelectVoice(voice);
            if (!IsStrictSuccess(hr))
                return hr;

            if (m_engine == NULL)
                return CO_E_NOTINITIALIZED;

            ScopedBstr value(text);
            if (!value.IsOk())
                return E_OUTOFMEMORY;

            // FestivalTTSCOM maps festival_say_text(TRUE/FALSE) to
            // S_OK/S_FALSE, so preserve the result exactly.
            return m_engine->raw_say_text(value);
        }

        HRESULT PlayDirectSinging(const wxString& voice,
                                  const wxString& directScheme)
        {
            AppendSingingTrace(wxT("BEGIN PlayDirectSinging"));
            AppendSingingTrace(wxT("voice = ") + voice);
            AppendSingingTrace(wxT("scheme = ") + directScheme);

            HRESULT hr = SelectVoice(voice);
            AppendSingingTrace(
                wxT("SelectVoice -> ") + HResultHex(hr));
            if (!IsStrictSuccess(hr))
                return hr;

            const wxString festivalLib =
                ResolveFestivalLibDirectory();

            AppendSingingTrace(
                wxString(wxT("ResolveFestivalLibDirectory -> ")) +
                (festivalLib.IsEmpty()
                    ? wxString(wxT("<EMPTY>"))
                    : festivalLib));

            if (festivalLib.IsEmpty())
                return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);

            const wxString modeFile =
                festivalLib + wxT("\\singing-mode.scm");

            if (!wxFileExists(modeFile))
                return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

            const wxString libPath = SchemeEscape(festivalLib);
            wxString xmlDtdPath = libPath;
            if (!xmlDtdPath.EndsWith(wxT("/")))
                xmlDtdPath += wxT("/");

            hr = EvalTrue(
                wxT("(set! xml_dtd_dir \"") +
                xmlDtdPath +
                wxT("\")"));
            AppendSingingTrace(
                wxT("Set xml_dtd_dir -> ") + HResultHex(hr));
            if (!IsStrictSuccess(hr))
                return hr;

            hr = EvalTrue(
                wxT("(set! load-path (cons \"") +
                libPath +
                wxT("\" load-path))"));
            AppendSingingTrace(
                wxT("Set load-path -> ") + HResultHex(hr));
            if (!IsStrictSuccess(hr))
                return hr;

            const wxString modePath = SchemeEscape(modeFile);
            hr = EvalTrue(
                wxT("(load \"") + modePath + wxT("\")"));
            AppendSingingTrace(
                wxT("Load singing-mode.scm -> ") + HResultHex(hr));
            if (!IsStrictSuccess(hr))
                return hr;

            const wxString separator =
                wxT("\n@@FESTIVAL_STEP@@\n");
            size_t start = 0;
            unsigned int step = 1;

            while (start <= directScheme.length())
            {
                const size_t found =
                    directScheme.find(separator, start);

                wxString command;
                if (found == wxString::npos)
                    command = directScheme.Mid(start);
                else
                    command = directScheme.Mid(start, found - start);

                command.Trim(true);
                command.Trim(false);

                if (!command.IsEmpty())
                {
                    AppendSingingTrace(
                        wxString::Format(
                            wxT("STEP %u command = "),
                            step) + command);

                    const wxString wrappedCommand =
                        wxT("(begin ") + command + wxT(" t)");

                    AppendSingingTrace(
                        wxString::Format(
                            wxT("STEP %u wrapped = "),
                            step) + wrappedCommand);

                    hr = Eval(wrappedCommand);

                    AppendSingingTrace(
                        wxString::Format(
                            wxT("STEP %u result = "),
                            step) + HResultHex(hr));

                    if (!IsStrictSuccess(hr))
                    {
                        // Ripristino best-effort dopo uno stadio fallito.
                        Eval(wxT("(begin ")
                             wxT("(if (symbol-bound? '")
                             wxT("festival_saved_tts_hooks) ")
                             wxT("(set! tts_hooks ")
                             wxT("festival_saved_tts_hooks)) ")
                             wxT("t)"));
                        Eval(wxT("(begin (singing_exit_func) t)"));
                        return hr;
                    }

                    ++step;
                }

                if (found == wxString::npos)
                    break;

                start = found + separator.length();
            }

            AppendSingingTrace(wxT("END PlayDirectSinging S_OK"));
            return S_OK;
        }

        HRESULT Stop()
        {
            AppendSingingTrace(wxT("BEGIN Stop audio"));

            const HRESULT hr =
                EvalTrue(wxT("(audio_mode 'shutup)"));

            AppendSingingTrace(
                wxT("audio_mode shutup -> ") + HResultHex(hr));
            return hr;
        }

    private:
        HRESULT LoadFromDll(const wxString& dllPath)
        {
            HMODULE module = LoadLibraryW(dllPath.wc_str());
            if (module == NULL)
                return HRESULT_FROM_WIN32(GetLastError());

            DllGetClassObjectProc getClassObject =
                reinterpret_cast<DllGetClassObjectProc>(
                    GetProcAddress(module, "DllGetClassObject"));

            if (getClassObject == NULL)
            {
                const HRESULT hr =
                    HRESULT_FROM_WIN32(GetLastError());
                FreeLibrary(module);
                return hr;
            }

            IClassFactory* factory = NULL;
            HRESULT hr = getClassObject(
                CLSID_FestivalTTSEngine,
                IID_IClassFactory,
                reinterpret_cast<void**>(&factory));

            if (FAILED(hr))
            {
                FreeLibrary(module);
                return hr;
            }

            IFestivalTTSEngineLocal* engine = NULL;
            hr = factory->CreateInstance(
                NULL,
                IID_IFestivalTTSEngine,
                reinterpret_cast<void**>(&engine));
            factory->Release();

            if (FAILED(hr))
            {
                FreeLibrary(module);
                return hr;
            }

            m_module = module;
            m_engine = engine;
            return S_OK;
        }

        HRESULT Eval(const wxString& command)
        {
            if (m_engine == NULL)
                return CO_E_NOTINITIALIZED;

            ScopedBstr value(command);
            if (!value.IsOk())
                return E_OUTOFMEMORY;

            // FestivalTTSCOM explicitly converts the Festival boolean
            // result to S_OK (TRUE) or S_FALSE (FALSE). Preserve it.
            return m_engine->raw_eval_command(value);
        }

        HRESULT EvalTrue(const wxString& command)
        {
            return Eval(wxT("(begin ") + command + wxT(" t)"));
        }

        HRESULT PrepareFestivalRuntime()
        {
            if (m_runtimePrepared)
                return S_OK;

            const wxString festivalLib =
                ResolveFestivalLibDirectory();
            if (festivalLib.IsEmpty())
                return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);

            const wxString initFile =
                festivalLib + wxT("\\init.scm");
            if (!wxFileExists(initFile))
                return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

            const wxString libPath =
                SchemeEscape(festivalLib);
            const wxString initPath =
                SchemeEscape(initFile);

            wxString xmlDtdPath = libPath;
            if (!xmlDtdPath.EndsWith(wxT("/")))
                xmlDtdPath += wxT("/");

            HRESULT hr = EvalTrue(
                wxT("(set! load-path (cons \"") +
                libPath +
                wxT("\" load-path))"));
            if (hr != S_OK)
                return hr;

            hr = EvalTrue(
                wxT("(set! xml_dtd_dir \"") +
                xmlDtdPath +
                wxT("\")"));
            if (hr != S_OK)
                return hr;

            // festival_initialize() may have used the paths compiled into
            // the old DLL. Reload init.scm from the bundled runtime so voices,
            // lexicons and audio modules are initialized from the local tree.
            hr = EvalTrue(
                wxT("(load \"") +
                initPath +
                wxT("\")"));
            if (hr != S_OK)
                return hr;

            hr = ConfigureWindowsAudio();
            if (hr != S_OK)
                return hr;

            m_runtimePrepared = true;
            return S_OK;
        }

        HRESULT ConfigureWindowsAudio()
        {
            // Festival/Speech Tools includes a native Win32 audio backend.
            // Selecting it explicitly avoids Unix/Linux defaults inherited
            // from site initialization files.
            return EvalTrue(
                wxT("(Parameter.set 'Audio_Method 'win32audio)"));
        }

        HRESULT SelectVoice(const wxString& value)
        {
            const wxString voice = SanitizeFestivalVoice(value);

            // Le funzioni voice_* possono restituire nil anche quando la
            // the voice was selected successfully. The final t value
            // forza FestivalTTSCOM a restituire S_OK.
            return EvalTrue(
                wxT("(voice_") + voice + wxT(")"));
        }

        HMODULE m_module;
        IFestivalTTSEngineLocal* m_engine;
        bool m_initialized;
        bool m_runtimePrepared;
    };
}

FestivalBridge::FestivalBridge()
    : m_eventTarget(NULL),
      m_started(false)
{
}

FestivalBridge::~FestivalBridge()
{
    Shutdown();
}

void FestivalBridge::Start(wxEvtHandler* eventTarget)
{
    if (m_started)
        return;

    m_eventTarget = eventTarget;
    m_started = true;
    m_worker = std::thread(&FestivalBridge::WorkerMain, this);
}

void FestivalBridge::Shutdown()
{
    if (!m_started)
        return;

    Command command;
    command.kind = Command_Shutdown;
    Push(command, false, true);

    if (m_worker.joinable())
        m_worker.join();

    m_started = false;
    m_eventTarget = NULL;
}

void FestivalBridge::TestConnection(const wxString& voice)
{
    Command command;
    command.kind = Command_Test;
    command.voice = voice;
    command.label = wxT("Test Festival connection");
    Push(command, true, true);
}

void FestivalBridge::PreviewPhoneme(const wxString& voice,
                                    const SingingEvent& event,
                                    double bpm)
{
    Command command;
    command.kind = Command_Phoneme;
    command.voice = voice;
    command.scheme = BuildDirectSingingScheme(
        std::vector<SingingEvent>(1, event),
        bpm);
    command.label = wxT("Phoneme preview");

    // Keep only the most recent preview request.
    Push(command, true, false);
}

void FestivalBridge::PlaySong(const SingingSong& song)
{
    Command command;
    command.kind = Command_Song;
    command.voice = song.voice;
    command.scheme = BuildDirectSingingScheme(
        song.events,
        song.bpm);
    command.label = wxT("Sequence playback");
    Push(command, true, false);
}

void FestivalBridge::RenderSong(const SingingSong& song,
                                const wxString& waveFilePath)
{
    Command command;
    command.kind = Command_Render;
    command.voice = song.voice;
    command.outputPath = waveFilePath;
    command.scheme = BuildDirectSingingRenderScheme(
        song.events,
        song.bpm,
        waveFilePath);
    command.label =
        wxT("WAV export: ") +
        wxFileName(waveFilePath).GetFullName();
    // Render commands may belong to a multi-event batch. Do not replace
    // an earlier queued render with the newest one.
    Push(command, false, false);
}

void FestivalBridge::Stop()
{
    Command command;
    command.kind = Command_Stop;
    command.label = wxT("Stop");

    AppendSingingTrace(wxT("Stop requested"));

    // Stop has priority and discards requests that have not started yet.
    Push(command, false, true);
}

void FestivalBridge::Push(const Command& command,
                          bool replaceSameKind,
                          bool highPriority)
{
    if (!m_started)
        return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (highPriority)
        {
            m_commands.clear();
            m_commands.push_front(command);
        }
        else
        {
            if (replaceSameKind)
            {
                for (std::deque<Command>::iterator it = m_commands.begin();
                     it != m_commands.end();)
                {
                    if (it->kind == command.kind)
                        it = m_commands.erase(it);
                    else
                        ++it;
                }
            }

            m_commands.push_back(command);
        }
    }

    m_condition.notify_one();
}

void FestivalBridge::PostStatus(int code,
                                const wxString& message)
{
    if (m_eventTarget == NULL)
        return;

    wxThreadEvent* event =
        new wxThreadEvent(EVT_FESTIVAL_STATUS);
    event->SetInt(code);
    event->SetString(message);
    wxQueueEvent(m_eventTarget, event);
}

void FestivalBridge::WorkerMain()
{
    FestivalComEngine engine;
    wxString details;
    HRESULT hr = engine.Initialize(&details);

    if (FAILED(hr))
    {
        PostStatus(
            FestivalStatus_Error,
            wxT("Festival initialization failed: ") + details);
    }
    else
    {
        PostStatus(FestivalStatus_Ready, details);
    }

    while (true)
    {
        Command command;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait(
                lock,
                [this]() { return !m_commands.empty(); });

            command = m_commands.front();
            m_commands.pop_front();
        }

        if (command.kind == Command_Shutdown)
            break;

        if (FAILED(hr))
        {
            PostStatus(
                FestivalStatus_Error,
                wxT("Festival is unavailable; command ignored."));
            continue;
        }

        PostStatus(
            FestivalStatus_Playing,
            command.label + wxT("..."));

        HRESULT result = S_OK;

        if (command.kind == Command_Test)
        {
            result = engine.SayText(
                command.voice,
                wxT("Festival connected successfully"));
        }
        else if (command.kind == Command_Stop)
        {
            result = engine.Stop();
        }
        else
        {
            if (command.scheme.IsEmpty())
            {
                result = E_INVALIDARG;
            }
            else
            {
                if (command.kind == Command_Render &&
                    wxFileExists(command.outputPath))
                {
                    if (!DeleteFileW(command.outputPath.wc_str()))
                    {
                        result =
                            HRESULT_FROM_WIN32(GetLastError());
                    }
                }

                if (IsStrictSuccess(result))
                {
                    result = engine.PlayDirectSinging(
                        command.voice,
                        command.scheme);
                }

                if (IsStrictSuccess(result) &&
                    command.kind == Command_Render)
                {
                    WIN32_FILE_ATTRIBUTE_DATA data;
                    std::memset(&data, 0, sizeof(data));

                    if (!GetFileAttributesExW(
                            command.outputPath.wc_str(),
                            GetFileExInfoStandard,
                            &data))
                    {
                        result =
                            HRESULT_FROM_WIN32(GetLastError());
                    }
                    else
                    {
                        ULARGE_INTEGER size;
                        size.HighPart = data.nFileSizeHigh;
                        size.LowPart = data.nFileSizeLow;

                        if (size.QuadPart <= 44)
                            result = E_FAIL;
                    }
                }
            }
        }

        if (!IsStrictSuccess(result))
        {
            wxString message =
                command.label + wxT(" failed: ") +
                HResultMessage(result);

            if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
            {
                message +=
                    wxT(" — singing-mode.scm not found ")
                    wxT("in the local runtime.");
            }

            PostStatus(FestivalStatus_Error, message);
        }
        else
        {
            if (command.kind == Command_Render)
            {
                PostStatus(
                    FestivalStatus_Ready,
                    wxT("Audio exported: ") +
                    command.outputPath);
            }
            else
            {
                PostStatus(
                    FestivalStatus_Ready,
                    command.label + wxT(" completed."));
            }
        }
    }
}
