#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "festival_bridge.h"
#include "piano_roll.h"
#include "song_model.h"
#include "sample_pool.h"
#include "slice_model.h"
#include "sample_engine.h"
#include "slicer_piano_roll.h"
#include "tone_preview.h"
#include "waveform_editor.h"
#include "auto_slicer.h"

#include <wx/wx.h>
#include <wx/accel.h>
#include <wx/artprov.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/dirdlg.h>
#include <wx/filename.h>
#include <wx/ffile.h>
#include <wx/combobox.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/spinctrl.h>
#include <wx/splitter.h>
#include <wx/statline.h>
#include <wx/sound.h>
#include <algorithm>
#include <cmath>

namespace
{
    enum
    {
        ID_MenuNew = wxID_HIGHEST + 1,
        ID_MenuOpen,
        ID_MenuSave,
        ID_MenuSaveAs,
        ID_MenuExportAudio,
        ID_MenuExit,
        ID_MenuAbout,
        ID_MenuUndo,
        ID_MenuRedo,
        ID_ApplyEvent,
        ID_TestFestival,
        ID_PreviewPhoneme,
        ID_PlaySong,
        ID_StopSong,
        ID_ExportAudio,
        ID_RenderEventsToSlicer,
        ID_AddEvent,
        ID_AddPause,
        ID_DuplicateEvent,
        ID_DeleteEvent,
        ID_EventList,
        ID_SampleList,
        ID_SampleImport,
        ID_SamplePreview,
        ID_SampleStop,
        ID_SampleRemove,
        ID_SliceCreate,
        ID_SliceDelete,
        ID_SliceList,
        ID_SliceApply,
        ID_SliceNoteOn,
        ID_SliceNoteOff,
        ID_SliceStopAll,
        ID_OpenSlicerPianoRoll,
        ID_AutoSlicePreview,
        ID_AutoSliceApply,
        ID_AutoSliceClear,
        ID_ProjectSaveFolder,
        ID_ProjectOpenFolder,
        ID_ProjectRenderWav
    };

    wxFont SectionFont()
    {
        wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        font.SetWeight(wxFONTWEIGHT_BOLD);
        font.SetPointSize(font.GetPointSize() + 1);
        return font;
    }

    wxStaticText* Caption(wxWindow* parent, const wxString& text)
    {
        wxStaticText* label = new wxStaticText(parent, wxID_ANY, text);
        label->SetForegroundColour(wxColour(75, 83, 95));
        return label;
    }

    bool NearlyEqual(double left, double right)
    {
        return std::fabs(left - right) < 0.000001;
    }

    bool EventsEqual(const SingingEvent& left,
                     const SingingEvent& right)
    {
        return left.pitch == right.pitch &&
               NearlyEqual(left.beats, right.beats) &&
               left.phoneme == right.phoneme;
    }

    struct PendingFestivalRender
    {
        wxString filePath;
        int eventIndex;
        SingingEvent event;
        wxString voice;
        double bpm;
    };

    wxString SafeFileComponent(const wxString& value)
    {
        wxString result;
        for (size_t i = 0; i < value.length(); ++i)
        {
            const wxChar c = value[i];
            if ((c >= wxT('0') && c <= wxT('9')) ||
                (c >= wxT('A') && c <= wxT('Z')) ||
                (c >= wxT('a') && c <= wxT('z')) ||
                c == wxT('-') || c == wxT('_'))
            {
                result += c;
            }
            else if (!result.EndsWith(wxT("_")))
            {
                result += wxT('_');
            }
        }
        if (result.IsEmpty())
            result = wxT("voice");
        return result.Left(32);
    }

    bool SongsEqual(const SingingSong& left,
                    const SingingSong& right)
    {
        if (left.title != right.title ||
            !NearlyEqual(left.bpm, right.bpm) ||
            left.voice != right.voice ||
            left.events.size() != right.events.size())
        {
            return false;
        }

        for (size_t i = 0; i < left.events.size(); ++i)
        {
            if (!EventsEqual(left.events[i], right.events[i]))
                return false;
        }

        return true;
    }
}

class SingModeFrame : public wxFrame
{
public:
    SingModeFrame()
        : wxFrame(NULL, wxID_ANY,
                  wxT("Festival Sing Mode Frontend — wxWidgets"),
                  wxDefaultPosition, wxSize(1320, 820),
                  wxDEFAULT_FRAME_STYLE | wxCLIP_CHILDREN),
          m_song(SingingSong::Demo()),
          m_selectedIndex(0),
          m_hasSavedSong(false),
          m_pianoRoll(NULL),
          m_eventList(NULL),
          m_voice(NULL),
          m_bpm(NULL),
          m_pitch(NULL),
          m_beats(NULL),
          m_phoneme(NULL),
          m_editPreview(NULL),
          m_status(NULL),
          m_log(NULL),
          m_sampleList(NULL),
          m_sampleDetails(NULL),
          m_waveformEditor(NULL),
          m_sliceList(NULL), m_sliceName(NULL), m_sliceStart(NULL),
          m_sliceLoopIn(NULL), m_sliceLoopOut(NULL), m_sliceEnd(NULL),
          m_sliceRootNote(NULL), m_sliceLoopEnabled(NULL),
          m_auditionNote(NULL), m_playMode(NULL), m_engineStatus(NULL),
          m_slicerPianoRollFrame(NULL)
    {
        SetMinSize(wxSize(1040, 700));
        SetBackgroundColour(wxColour(242, 244, 247));

        BuildMenu();
        BuildUi();
        CreateStatusBar(2);
        int widths[] = { -3, -1 };
        SetStatusWidths(2, widths);
        SetStatusText(wxT("Starting Festival..."), 0);
        SetStatusText(wxT("Linux / CMake / wxWidgets"), 1);

        Bind(wxEVT_MENU, &SingModeFrame::OnNewSong, this, ID_MenuNew);
        Bind(wxEVT_MENU, &SingModeFrame::OnOpenSong, this, ID_MenuOpen);
        Bind(wxEVT_MENU, &SingModeFrame::OnSaveSong, this, ID_MenuSave);
        Bind(wxEVT_MENU, &SingModeFrame::OnSaveSongAs, this, ID_MenuSaveAs);
        Bind(wxEVT_MENU, &SingModeFrame::OnExportAudio, this, ID_MenuExportAudio);
        Bind(wxEVT_MENU, &SingModeFrame::OnExit, this, ID_MenuExit);
        Bind(wxEVT_MENU, &SingModeFrame::OnAbout, this, ID_MenuAbout);
        Bind(wxEVT_MENU, &SingModeFrame::OnUndo, this, ID_MenuUndo);
        Bind(wxEVT_MENU, &SingModeFrame::OnRedo, this, ID_MenuRedo);
        Bind(wxEVT_UPDATE_UI, &SingModeFrame::OnUpdateUndo, this, ID_MenuUndo);
        Bind(wxEVT_UPDATE_UI, &SingModeFrame::OnUpdateRedo, this, ID_MenuRedo);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnApplyEvent, this, ID_ApplyEvent);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnTestFestival, this, ID_TestFestival);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnPreviewPhoneme, this, ID_PreviewPhoneme);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnPlaySong, this, ID_PlaySong);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnStopSong, this, ID_StopSong);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnExportAudio, this, ID_ExportAudio);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnRenderEventsToSlicer, this, ID_RenderEventsToSlicer);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnAddEvent, this, ID_AddEvent);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnAddPause, this, ID_AddPause);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnDuplicateEvent, this, ID_DuplicateEvent);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnDeleteEvent, this, ID_DeleteEvent);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnSampleImport, this, ID_SampleImport);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnSamplePreview, this, ID_SamplePreview);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnSampleStop, this, ID_SampleStop);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnSampleRemove, this, ID_SampleRemove);
        Bind(wxEVT_LIST_ITEM_SELECTED, &SingModeFrame::OnSampleSelection, this, ID_SampleList);
        Bind(wxEVT_LIST_ITEM_SELECTED, &SingModeFrame::OnListSelection, this, ID_EventList);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnSliceCreate, this, ID_SliceCreate);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnSliceDelete, this, ID_SliceDelete);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnSliceApply, this, ID_SliceApply);
        Bind(wxEVT_LIST_ITEM_SELECTED, &SingModeFrame::OnSliceSelection, this, ID_SliceList);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnSliceNoteOn, this, ID_SliceNoteOn);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnSliceNoteOff, this, ID_SliceNoteOff);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnSliceStopAll, this, ID_SliceStopAll);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnOpenSlicerPianoRoll, this, ID_OpenSlicerPianoRoll);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnAutoSlicePreview, this, ID_AutoSlicePreview);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnAutoSliceApply, this, ID_AutoSliceApply);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnAutoSliceClear, this, ID_AutoSliceClear);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnProjectSaveFolder, this, ID_ProjectSaveFolder);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnProjectOpenFolder, this, ID_ProjectOpenFolder);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnProjectRenderWav, this, ID_ProjectRenderWav);
        Bind(EVT_FESTIVAL_STATUS, &SingModeFrame::OnFestivalStatus, this);
        Bind(wxEVT_CLOSE_WINDOW, &SingModeFrame::OnClose, this);

        RefreshAll();
        m_festival.Start(this);
        Centre();
    }

    ~SingModeFrame()
    {
        m_sampleEngine.Shutdown();
        m_festival.Shutdown();
    }

private:
    void BuildMenu()
    {
        wxMenu* file = new wxMenu;
        file->Append(ID_MenuNew, wxT("&New	Ctrl+N"));
        file->Append(ID_MenuOpen, wxT("&Open...	Ctrl+O"));
        file->AppendSeparator();
        file->Append(ID_MenuSave, wxT("&Save	Ctrl+S"));
        file->Append(ID_MenuSaveAs,
                     wxT("Save &As...	Ctrl+Shift+S"));
        file->AppendSeparator();
        file->Append(ID_MenuExportAudio,
                     wxT("&Export WAV Audio...	Ctrl+E"));
        file->AppendSeparator();
        file->Append(ID_MenuExit, wxT("E&xit"));

        wxMenu* edit = new wxMenu;
        edit->Append(ID_MenuUndo, wxT("&Undo\tCtrl+Z"));
        edit->Append(ID_MenuRedo, wxT("&Redo\tCtrl+Y"));

        wxMenu* help = new wxMenu;
        help->Append(ID_MenuAbout, wxT("&About"));

        wxMenuBar* bar = new wxMenuBar;
        bar->Append(file, wxT("&File"));
        bar->Append(edit, wxT("&Edit"));
        bar->Append(help, wxT("&Help"));
        SetMenuBar(bar);

        // Ctrl+Shift+Z is accepted as an additional Redo shortcut.
        wxAcceleratorEntry entries[8];
        entries[0].Set(wxACCEL_CTRL, static_cast<int>('Z'), ID_MenuUndo);
        entries[1].Set(wxACCEL_CTRL, static_cast<int>('Y'), ID_MenuRedo);
        entries[2].Set(wxACCEL_CTRL | wxACCEL_SHIFT,
                       static_cast<int>('Z'), ID_MenuRedo);
        entries[3].Set(wxACCEL_CTRL, static_cast<int>('N'), ID_MenuNew);
        entries[4].Set(wxACCEL_CTRL, static_cast<int>('O'), ID_MenuOpen);
        entries[5].Set(wxACCEL_CTRL, static_cast<int>('S'), ID_MenuSave);
        entries[6].Set(wxACCEL_CTRL | wxACCEL_SHIFT,
                       static_cast<int>('S'), ID_MenuSaveAs);
        entries[7].Set(wxACCEL_CTRL,
                       static_cast<int>('E'), ID_MenuExportAudio);
        SetAcceleratorTable(wxAcceleratorTable(8, entries));
    }

    void BuildUi()
    {
        wxPanel* root = new wxPanel(this);
        root->SetBackgroundColour(wxColour(242, 244, 247));

        wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);
        wxNotebook* workspace = new wxNotebook(root, wxID_ANY);

        workspace->AddPage(BuildSingerPage(workspace),
                           wxT("Singer"), true);
        workspace->AddPage(BuildSlicerPlaceholderPage(workspace),
                           wxT("Slicer"), false);

        rootSizer->Add(workspace, 1, wxEXPAND | wxALL, 6);
        root->SetSizer(rootSizer);
    }

    wxPanel* BuildSingerPage(wxWindow* parent)
    {
        wxPanel* pagePanel = new wxPanel(parent);
        pagePanel->SetBackgroundColour(wxColour(242, 244, 247));

        wxBoxSizer* page = new wxBoxSizer(wxVERTICAL);
        page->Add(BuildProjectPanel(pagePanel), 0, wxEXPAND | wxALL, 9);

        wxSplitterWindow* outer = new wxSplitterWindow(
            pagePanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
            wxSP_LIVE_UPDATE | wxSP_3D);

        wxPanel* sequencePanel = BuildSequencePanel(outer);
        wxPanel* editorPanel = BuildEditorPanel(outer);

        outer->SplitVertically(sequencePanel, editorPanel, 930);
        outer->SetMinimumPaneSize(310);
        outer->SetSashGravity(0.73);

        page->Add(outer, 1, wxEXPAND | wxLEFT | wxRIGHT, 9);
        page->Add(BuildTransportPanel(pagePanel),
                  0, wxEXPAND | wxALL, 9);

        pagePanel->SetSizer(page);
        return pagePanel;
    }

    wxPanel* BuildSlicerPlaceholderPage(wxWindow* parent)
    {
        wxPanel* panel = new wxPanel(parent);
        panel->SetBackgroundColour(wxColour(242, 244, 247));

        wxBoxSizer* layout = new wxBoxSizer(wxVERTICAL);

        wxBoxSizer* heading = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText* title = new wxStaticText(
            panel, wxID_ANY, wxT("Sample Pool"));
        title->SetFont(SectionFont());
        heading->Add(title, 0, wxALIGN_CENTER_VERTICAL);
        heading->AddStretchSpacer();
        heading->Add(new wxButton(panel, ID_ProjectOpenFolder, wxT("Open Project Folder...")), 0, wxRIGHT, 6);
        heading->Add(new wxButton(panel, ID_ProjectSaveFolder, wxT("Save Project Folder...")), 0, wxRIGHT, 6);
        heading->Add(new wxButton(panel, ID_ProjectRenderWav, wxT("Render Slicer WAV...")), 0, wxRIGHT, 12);
        heading->Add(new wxButton(panel, ID_SampleImport, wxT("Import WAV...")),
                     0, wxRIGHT, 6);
        heading->Add(new wxButton(panel, ID_SamplePreview, wxT("Preview")),
                     0, wxRIGHT, 6);
        heading->Add(new wxButton(panel, ID_SampleStop, wxT("Stop")),
                     0, wxRIGHT, 6);
        heading->Add(new wxButton(panel, ID_SampleRemove, wxT("Remove")), 0, wxRIGHT, 12);
        heading->Add(new wxButton(panel, ID_SliceCreate, wxT("Create Slice")), 0, wxRIGHT, 6);
        heading->Add(new wxButton(panel, ID_SliceDelete, wxT("Delete Slice")), 0);
        layout->Add(heading, 0, wxEXPAND | wxALL, 10);

        wxSplitterWindow* splitter = new wxSplitterWindow(
            panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
            wxSP_LIVE_UPDATE | wxSP_3D);

        wxPanel* poolPanel = new wxPanel(splitter);
        poolPanel->SetBackgroundColour(wxColour(242, 244, 247));
        wxBoxSizer* poolLayout = new wxBoxSizer(wxVERTICAL);

        m_sampleList = new wxListCtrl(
            poolPanel, ID_SampleList, wxDefaultPosition, wxDefaultSize,
            wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_SIMPLE);
        m_sampleList->InsertColumn(0, wxT("Name"), wxLIST_FORMAT_LEFT, 270);
        m_sampleList->InsertColumn(1, wxT("ID"), wxLIST_FORMAT_LEFT, 155);
        m_sampleList->InsertColumn(2, wxT("Channels"), wxLIST_FORMAT_RIGHT, 85);
        m_sampleList->InsertColumn(3, wxT("Sample rate"), wxLIST_FORMAT_RIGHT, 110);
        m_sampleList->InsertColumn(4, wxT("Bits"), wxLIST_FORMAT_RIGHT, 65);
        m_sampleList->InsertColumn(5, wxT("Duration"), wxLIST_FORMAT_RIGHT, 95);
        poolLayout->Add(m_sampleList, 1, wxEXPAND);

        // No separate "Selected source" panel: keep the vertical space for
        // the Sample Pool list. Selection metadata remains available through
        // the list itself and the waveform below.
        poolPanel->SetSizer(poolLayout);

        m_waveformEditor = new WaveformEditorPanel(splitter);
        // wxGTK needs a substantially smaller minimum pane here.  With the
        // original Win32 values the lower waveform pane can be collapsed
        // entirely on displays with limited vertical space.
        // Keep enough height for the Sample Pool list to remain usable while
        // still reserving a visible waveform pane on wxGTK.
        splitter->SplitHorizontally(poolPanel, m_waveformEditor, 205);
        splitter->SetMinimumPaneSize(70);
        splitter->SetSashGravity(0.52);
        splitter->SetMinSize(wxSize(-1, 285));
        layout->Add(splitter, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

        wxStaticBoxSizer* sliceBox = new wxStaticBoxSizer(wxHORIZONTAL, panel, wxT("Slices"));
        m_sliceList = new wxListCtrl(panel, ID_SliceList, wxDefaultPosition, wxSize(500, 115),
                                     wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_SIMPLE);
        m_sliceList->InsertColumn(0, wxT("Name"), wxLIST_FORMAT_LEFT, 125);
        m_sliceList->InsertColumn(1, wxT("ID"), wxLIST_FORMAT_LEFT, 135);
        m_sliceList->InsertColumn(2, wxT("Start"), wxLIST_FORMAT_RIGHT, 75);
        m_sliceList->InsertColumn(3, wxT("Loop In"), wxLIST_FORMAT_RIGHT, 75);
        m_sliceList->InsertColumn(4, wxT("Loop Out"), wxLIST_FORMAT_RIGHT, 75);
        m_sliceList->InsertColumn(5, wxT("End"), wxLIST_FORMAT_RIGHT, 75);
        sliceBox->Add(m_sliceList, 1, wxEXPAND | wxALL, 6);

        wxFlexGridSizer* fields = new wxFlexGridSizer(2, 6, 6);
        fields->AddGrowableCol(1, 1);
        fields->Add(Caption(panel, wxT("Name")), 0, wxALIGN_CENTER_VERTICAL);
        m_sliceName = new wxTextCtrl(panel, wxID_ANY); fields->Add(m_sliceName, 1, wxEXPAND);
        fields->Add(Caption(panel, wxT("Start frame")), 0, wxALIGN_CENTER_VERTICAL);
        m_sliceStart = new wxTextCtrl(panel, wxID_ANY); fields->Add(m_sliceStart, 1, wxEXPAND);
        fields->Add(Caption(panel, wxT("Loop In")), 0, wxALIGN_CENTER_VERTICAL);
        m_sliceLoopIn = new wxTextCtrl(panel, wxID_ANY); fields->Add(m_sliceLoopIn, 1, wxEXPAND);
        fields->Add(Caption(panel, wxT("Loop Out")), 0, wxALIGN_CENTER_VERTICAL);
        m_sliceLoopOut = new wxTextCtrl(panel, wxID_ANY); fields->Add(m_sliceLoopOut, 1, wxEXPAND);
        fields->Add(Caption(panel, wxT("End frame")), 0, wxALIGN_CENTER_VERTICAL);
        m_sliceEnd = new wxTextCtrl(panel, wxID_ANY); fields->Add(m_sliceEnd, 1, wxEXPAND);
        fields->Add(Caption(panel, wxT("Root MIDI note")), 0, wxALIGN_CENTER_VERTICAL);
        m_sliceRootNote = new wxSpinCtrl(panel, wxID_ANY, wxT("60"), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 127, 60);
        fields->Add(m_sliceRootNote, 1, wxEXPAND);
        m_sliceLoopEnabled = new wxCheckBox(panel, wxID_ANY, wxT("Loop enabled"));
        m_sliceLoopEnabled->SetValue(true); fields->Add(m_sliceLoopEnabled, 0, wxALIGN_CENTER_VERTICAL);
        fields->Add(new wxButton(panel, ID_SliceApply, wxT("Apply Slice")), 0, wxEXPAND);
        sliceBox->Add(fields, 0, wxEXPAND | wxALL, 6);
        layout->Add(sliceBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

        wxStaticBoxSizer* autoBox = new wxStaticBoxSizer(wxHORIZONTAL, panel, wxT("Auto-slicing"));
        autoBox->Add(Caption(panel, wxT("Mode")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
        m_autoSliceMode = new wxChoice(panel, wxID_ANY);
        m_autoSliceMode->Append(wxT("Transients"));
        m_autoSliceMode->Append(wxT("Uniform"));
        m_autoSliceMode->SetSelection(0);
        autoBox->Add(m_autoSliceMode, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
        autoBox->Add(Caption(panel, wxT("Divisions")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
        m_autoSliceDivisions = new wxSpinCtrl(panel, wxID_ANY, wxT("8"), wxDefaultPosition, wxSize(70,-1), wxSP_ARROW_KEYS, 1, 128, 8);
        autoBox->Add(m_autoSliceDivisions, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
        autoBox->Add(Caption(panel, wxT("Sensitivity")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
        m_autoSliceSensitivity = new wxSpinCtrlDouble(panel, wxID_ANY, wxT("0.35"), wxDefaultPosition, wxSize(80,-1), wxSP_ARROW_KEYS, 0.01, 1.0, 0.35, 0.01);
        autoBox->Add(m_autoSliceSensitivity, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
        autoBox->Add(Caption(panel, wxT("Min gap ms")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
        m_autoSliceGapMs = new wxSpinCtrlDouble(panel, wxID_ANY, wxT("60"), wxDefaultPosition, wxSize(82,-1), wxSP_ARROW_KEYS, 1.0, 5000.0, 60.0, 1.0);
        autoBox->Add(m_autoSliceGapMs, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
        autoBox->Add(new wxButton(panel, ID_AutoSlicePreview, wxT("Preview markers")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
        autoBox->Add(new wxButton(panel, ID_AutoSliceApply, wxT("Apply slices")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
        autoBox->Add(new wxButton(panel, ID_AutoSliceClear, wxT("Clear preview")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
        m_autoSliceStatus = new wxStaticText(panel, wxID_ANY, wxT("No preview"));
        m_autoSliceStatus->SetForegroundColour(wxColour(75,83,95));
        autoBox->Add(m_autoSliceStatus, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 12);
        layout->Add(autoBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

        wxStaticBoxSizer* engineBox = new wxStaticBoxSizer(wxHORIZONTAL, panel, wxT("Slice audition — ALSA realtime engine"));
        engineBox->Add(Caption(panel, wxT("MIDI note")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
        m_auditionNote = new wxSpinCtrl(panel, wxID_ANY, wxT("60"), wxDefaultPosition, wxSize(72, -1), wxSP_ARROW_KEYS, 0, 127, 60);
        engineBox->Add(m_auditionNote, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
        engineBox->Add(Caption(panel, wxT("Mode")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 14);
        m_playMode = new wxChoice(panel, wxID_ANY);
        m_playMode->Append(wxT("Retrigger"));
        m_playMode->Append(wxT("Legato"));
        m_playMode->SetSelection(0);
        engineBox->Add(m_playMode, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
        engineBox->Add(new wxButton(panel, ID_SliceNoteOn, wxT("Note On")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 14);
        engineBox->Add(new wxButton(panel, ID_SliceNoteOff, wxT("Note Off")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
        engineBox->Add(new wxButton(panel, ID_SliceStopAll, wxT("Stop All")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
        engineBox->Add(new wxButton(panel, ID_OpenSlicerPianoRoll, wxT("Open Piano Roll...")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 14);
        m_engineStatus = new wxStaticText(panel, wxID_ANY, wxT("Engine idle"));
        m_engineStatus->SetForegroundColour(wxColour(75, 83, 95));
        engineBox->Add(m_engineStatus, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 14);
        layout->Add(engineBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

        wxStaticText* note = new wxStaticText(
            panel, wxID_ANY,
            wxT("M11: project-folder save/load, missing-file reporting and offline Slicer WAV rendering."));
        note->SetForegroundColour(wxColour(75, 83, 95));
        layout->Add(note, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 12);

        panel->SetSizer(layout);
        return panel;
    }

    wxPanel* BuildProjectPanel(wxWindow* parent)
    {
        wxPanel* panel = new wxPanel(parent);
        panel->SetBackgroundColour(*wxWHITE);

        wxStaticBoxSizer* box =
            new wxStaticBoxSizer(wxHORIZONTAL, panel, wxT("Festival"));

        box->Add(Caption(panel, wxT("Voice")),
                 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);

        m_voice = new wxComboBox(panel, wxID_ANY);
        m_voice->Append(wxT("kal_diphone"));
        m_voice->Append(wxT("cmu_us_slt_arctic_hts"));
        m_voice->Append(wxT("cmu_us_awb_arctic_hts"));
        m_voice->SetValue(m_song.voice);
        m_voice->SetMinSize(wxSize(220, -1));
        box->Add(m_voice, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);

        box->Add(Caption(panel, wxT("BPM")),
                 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 18);

        m_bpm = new wxSpinCtrlDouble(
            panel, wxID_ANY, wxString::Format(wxT("%.0f"), m_song.bpm),
            wxDefaultPosition, wxSize(82, -1),
            wxSP_ARROW_KEYS, 30.0, 300.0, m_song.bpm, 1.0);
        m_bpm->SetDigits(0);
        box->Add(m_bpm, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);

        box->AddStretchSpacer();

        wxStaticText* direct = new wxStaticText(
            panel, wxID_ANY,
            wxT("Phonemes/sequence: direct Festival playback • Editing: local tone preview"));
        direct->SetForegroundColour(wxColour(43, 105, 78));
        direct->SetFont(wxFont(9, wxFONTFAMILY_SWISS,
                               wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        box->Add(direct, 0,
                 wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 10);

        panel->SetSizer(box);
        return panel;
    }

    wxPanel* BuildSequencePanel(wxWindow* parent)
    {
        wxPanel* panel = new wxPanel(parent);
        panel->SetBackgroundColour(*wxWHITE);

        wxBoxSizer* column = new wxBoxSizer(wxVERTICAL);

        wxBoxSizer* titleRow = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText* title =
            new wxStaticText(panel, wxID_ANY, wxT("Piano roll"));
        title->SetFont(SectionFont());
        titleRow->Add(title, 0, wxALIGN_CENTER_VERTICAL);

        titleRow->AddStretchSpacer();

        wxCheckBox* gridLabel =
            new wxCheckBox(panel, wxID_ANY,
                           wxT("Play tone while dragging/resizing"));
        gridLabel->SetValue(true);
        m_editPreview = gridLabel;
        titleRow->Add(gridLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

        titleRow->Add(Caption(panel, wxT("Grid")),
                      0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

        wxChoice* snap = new wxChoice(panel, wxID_ANY);
        snap->Append(wxT("1"));
        snap->Append(wxT("1/2"));
        snap->Append(wxT("1/4"));
        snap->Append(wxT("1/8"));
        snap->Append(wxT("1/16"));
        snap->SetSelection(2);
        snap->SetToolTip(
            wxT("Snap uses increments equal to one quarter of the selected step."));
        snap->Bind(wxEVT_CHOICE, [this, snap](wxCommandEvent&)
        {
            const int selection = snap->GetSelection();
            const double values[] = { 1.0, 0.5, 0.25, 0.125, 0.0625 };
            int safeSelection = selection;
            if (safeSelection < 0)
                safeSelection = 0;
            if (safeSelection > 4)
                safeSelection = 4;

            m_pianoRoll->SetSnapBeats(values[safeSelection]);
        });
        titleRow->Add(snap, 0, wxALIGN_CENTER_VERTICAL);

        column->Add(titleRow, 0, wxEXPAND | wxALL, 9);

        wxSplitterWindow* split = new wxSplitterWindow(
            panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
            wxSP_LIVE_UPDATE | wxSP_3D);

        wxPanel* listPanel = new wxPanel(split);
        wxBoxSizer* listSizer = new wxBoxSizer(wxVERTICAL);

        m_eventList = new wxListCtrl(
            listPanel, ID_EventList, wxDefaultPosition, wxDefaultSize,
            wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_SIMPLE);
        m_eventList->AppendColumn(wxT("#"), wxLIST_FORMAT_RIGHT, 36);
        m_eventList->AppendColumn(wxT("Phoneme / rest"),
                                  wxLIST_FORMAT_LEFT, 115);
        m_eventList->AppendColumn(wxT("Pitch"), wxLIST_FORMAT_CENTER, 58);
        m_eventList->AppendColumn(wxT("Duration"), wxLIST_FORMAT_CENTER, 64);
        listSizer->Add(m_eventList, 1, wxEXPAND);

        wxBoxSizer* listButtons = new wxBoxSizer(wxHORIZONTAL);
        listButtons->Add(new wxButton(listPanel, ID_AddEvent, wxT("+ Pitch")),
                         0, wxRIGHT, 4);
        listButtons->Add(new wxButton(listPanel, ID_AddPause, wxT("+ Rest")),
                         0, wxRIGHT, 4);
        listButtons->Add(new wxButton(listPanel, ID_DuplicateEvent, wxT("Duplicate")),
                         0, wxRIGHT, 4);
        listButtons->Add(new wxButton(listPanel, ID_DeleteEvent, wxT("Delete")),
                         0);
        listSizer->Add(listButtons, 0, wxEXPAND | wxTOP, 6);
        listPanel->SetSizer(listSizer);

        m_pianoRoll = new PianoRollPanel(split);
        m_pianoRoll->SetSelectionCallback(
            [this](int index) { OnRollSelection(index); });
        m_pianoRoll->SetBeforeChangeCallback(
            [this]() { OnRollBeforeChange(); });
        m_pianoRoll->SetChangedCallback(
            [this](int index) { OnRollChanged(index); });
        m_pianoRoll->SetPreviewCallback(
            [this](const SingingEvent& event, const wxString& editKind)
            {
                OnRollPreview(event, editKind);
            });
        m_pianoRoll->SetDeleteCallback(
            [this]()
            {
                wxCommandEvent command;
                OnDeleteEvent(command);
            });

        split->SplitVertically(listPanel, m_pianoRoll, 275);
        split->SetMinimumPaneSize(200);
        split->SetSashGravity(0.0);

        column->Add(split, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 9);
        panel->SetSizer(column);
        return panel;
    }

    wxPanel* BuildEditorPanel(wxWindow* parent)
    {
        wxPanel* panel = new wxPanel(parent);
        panel->SetBackgroundColour(*wxWHITE);

        wxBoxSizer* column = new wxBoxSizer(wxVERTICAL);

        wxStaticText* title =
            new wxStaticText(panel, wxID_ANY, wxT("Selected Note"));
        title->SetFont(SectionFont());
        column->Add(title, 0, wxALL, 10);

        wxFlexGridSizer* grid = new wxFlexGridSizer(2, 8, 8);
        grid->AddGrowableCol(1, 1);

        grid->Add(Caption(panel, wxT("Pitch")),
                  0, wxALIGN_CENTER_VERTICAL);
        m_pitch = new wxComboBox(panel, wxID_ANY);
        const std::vector<wxString> pitches = BuildPitchList(36, 95);
        for (size_t i = 0; i < pitches.size(); ++i)
            m_pitch->Append(pitches[i]);
        grid->Add(m_pitch, 1, wxEXPAND);

        grid->Add(Caption(panel, wxT("Duration (beats)")),
                  0, wxALIGN_CENTER_VERTICAL);
        m_beats = new wxSpinCtrlDouble(
            panel, wxID_ANY, wxT("1"),
            wxDefaultPosition, wxDefaultSize,
            wxSP_ARROW_KEYS, 0.0625, 64.0, 1.0, 0.0625);
        m_beats->SetDigits(4);
        grid->Add(m_beats, 1, wxEXPAND);

        grid->Add(Caption(panel, wxT("Phoneme / syllable")),
                  0, wxALIGN_TOP);
        m_phoneme = new wxTextCtrl(
            panel, wxID_ANY, wxT("la"),
            wxDefaultPosition, wxSize(-1, 70),
            wxTE_MULTILINE | wxTE_RICH2);
        m_phoneme->SetToolTip(
            wxT("Leave empty to turn the event into a rest."));
        grid->Add(m_phoneme, 1, wxEXPAND);

        column->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
        column->Add(new wxButton(panel, ID_ApplyEvent,
                                 wxT("Apply Changes")),
                    0, wxEXPAND | wxALL, 10);

        column->Add(new wxStaticLine(panel), 0,
                    wxEXPAND | wxLEFT | wxRIGHT, 10);

        // Phoneme preview section: depends on selected event.
        wxStaticBoxSizer* phonemePreview =
            new wxStaticBoxSizer(wxVERTICAL, panel,
                                 wxT("Phoneme preview"));
        phonemePreview->Add(
            new wxStaticText(
                panel, wxID_ANY,
                wxT("Sings only the selected phoneme/syllable,\n")
                wxT("using the current pitch and duration. An empty field is a rest.")),
            0, wxALL, 6);
        phonemePreview->Add(
            new wxButton(panel, ID_PreviewPhoneme,
                         wxT("▶ Preview Selected Phoneme")),
            0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
        column->Add(phonemePreview, 0, wxEXPAND | wxALL, 10);

        column->AddStretchSpacer();

        wxStaticText* activity =
            new wxStaticText(panel, wxID_ANY, wxT("Festival Activity"));
        activity->SetFont(SectionFont());
        column->Add(activity, 0, wxLEFT | wxRIGHT | wxTOP, 10);

        m_log = new wxTextCtrl(
            panel, wxID_ANY, wxEmptyString,
            wxDefaultPosition, wxSize(-1, 100),
            wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
        m_log->SetBackgroundColour(wxColour(247, 248, 250));
        column->Add(m_log, 0, wxEXPAND | wxALL, 10);

        panel->SetSizer(column);
        return panel;
    }

    wxPanel* BuildTransportPanel(wxWindow* parent)
    {
        wxPanel* panel = new wxPanel(parent);
        panel->SetBackgroundColour(*wxWHITE);

        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);

        wxButton* play = new wxButton(
            panel, ID_PlaySong,
            wxT("▶ Play sequence directly with Festival"));
        wxBitmap playBitmap = wxArtProvider::GetBitmap(
            wxART_GO_FORWARD, wxART_BUTTON, wxSize(16, 16));
        if (playBitmap.IsOk())
            play->SetBitmap(playBitmap);
        row->Add(play, 0, wxALL, 7);

        wxButton* stop = new wxButton(
            panel, ID_StopSong, wxT("■ Stop"));
        wxBitmap stopBitmap = wxArtProvider::GetBitmap(
            wxART_CROSS_MARK, wxART_BUTTON, wxSize(16, 16));
        if (stopBitmap.IsOk())
            stop->SetBitmap(stopBitmap);
        row->Add(stop, 0, wxTOP | wxBOTTOM | wxRIGHT, 7);

        row->Add(new wxButton(
                     panel,
                     ID_ExportAudio,
                     wxT("Export WAV...")),
                 0, wxTOP | wxBOTTOM | wxRIGHT, 7);

        row->Add(new wxButton(
                     panel,
                     ID_RenderEventsToSlicer,
                     wxT("Render Events to Slicer...")),
                 0, wxTOP | wxBOTTOM | wxRIGHT, 7);

        row->Add(new wxButton(
                     panel,
                     ID_TestFestival,
                     wxT("Test Festival")),
                 0, wxTOP | wxBOTTOM | wxRIGHT, 7);

        row->AddStretchSpacer();

        m_status = new wxStaticText(
            panel, wxID_ANY, wxT("Festival: initializing..."));
        m_status->SetForegroundColour(wxColour(77, 84, 96));
        row->Add(m_status, 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

        panel->SetSizer(row);
        return panel;
    }

    void RefreshAll()
    {
        m_song.voice = CurrentVoice();
        m_song.bpm = m_bpm != NULL ? m_bpm->GetValue() : m_song.bpm;

        RefreshList();
        m_pianoRoll->SetSong(&m_song);
        m_pianoRoll->SetSelectedIndex(m_selectedIndex);
        LoadEditor();
        UpdateDocumentTitle();
    }

    void RefreshList()
    {
        m_eventList->Freeze();
        m_eventList->DeleteAllItems();

        for (size_t i = 0; i < m_song.events.size(); ++i)
        {
            const SingingEvent& event = m_song.events[i];
            const long row = m_eventList->InsertItem(
                static_cast<long>(i),
                wxString::Format(wxT("%d"), static_cast<int>(i + 1)));
            const bool pause = IsPauseEvent(event);
            m_eventList->SetItem(
                row, 1, pause ? wxT("(rest)") : event.phoneme);
            m_eventList->SetItem(
                row, 2, pause ? wxT("-") : event.pitch);
            m_eventList->SetItem(row, 3,
                wxString::Format(wxT("%.4g"), event.beats));
        }

        if (m_selectedIndex >= 0 &&
            m_selectedIndex < static_cast<int>(m_song.events.size()))
        {
            m_eventList->SetItemState(
                m_selectedIndex,
                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
            m_eventList->EnsureVisible(m_selectedIndex);
        }

        m_eventList->Thaw();
    }

    void LoadEditor()
    {
        const bool valid =
            m_selectedIndex >= 0 &&
            m_selectedIndex < static_cast<int>(m_song.events.size());

        m_pitch->Enable(valid);
        m_beats->Enable(valid);
        m_phoneme->Enable(valid);

        if (!valid)
        {
            m_pitch->SetValue(wxT("C4"));
            m_beats->SetValue(1.0);
            m_phoneme->SetValue(wxEmptyString);
            return;
        }

        const SingingEvent& event = m_song.events[m_selectedIndex];
        m_pitch->SetValue(event.pitch);
        m_beats->SetValue(event.beats);
        m_phoneme->SetValue(event.phoneme);
    }

    bool ApplyEditor(bool showError, bool recordHistory = true)
    {
        if (m_selectedIndex < 0 ||
            m_selectedIndex >= static_cast<int>(m_song.events.size()))
        {
            if (showError)
                wxMessageBox(wxT("Select a note first."),
                             wxT("No note selected"),
                             wxOK | wxICON_INFORMATION, this);
            return false;
        }

        SingingEvent updated = m_song.events[m_selectedIndex];
        updated.pitch = NormalizePitch(m_pitch->GetValue());
        updated.beats = m_beats->GetValue();
        if (updated.beats < 0.0625)
            updated.beats = 0.0625;

        wxString phoneme = m_phoneme->GetValue();
        phoneme.Trim(true);
        phoneme.Trim(false);
        // An empty phoneme is intentional: it represents a pause.
        updated.phoneme = phoneme;

        const wxString updatedVoice = CurrentVoice();
        const double updatedBpm = m_bpm->GetValue();

        const bool changed =
            !EventsEqual(updated, m_song.events[m_selectedIndex]) ||
            updatedVoice != m_song.voice ||
            !NearlyEqual(updatedBpm, m_song.bpm);

        if (!changed)
            return true;

        if (recordHistory)
            PushUndoState();

        ApplyEventEditPreservingFollowingTiming(
            &m_song.events,
            static_cast<size_t>(m_selectedIndex),
            updated);
        m_song.voice = updatedVoice;
        m_song.bpm = updatedBpm;

        RefreshList();
        m_pianoRoll->SetSong(&m_song);
        m_pianoRoll->SetSelectedIndex(m_selectedIndex);
        UpdateDocumentTitle();
        return true;
    }

    wxString CurrentVoice() const
    {
        return SanitizeFestivalVoice(
            m_voice != NULL ? m_voice->GetValue()
                            : m_song.voice);
    }

    struct HistoryState
    {
        SingingSong song;
        int selectedIndex;
        bool includesDocumentIdentity;
        wxString filePath;
        SingingSong savedSong;
        bool hasSavedSong;
    };

    HistoryState CaptureHistoryState(
        bool includeDocumentIdentity = false) const
    {
        HistoryState state;
        state.song = m_song;
        state.selectedIndex = m_selectedIndex;
        state.includesDocumentIdentity = includeDocumentIdentity;
        state.filePath =
            includeDocumentIdentity ? m_currentFilePath : wxString();
        state.savedSong =
            includeDocumentIdentity ? m_savedSong : SingingSong();
        state.hasSavedSong =
            includeDocumentIdentity ? m_hasSavedSong : false;
        return state;
    }

    bool HistoryStatesEqual(const HistoryState& left,
                            const HistoryState& right) const
    {
        if (left.selectedIndex != right.selectedIndex ||
            !SongsEqual(left.song, right.song) ||
            left.includesDocumentIdentity !=
                right.includesDocumentIdentity)
        {
            return false;
        }

        if (!left.includesDocumentIdentity)
            return true;

        return left.filePath == right.filePath &&
               left.hasSavedSong == right.hasSavedSong &&
               (!left.hasSavedSong ||
                SongsEqual(left.savedSong, right.savedSong));
    }

    void PushLimited(std::vector<HistoryState>& stack,
                     const HistoryState& state)
    {
        const size_t maximumHistory = 100;

        stack.push_back(state);
        if (stack.size() > maximumHistory)
            stack.erase(stack.begin());
    }

    void PushUndoState()
    {
        const HistoryState state = CaptureHistoryState();

        if (m_undoStack.empty() ||
            !HistoryStatesEqual(m_undoStack.back(), state))
        {
            PushLimited(m_undoStack, state);
        }

        m_redoStack.clear();
    }

    void RestoreHistoryState(const HistoryState& state)
    {
        m_tonePreview.Stop();

        m_song = state.song;
        m_selectedIndex = state.selectedIndex;

        if (state.includesDocumentIdentity)
        {
            m_currentFilePath = state.filePath;
            m_savedSong = state.savedSong;
            m_hasSavedSong = state.hasSavedSong;
        }

        if (m_song.events.empty())
        {
            m_selectedIndex = -1;
        }
        else
        {
            m_selectedIndex = std::max(
                0,
                std::min(m_selectedIndex,
                         static_cast<int>(m_song.events.size()) - 1));
        }

        m_voice->SetValue(m_song.voice);
        m_bpm->SetValue(m_song.bpm);

        RefreshList();
        m_pianoRoll->SetSong(&m_song);
        m_pianoRoll->SetSelectedIndex(m_selectedIndex);
        LoadEditor();
        UpdateDocumentTitle();
    }

    void OnUndo(wxCommandEvent&)
    {
        if (m_undoStack.empty())
            return;

        const HistoryState state = m_undoStack.back();
        PushLimited(
            m_redoStack,
            CaptureHistoryState(state.includesDocumentIdentity));

        m_undoStack.pop_back();
        RestoreHistoryState(state);

        AppendLog(wxT("Last change undone."));
    }

    void OnRedo(wxCommandEvent&)
    {
        if (m_redoStack.empty())
            return;

        const HistoryState state = m_redoStack.back();
        PushLimited(
            m_undoStack,
            CaptureHistoryState(state.includesDocumentIdentity));

        m_redoStack.pop_back();
        RestoreHistoryState(state);

        AppendLog(wxT("Last change restored."));
    }

    void OnUpdateUndo(wxUpdateUIEvent& event)
    {
        event.Enable(!m_undoStack.empty());
    }

    void OnUpdateRedo(wxUpdateUIEvent& event)
    {
        event.Enable(!m_redoStack.empty());
    }

    bool IsDocumentModified() const
    {
        return !m_hasSavedSong ||
               !SongsEqual(m_song, m_savedSong);
    }

    void UpdateDocumentTitle()
    {
        wxString displayName;

        if (!m_currentFilePath.IsEmpty())
        {
            displayName =
                wxFileName(m_currentFilePath).GetFullName();
        }
        else
        {
            displayName = m_song.title;
            displayName.Trim(true);
            displayName.Trim(false);
            if (displayName.IsEmpty())
                displayName = wxT("New Song");
        }

        if (IsDocumentModified())
            displayName += wxT(" *");

        SetTitle(
            displayName +
            wxT(" — Festival Sing Mode Frontend"));
    }

    wxString SuggestedFileName() const
    {
        wxString name = m_song.title;
        name.Trim(true);
        name.Trim(false);

        if (name.IsEmpty() ||
            name == wxT("New Song"))
        {
            name = wxT("song");
        }

        const wxString forbidden = wxT("<>:\"/\\|?*");
        for (size_t i = 0; i < forbidden.Length(); ++i)
            name.Replace(forbidden.Mid(i, 1), wxT("_"));

        return name + wxT(".xml");
    }

    bool SaveDocumentTo(const wxString& path)
    {
        ApplyEditor(false);

        m_song.voice = CurrentVoice();
        m_song.bpm = m_bpm->GetValue();

        if (m_song.title.IsEmpty() ||
            m_song.title == wxT("New Song"))
        {
            m_song.title = wxFileName(path).GetName();
        }

        const wxString xml =
            BuildSingingSongFileXml(m_song);

        wxFFile file(path, wxT("wb"));
        if (!file.IsOpened() ||
            !file.Write(xml, wxConvUTF8))
        {
            wxMessageBox(
                wxT("Unable to save the file:\n") + path,
                wxT("Save Error"),
                wxOK | wxICON_ERROR,
                this);
            return false;
        }

        file.Close();

        m_currentFilePath = path;
        m_savedSong = m_song;
        m_hasSavedSong = true;
        UpdateDocumentTitle();

        AppendLog(
            wxT("Song saved: ") + path);
        return true;
    }

    bool SaveCurrentDocument(bool forceSaveAs)
    {
        if (!forceSaveAs &&
            !m_currentFilePath.IsEmpty())
        {
            return SaveDocumentTo(m_currentFilePath);
        }

        wxFileDialog dialog(
            this,
            wxT("Save Singing-Mode Song"),
            wxEmptyString,
            SuggestedFileName(),
            wxT("Festival singing XML (*.xml)|*.xml|")
            wxT("All files (*.*)|*.*"),
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

        if (dialog.ShowModal() != wxID_OK)
            return false;

        wxString path = dialog.GetPath();
        if (wxFileName(path).GetExt().IsEmpty())
            path += wxT(".xml");

        return SaveDocumentTo(path);
    }

    bool ConfirmDiscardChanges()
    {
        if (!IsDocumentModified())
            return true;

        const int answer = wxMessageBox(
            wxT("The song contains unsaved changes.\n")
            wxT("Do you want to save them before continuing?"),
            wxT("Unsaved Changes"),
            wxYES_NO | wxCANCEL | wxICON_QUESTION,
            this);

        if (answer == wxCANCEL)
            return false;

        if (answer == wxYES)
            return SaveCurrentDocument(false);

        return true;
    }

    void PushDocumentUndoState()
    {
        const HistoryState state =
            CaptureHistoryState(true);

        if (m_undoStack.empty() ||
            !HistoryStatesEqual(m_undoStack.back(), state))
        {
            PushLimited(m_undoStack, state);
        }

        m_redoStack.clear();
    }

    void OnNewSong(wxCommandEvent&)
    {
        if (!ConfirmDiscardChanges())
            return;

        PushDocumentUndoState();
        m_tonePreview.Stop();
        m_festival.Stop();

        m_song = SingingSong();
        m_song.events.clear();
        m_selectedIndex = -1;
        m_currentFilePath.clear();
        m_hasSavedSong = false;

        m_voice->SetValue(m_song.voice);
        m_bpm->SetValue(m_song.bpm);
        RefreshAll();
        AppendLog(wxT("New Song."));
    }

    void OnOpenSong(wxCommandEvent&)
    {
        if (!ConfirmDiscardChanges())
            return;

        wxFileDialog dialog(
            this,
            wxT("Open Singing-Mode Song"),
            wxEmptyString,
            wxEmptyString,
            wxT("Festival singing XML (*.xml)|*.xml|")
            wxT("All files (*.*)|*.*"),
            wxFD_OPEN | wxFD_FILE_MUST_EXIST);

        if (dialog.ShowModal() != wxID_OK)
            return;

        wxFFile file(dialog.GetPath(), wxT("rb"));
        wxString xml;

        if (!file.IsOpened() ||
            !file.ReadAll(&xml, wxConvUTF8))
        {
            wxMessageBox(
                wxT("Unable to read the file:\n") +
                dialog.GetPath(),
                wxT("Open Error"),
                wxOK | wxICON_ERROR,
                this);
            return;
        }

        SingingSong loaded;
        wxString error;
        if (!ParseSingingSongFileXml(xml, &loaded, &error))
        {
            wxMessageBox(
                wxT("The file is not valid singing-mode XML.\n\n") +
                error,
                wxT("XML Error"),
                wxOK | wxICON_ERROR,
                this);
            return;
        }

        if (loaded.title.IsEmpty() ||
            loaded.title == wxT("New Song"))
        {
            loaded.title =
                wxFileName(dialog.GetPath()).GetName();
        }

        PushDocumentUndoState();
        m_tonePreview.Stop();
        m_festival.Stop();

        m_song = loaded;
        m_selectedIndex =
            m_song.events.empty() ? -1 : 0;
        m_currentFilePath = dialog.GetPath();
        m_savedSong = loaded;
        m_hasSavedSong = true;

        m_voice->SetValue(m_song.voice);
        m_bpm->SetValue(m_song.bpm);
        RefreshAll();

        AppendLog(
            wxString::Format(
                wxT("Song loaded: %s • %lu events"),
                m_currentFilePath.c_str(),
                static_cast<unsigned long>(
                    m_song.events.size())));
    }

    void OnSaveSong(wxCommandEvent&)
    {
        SaveCurrentDocument(false);
    }

    void OnSaveSongAs(wxCommandEvent&)
    {
        SaveCurrentDocument(true);
    }

    void AppendLog(const wxString& text)
    {
        if (m_log == NULL)
            return;
        m_log->AppendText(text + wxT("\n"));
        m_log->ShowPosition(m_log->GetLastPosition());
    }

    void OnRollSelection(int index)
    {
        m_selectedIndex = index;
        RefreshList();
        LoadEditor();
    }

    void OnRollBeforeChange()
    {
        PushUndoState();
    }

    void OnRollChanged(int index)
    {
        m_selectedIndex = index;
        RefreshList();
        LoadEditor();
        UpdateDocumentTitle();
        AppendLog(wxT("Sequence updated from the piano roll."));
    }

    void OnRollPreview(const SingingEvent& event,
                       const wxString& editKind)
    {
        if (m_editPreview == NULL || !m_editPreview->GetValue())
            return;

        if (IsPauseEvent(event))
        {
            m_tonePreview.Stop();
            AppendLog(
                wxT("Editing rest: ") + editKind +
                wxString::Format(
                    wxT(" • %.4g beats"), event.beats));
            return;
        }

        // Editing preview is intentionally voice-free. It is a pure sine
        // tone generated in memory and played immediately through WinMM.
        m_tonePreview.Play(
            event.pitch, event.beats, m_bpm->GetValue());

        AppendLog(wxT("Editing pitch: ") + editKind +
                  wxT(" • ") + event.pitch +
                  wxString::Format(wxT(" • %.4g beats"), event.beats));
    }

    void OnListSelection(wxListEvent& event)
    {
        m_selectedIndex = static_cast<int>(event.GetIndex());
        m_pianoRoll->SetSelectedIndex(m_selectedIndex);
        LoadEditor();
    }

    void OnApplyEvent(wxCommandEvent&)
    {
        if (ApplyEditor(true))
            AppendLog(wxT("Changes applied to the note."));
    }

    void OnTestFestival(wxCommandEvent&)
    {
        AppendLog(wxT("Festival COM connection test requested."));
        m_festival.TestConnection(CurrentVoice());
    }

    void OnPreviewPhoneme(wxCommandEvent&)
    {
        if (!ApplyEditor(true))
            return;

        const SingingEvent& event = m_song.events[m_selectedIndex];

        if (IsPauseEvent(event))
        {
            m_tonePreview.Stop();
            AppendLog(
                wxString::Format(
                    wxT("Rest preview: %.4g beats; no phoneme to sing."),
                    event.beats));
            return;
        }

        m_festival.PreviewPhoneme(
            CurrentVoice(), event, m_bpm->GetValue());
    }


    void OnPlaySong(wxCommandEvent&)
    {
        ApplyEditor(false);
        m_song.voice = CurrentVoice();
        m_song.bpm = m_bpm->GetValue();

        if (m_song.events.empty())
        {
            wxMessageBox(wxT("Add at least one note or rest."),
                         wxT("Empty Sequence"),
                         wxOK | wxICON_INFORMATION, this);
            return;
        }

        bool hasVoicedEvent = false;
        for (size_t i = 0; i < m_song.events.size(); ++i)
        {
            if (!IsPauseEvent(m_song.events[i]))
            {
                hasVoicedEvent = true;
                break;
            }
        }

        if (!hasVoicedEvent)
        {
            wxMessageBox(
                wxT("The sequence contains only rests.\n")
                wxT("Add at least one phoneme before starting Festival."),
                wxT("No Phonemes"),
                wxOK | wxICON_INFORMATION, this);
            return;
        }

        m_festival.PlaySong(m_song);
    }

    void OnStopSong(wxCommandEvent&)
    {
        m_tonePreview.Stop();
        m_festival.Stop();
        m_status->SetLabel(wxT("Festival: stopped."));
        SetStatusText(wxT("Festival: stopped."), 0);
        AppendLog(wxT("Playback stopped by user."));
    }

    void OnExportAudio(wxCommandEvent&)
    {
        ApplyEditor(false);
        m_song.voice = CurrentVoice();
        m_song.bpm = m_bpm->GetValue();

        if (m_song.events.empty())
        {
            wxMessageBox(
                wxT("Add at least one note or rest."),
                wxT("Empty Sequence"),
                wxOK | wxICON_INFORMATION,
                this);
            return;
        }

        bool hasVoicedEvent = false;
        for (size_t i = 0; i < m_song.events.size(); ++i)
        {
            if (!IsPauseEvent(m_song.events[i]))
            {
                hasVoicedEvent = true;
                break;
            }
        }

        if (!hasVoicedEvent)
        {
            wxMessageBox(
                wxT("The sequence contains only rests.\n")
                wxT("Add at least one phoneme to render."),
                wxT("No Phonemes"),
                wxOK | wxICON_INFORMATION,
                this);
            return;
        }

        wxString suggestedName;
        if (!m_currentFilePath.IsEmpty())
            suggestedName = wxFileName(m_currentFilePath).GetName();
        else
            suggestedName = wxFileName(SuggestedFileName()).GetName();

        if (suggestedName.IsEmpty())
            suggestedName = wxT("song");

        suggestedName += wxT(".wav");

        wxFileDialog dialog(
            this,
            wxT("Export WAV Audio"),
            wxEmptyString,
            suggestedName,
            wxT("WAV audio file (*.wav)|*.wav|")
            wxT("All files (*.*)|*.*"),
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

        if (dialog.ShowModal() != wxID_OK)
            return;

        wxString path = dialog.GetPath();
        if (wxFileName(path).GetExt().IsEmpty())
            path += wxT(".wav");

        m_tonePreview.Stop();
        m_festival.RenderSong(m_song, path);

        AppendLog(
            wxT("Audio rendering started: ") + path);
    }

    void OnRenderEventsToSlicer(wxCommandEvent&)
    {
        ApplyEditor(false);
        m_song.voice = CurrentVoice();
        m_song.bpm = m_bpm->GetValue();

        size_t voicedCount = 0;
        for (size_t i = 0; i < m_song.events.size(); ++i)
        {
            if (!IsPauseEvent(m_song.events[i]))
                ++voicedCount;
        }

        if (voicedCount == 0)
        {
            wxMessageBox(wxT("The sequence contains no voiced events to render."),
                         wxT("Render Events to Slicer"),
                         wxOK | wxICON_INFORMATION, this);
            return;
        }

        wxDirDialog dialog(this,
                           wxT("Choose the folder for the event WAV files"),
                           wxEmptyString,
                           wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK)
            return;

        const wxString folder = dialog.GetPath();
        const wxString batchStamp =
            wxDateTime::Now().Format(wxT("%Y%m%d-%H%M%S"));

        size_t queued = 0;
        for (size_t i = 0; i < m_song.events.size(); ++i)
        {
            const SingingEvent& source = m_song.events[i];
            if (IsPauseEvent(source))
                continue;

            const wxString fileName = wxString::Format(
                wxT("festival-%s-event-%04u-%s-%s.wav"),
                batchStamp.c_str(),
                static_cast<unsigned int>(i + 1),
                SafeFileComponent(source.pitch).c_str(),
                SafeFileComponent(source.phoneme).c_str());
            const wxString path = wxFileName(folder, fileName).GetFullPath();

            PendingFestivalRender pending;
            pending.filePath = path;
            pending.eventIndex = static_cast<int>(i);
            pending.event = source;
            pending.voice = m_song.voice;
            pending.bpm = m_song.bpm;
            m_pendingFestivalRenders.push_back(pending);

            SingingSong single;
            single.title = wxString::Format(wxT("Event %u"),
                                             static_cast<unsigned int>(i + 1));
            single.voice = m_song.voice;
            single.bpm = m_song.bpm;
            single.events.push_back(source);
            m_festival.RenderSong(single, path);
            ++queued;
        }

        AppendLog(wxString::Format(
            wxT("Queued %u Festival event WAV file(s) for the Sample Pool."),
            static_cast<unsigned int>(queued)));
    }

    void OnAddEvent(wxCommandEvent&)
    {
        PushUndoState();

        const int insertion =
            m_selectedIndex >= 0
                ? m_selectedIndex + 1
                : static_cast<int>(m_song.events.size());

        m_song.events.insert(
            m_song.events.begin() + insertion,
            SingingEvent(wxT("C4"), 1.0, wxT("la")));
        m_selectedIndex = insertion;
        RefreshAll();
    }

    void OnAddPause(wxCommandEvent&)
    {
        PushUndoState();

        const int insertion =
            m_selectedIndex >= 0
                ? m_selectedIndex + 1
                : static_cast<int>(m_song.events.size());

        const wxString pitch =
            m_selectedIndex >= 0 &&
            m_selectedIndex < static_cast<int>(m_song.events.size())
                ? m_song.events[m_selectedIndex].pitch
                : wxString(wxT("C4"));

        m_song.events.insert(
            m_song.events.begin() + insertion,
            SingingEvent(pitch, 1.0, wxEmptyString));
        m_selectedIndex = insertion;
        RefreshAll();
        AppendLog(wxT("Rest added."));
    }

    void OnDuplicateEvent(wxCommandEvent&)
    {
        if (m_selectedIndex < 0 ||
            m_selectedIndex >= static_cast<int>(m_song.events.size()))
            return;

        PushUndoState();
        ApplyEditor(false, false);
        const SingingEvent copy = m_song.events[m_selectedIndex];
        ++m_selectedIndex;
        m_song.events.insert(
            m_song.events.begin() + m_selectedIndex, copy);
        RefreshAll();
    }

    void OnDeleteEvent(wxCommandEvent&)
    {
        if (m_selectedIndex < 0 ||
            m_selectedIndex >= static_cast<int>(m_song.events.size()))
            return;

        const bool deletingPause =
            IsPauseEvent(m_song.events[m_selectedIndex]);
        const bool hasFollowingEvents =
            m_selectedIndex + 1 <
            static_cast<int>(m_song.events.size());

        PushUndoState();

        m_song.events.erase(
            m_song.events.begin() + m_selectedIndex);

        if (m_song.events.empty())
            m_selectedIndex = -1;
        else if (m_selectedIndex >=
                 static_cast<int>(m_song.events.size()))
            m_selectedIndex =
                static_cast<int>(m_song.events.size()) - 1;

        RefreshAll();

        if (deletingPause)
        {
            AppendLog(
                hasFollowingEvents
                    ? wxT("Rest deleted; subsequent events shifted to the left.")
                    : wxT("Trailing rest deleted."));
        }
    }

    long SelectedSampleIndex() const
    {
        if (m_sampleList == NULL)
            return -1;
        return m_sampleList->GetNextItem(-1, wxLIST_NEXT_ALL,
                                         wxLIST_STATE_SELECTED);
    }

    void RefreshSampleList(long selectIndex = -1)
    {
        if (m_sampleList == NULL)
            return;

        m_sampleList->DeleteAllItems();
        for (size_t i = 0; i < m_samplePool.GetCount(); ++i)
        {
            const SamplePoolItem* item = m_samplePool.GetAt(i);
            if (!item)
                continue;

            const long row = m_sampleList->InsertItem(
                static_cast<long>(i), item->displayName);
            m_sampleList->SetItem(row, 1, item->id);
            m_sampleList->SetItem(row, 2,
                wxString::Format(wxT("%u"),
                    static_cast<unsigned int>(item->wav.channelCount)));
            m_sampleList->SetItem(row, 3,
                wxString::Format(wxT("%u Hz"), item->wav.sampleRate));
            m_sampleList->SetItem(row, 4,
                wxString::Format(wxT("%u"),
                    static_cast<unsigned int>(item->wav.bitsPerSample)));
            m_sampleList->SetItem(row, 5,
                wxString::Format(wxT("%.3f s"), item->wav.durationSeconds));
        }

        if (selectIndex >= 0 &&
            selectIndex < static_cast<long>(m_samplePool.GetCount()))
        {
            m_sampleList->SetItemState(selectIndex,
                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
            m_sampleList->EnsureVisible(selectIndex);
        }
        UpdateSampleDetails();
    }

    void UpdateSampleDetails(bool updateWaveform = true)
    {
        const long selected = SelectedSampleIndex();
        const SamplePoolItem* item = selected >= 0
            ? m_samplePool.GetAt(static_cast<size_t>(selected)) : NULL;
        if (!item)
        {
            if (m_sampleDetails)
                m_sampleDetails->SetLabel(
                    wxT("No sample selected. Import a PCM or IEEE-float WAV file."));
            if (updateWaveform && m_waveformEditor)
                m_waveformEditor->ClearSample();
            return;
        }

        wxString details = wxString::Format(
            wxT("%s\n%s\n%u channel(s), %u Hz, %u-bit, %llu frames, %.3f seconds"),
            item->displayName.c_str(), item->filePath.c_str(),
            static_cast<unsigned int>(item->wav.channelCount),
            item->wav.sampleRate,
            static_cast<unsigned int>(item->wav.bitsPerSample),
            item->wav.frameCount,
            item->wav.durationSeconds);

        if (item->generatedByFestival)
        {
            details += wxString::Format(
                wxT("\nFestival event %d: %s / %s / %.4g beats / %s / %.0f BPM"),
                item->sourceEventIndex + 1,
                item->sourcePitch.c_str(),
                item->sourcePhoneme.c_str(),
                item->sourceBeats,
                item->sourceVoice.c_str(),
                item->sourceBpm);
        }
        if (m_sampleDetails)
            m_sampleDetails->SetLabel(details);

        if (updateWaveform && m_waveformEditor)
        {
            m_autoSlicePreview.clear();
            if (m_autoSliceStatus) m_autoSliceStatus->SetLabel(wxT("No preview"));
            wxString waveformError;
            if (!m_waveformEditor->SetSample(item, &waveformError))
            {
                AppendLog(wxT("Waveform could not be loaded: ") + waveformError);
                m_waveformEditor->ClearSample();
            }
        }
    }

    long SelectedSliceIndex() const
    {
        return m_sliceList ? m_sliceList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) : -1;
    }

    void RefreshSliceList(long selectIndex = -1)
    {
        if (!m_sliceList) return;
        m_sliceList->DeleteAllItems();
        const long sourceIndex = SelectedSampleIndex();
        const SamplePoolItem* source = sourceIndex >= 0 ? m_samplePool.GetAt(static_cast<size_t>(sourceIndex)) : NULL;
        long visibleRow = 0;
        long selectedRow = -1;
        for (size_t i = 0; i < m_sliceModel.GetCount(); ++i)
        {
            const AudioSlice* slice = m_sliceModel.GetAt(i);
            if (!slice || !source || slice->sourceId != source->id) continue;
            long row = m_sliceList->InsertItem(visibleRow, slice->name);
            m_sliceList->SetItem(row, 1, slice->id);
            m_sliceList->SetItem(row, 2, wxString::Format(wxT("%llu"), slice->startFrame));
            m_sliceList->SetItem(row, 3, wxString::Format(wxT("%llu"), slice->loopInFrame));
            m_sliceList->SetItem(row, 4, wxString::Format(wxT("%llu"), slice->loopOutFrame));
            m_sliceList->SetItem(row, 5, wxString::Format(wxT("%llu"), slice->endFrame));
            m_sliceList->SetItemData(row, static_cast<long>(i));
            if (static_cast<long>(i) == selectIndex) selectedRow = row;
            ++visibleRow;
        }
        if (selectedRow >= 0)
            m_sliceList->SetItemState(selectedRow, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                      wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        if (visibleRow == 0 && m_waveformEditor)
            m_waveformEditor->SetSliceMarkers(false,0,0,0,0);
    }

    AudioSlice* SelectedSlice()
    {
        const long row = SelectedSliceIndex();
        if (row < 0) return NULL;
        const long modelIndex = static_cast<long>(m_sliceList->GetItemData(row));
        return modelIndex >= 0 ? m_sliceModel.GetAt(static_cast<size_t>(modelIndex)) : NULL;
    }

    void LoadSliceControls(AudioSlice* slice)
    {
        if (!slice) return;
        m_sliceName->SetValue(slice->name);
        m_sliceStart->SetValue(wxString::Format(wxT("%llu"), slice->startFrame));
        m_sliceLoopIn->SetValue(wxString::Format(wxT("%llu"), slice->loopInFrame));
        m_sliceLoopOut->SetValue(wxString::Format(wxT("%llu"), slice->loopOutFrame));
        m_sliceEnd->SetValue(wxString::Format(wxT("%llu"), slice->endFrame));
        m_sliceRootNote->SetValue(slice->rootMidiNote);
        m_sliceLoopEnabled->SetValue(slice->loopEnabled);
        if (m_waveformEditor)
            m_waveformEditor->SetSliceMarkers(true, slice->startFrame, slice->loopInFrame, slice->loopOutFrame, slice->endFrame);
    }

    bool ParseFrame(wxTextCtrl* control, unsigned long long* value) const
    {
        return control && value && control->GetValue().ToULongLong(value);
    }

    void OnSliceCreate(wxCommandEvent&)
    {
        const long sourceIndex = SelectedSampleIndex();
        const SamplePoolItem* source = sourceIndex >= 0 ? m_samplePool.GetAt(static_cast<size_t>(sourceIndex)) : NULL;
        if (!source || !m_waveformEditor || !m_waveformEditor->HasSelection())
        {
            wxMessageBox(wxT("Select a Sample Pool source and drag a non-empty waveform selection first."),
                         wxT("Create Slice"), wxOK | wxICON_INFORMATION, this);
            return;
        }
        AudioSlice added; wxString error;
        if (!m_sliceModel.AddFromSelection(source->id, m_waveformEditor->GetSelectionStartFrame(),
                                            m_waveformEditor->GetSelectionEndFrame(), &added, &error))
        {
            wxMessageBox(error, wxT("Create Slice"), wxOK | wxICON_WARNING, this); return;
        }
        const long index = static_cast<long>(m_sliceModel.GetCount()) - 1;
        RefreshSliceList(index);
        LoadSliceControls(m_sliceModel.GetAt(static_cast<size_t>(index)));
    }

    void OnSliceDelete(wxCommandEvent&)
    {
        const long row = SelectedSliceIndex();
        if (row < 0) return;
        const size_t index = static_cast<size_t>(m_sliceList->GetItemData(row));
        m_sliceModel.RemoveAt(index);
        RefreshSliceList();
    }

    void OnSliceSelection(wxListEvent& event)
    {
        const long index = static_cast<long>(m_sliceList->GetItemData(event.GetIndex()));
        LoadSliceControls(index >= 0 ? m_sliceModel.GetAt(static_cast<size_t>(index)) : NULL);
    }

    void OnSliceApply(wxCommandEvent&)
    {
        AudioSlice* slice = SelectedSlice();
        if (!slice) return;
        unsigned long long start=0, loopIn=0, loopOut=0, end=0;
        if (!ParseFrame(m_sliceStart,&start) || !ParseFrame(m_sliceLoopIn,&loopIn) ||
            !ParseFrame(m_sliceLoopOut,&loopOut) || !ParseFrame(m_sliceEnd,&end))
        {
            wxMessageBox(wxT("All marker positions must be whole frame numbers."), wxT("Apply Slice"), wxOK | wxICON_WARNING, this); return;
        }
        const long sourceIndex = SelectedSampleIndex();
        const SamplePoolItem* source = sourceIndex >= 0 ? m_samplePool.GetAt(static_cast<size_t>(sourceIndex)) : NULL;
        if (!source || !(start <= loopIn && loopIn < loopOut && loopOut <= end && end <= source->wav.frameCount))
        {
            wxMessageBox(wxT("Required order: start <= loopIn < loopOut <= end, all within the source."),
                         wxT("Apply Slice"), wxOK | wxICON_WARNING, this); return;
        }
        slice->name = m_sliceName->GetValue().IsEmpty() ? slice->id : m_sliceName->GetValue();
        slice->startFrame=start; slice->loopInFrame=loopIn; slice->loopOutFrame=loopOut; slice->endFrame=end;
        slice->rootMidiNote=m_sliceRootNote->GetValue(); slice->loopEnabled=m_sliceLoopEnabled->GetValue();
        const long modelIndex = static_cast<long>(m_sliceList->GetItemData(SelectedSliceIndex()));
        RefreshSliceList(modelIndex); LoadSliceControls(slice);
    }

    const SamplePoolItem* FindSourceById(const wxString& sourceId) const
    {
        for (size_t i = 0; i < m_samplePool.GetCount(); ++i)
        {
            const SamplePoolItem* item = m_samplePool.GetAt(i);
            if (item && item->id == sourceId) return item;
        }
        return NULL;
    }

    bool BuildAutoSlicePreview(wxString* error)
    {
        const long selected = SelectedSampleIndex();
        const SamplePoolItem* source = selected >= 0 ? m_samplePool.GetAt(static_cast<size_t>(selected)) : NULL;
        if (!source) { if(error)*error=wxT("Select a Sample Pool source first."); return false; }
        AutoSliceSettings settings;
        settings.uniformDivisions = m_autoSliceDivisions ? m_autoSliceDivisions->GetValue() : 8;
        settings.sensitivity = m_autoSliceSensitivity ? m_autoSliceSensitivity->GetValue() : 0.35;
        settings.minimumGapMs = m_autoSliceGapMs ? m_autoSliceGapMs->GetValue() : 60.0;
        bool ok = m_autoSliceMode && m_autoSliceMode->GetSelection() == 1
            ? AutoSlicer::Uniform(*source, settings, &m_autoSlicePreview, error)
            : AutoSlicer::Transients(*source, settings, &m_autoSlicePreview, error);
        if (ok && m_waveformEditor) m_waveformEditor->SetAutoSlicePreview(m_autoSlicePreview);
        if (ok && m_autoSliceStatus) m_autoSliceStatus->SetLabel(wxString::Format(wxT("%u proposed slice(s)"), m_autoSlicePreview.size() > 1 ? static_cast<unsigned int>(m_autoSlicePreview.size()-1) : 0));
        return ok;
    }

    void OnAutoSlicePreview(wxCommandEvent&)
    {
        wxString error;
        if (!BuildAutoSlicePreview(&error)) wxMessageBox(error, wxT("Auto-slicing"), wxOK | wxICON_WARNING, this);
    }

    void OnAutoSliceApply(wxCommandEvent&)
    {
        wxString error;
        if (m_autoSlicePreview.size() < 2 && !BuildAutoSlicePreview(&error)) { wxMessageBox(error, wxT("Auto-slicing"), wxOK | wxICON_WARNING, this); return; }
        const long selected = SelectedSampleIndex();
        const SamplePoolItem* source = selected >= 0 ? m_samplePool.GetAt(static_cast<size_t>(selected)) : NULL;
        if (!source) return;
        unsigned int addedCount = 0;
        for (size_t i=1;i<m_autoSlicePreview.size();++i)
        {
            if (m_autoSlicePreview[i] <= m_autoSlicePreview[i-1]) continue;
            AudioSlice added;
            if (m_sliceModel.AddFromSelection(source->id, m_autoSlicePreview[i-1], m_autoSlicePreview[i], &added, &error)) ++addedCount;
        }
        RefreshSliceList();
        if (m_slicerPianoRollFrame) m_slicerPianoRollFrame->RefreshSlices();
        if (m_autoSliceStatus) m_autoSliceStatus->SetLabel(wxString::Format(wxT("Applied %u slice(s)"), addedCount));
        AppendLog(wxString::Format(wxT("Auto-slicing applied %u slice(s) to %s."), addedCount, source->displayName.c_str()));
    }

    void OnAutoSliceClear(wxCommandEvent&)
    {
        m_autoSlicePreview.clear();
        if (m_waveformEditor) m_waveformEditor->ClearAutoSlicePreview();
        if (m_autoSliceStatus) m_autoSliceStatus->SetLabel(wxT("No preview"));
    }


    wxString ProjectEscape(const wxString& value) const
    {
        wxString r = value;
        r.Replace(wxT("%"), wxT("%25"));
        r.Replace(wxT("|"), wxT("%7C"));
        r.Replace(wxT("\r"), wxT("%0D"));
        r.Replace(wxT("\n"), wxT("%0A"));
        return r;
    }

    wxString ProjectUnescape(const wxString& value) const
    {
        wxString r = value;
        r.Replace(wxT("%0A"), wxT("\n"));
        r.Replace(wxT("%0D"), wxT("\r"));
        r.Replace(wxT("%7C"), wxT("|"));
        r.Replace(wxT("%25"), wxT("%"));
        return r;
    }

    SlicerPianoRollFrame* EnsureSlicerPianoRoll()
    {
        if (!m_slicerPianoRollFrame)
            m_slicerPianoRollFrame = new SlicerPianoRollFrame(
                this, &m_sliceModel, &m_samplePool, &m_sampleEngine);
        m_slicerPianoRollFrame->RefreshSlices();
        return m_slicerPianoRollFrame;
    }

    bool SaveProjectFolder(const wxString& folder, wxString* error)
    {
        wxFileName::Mkdir(folder, 0777, wxPATH_MKDIR_FULL);
        const wxString audioFolder = wxFileName(folder, wxT("audio")).GetFullPath();
        wxFileName::Mkdir(audioFolder, 0777, wxPATH_MKDIR_FULL);

        ApplyEditor(false);
        m_song.voice = CurrentVoice();
        m_song.bpm = m_bpm->GetValue();
        wxFFile singer(wxFileName(folder, wxT("singer.xml")).GetFullPath(), wxT("wb"));
        if (!singer.IsOpened() || !singer.Write(BuildSingingSongFileXml(m_song), wxConvUTF8))
        { if(error)*error=wxT("Unable to write singer.xml."); return false; }
        singer.Close();

        SlicerPianoRollFrame* roll = EnsureSlicerPianoRoll();
        wxString manifest = wxT("FVSPROJECT|1\n");
        manifest += wxString::Format(wxT("BPM|%.10g\n"), roll->GetBpm());
        for (size_t i=0;i<m_samplePool.GetCount();++i)
        {
            const SamplePoolItem* item=m_samplePool.GetAt(i); if(!item)continue;
            const wxString fileName=item->id+wxT(".wav");
            const wxString dest=wxFileName(audioFolder,fileName).GetFullPath();
            wxFileName sourceName(item->filePath);
            wxFileName destinationName(dest);
            sourceName.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
            destinationName.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
            if (sourceName.GetFullPath().CmpNoCase(destinationName.GetFullPath()) != 0)
            {
                if (!wxCopyFile(item->filePath, dest, true))
                { if(error)*error=wxT("Unable to copy source WAV: ")+item->filePath; return false; }
            }
            manifest += wxT("SOURCE|")+ProjectEscape(item->id)+wxT("|")+ProjectEscape(fileName)+wxT("|")+ProjectEscape(item->displayName)+wxT("|")+
                (item->generatedByFestival?wxT("1"):wxT("0"))+wxT("|")+wxString::Format(wxT("%d|%.10g|%.10g|"),item->sourceEventIndex,item->sourceBeats,item->sourceBpm)+
                ProjectEscape(item->sourcePitch)+wxT("|")+ProjectEscape(item->sourcePhoneme)+wxT("|")+ProjectEscape(item->sourceVoice)+wxT("\n");
        }
        const std::vector<AudioSlice>& slices=m_sliceModel.Items();
        for(size_t i=0;i<slices.size();++i){const AudioSlice& x=slices[i];manifest+=wxT("SLICE|")+ProjectEscape(x.id)+wxT("|")+ProjectEscape(x.sourceId)+wxT("|")+ProjectEscape(x.name)+wxT("|")+wxString::Format(wxT("%llu|%llu|%llu|%llu|%d|%d\n"),x.startFrame,x.loopInFrame,x.loopOutFrame,x.endFrame,x.rootMidiNote,x.loopEnabled?1:0);}
        const std::vector<SliceRollEvent>& events=roll->Events();
        for(size_t i=0;i<events.size();++i){const SliceRollEvent& e=events[i];manifest+=wxT("EVENT|")+ProjectEscape(e.id)+wxT("|")+ProjectEscape(e.sliceId)+wxT("|")+wxString::Format(wxT("%d|%ld|%ld|%d\n"),e.midiNote,e.startTick,e.durationTicks,e.velocity);}
        wxFFile out(wxFileName(folder,wxT("project.fvsp")).GetFullPath(),wxT("wb"));
        if(!out.IsOpened()||!out.Write(manifest,wxConvUTF8)){if(error)*error=wxT("Unable to write project.fvsp.");return false;} out.Close();
        return true;
    }

    bool LoadProjectFolder(const wxString& folder, wxString* error, wxString* warnings)
    {
        wxString text; wxFFile in(wxFileName(folder,wxT("project.fvsp")).GetFullPath(),wxT("rb"));
        if(!in.IsOpened()||!in.ReadAll(&text,wxConvUTF8)){if(error)*error=wxT("project.fvsp was not found or cannot be read.");return false;}
        SingingSong loadedSong; wxString singerXml; wxFFile sf(wxFileName(folder,wxT("singer.xml")).GetFullPath(),wxT("rb"));
        if(sf.IsOpened()&&sf.ReadAll(&singerXml,wxConvUTF8)){wxString parseError;if(!ParseSingingSongFileXml(singerXml,&loadedSong,&parseError)){if(error)*error=wxT("Invalid singer.xml: ")+parseError;return false;}}
        else loadedSong=m_song;

        m_sampleEngine.StopAll(); m_samplePool.Clear(); m_sliceModel.Items().clear(); std::vector<SliceRollEvent> events; double projectBpm=120.0;
        wxArrayString lines=wxSplit(text,wxT('\n'));
        for(size_t li=0;li<lines.GetCount();++li)
        {
            wxArrayString f=wxSplit(lines[li],wxT('|')); if(f.GetCount()==0)continue;
            if(f[0]==wxT("BPM")&&f.GetCount()>1)f[1].ToDouble(&projectBpm);
            else if(f[0]==wxT("SOURCE")&&f.GetCount()>=11)
            {
                const wxString id=ProjectUnescape(f[1]), path=wxFileName(wxFileName(folder,wxT("audio")).GetFullPath(),ProjectUnescape(f[2])).GetFullPath();
                if(!wxFileExists(path)){if(warnings)*warnings+=wxT("Missing WAV: ")+path+wxT("\n");continue;}
                SamplePoolItem added; wxString addError; if(!m_samplePool.AddWav(path,&added,&addError)){if(warnings)*warnings+=addError+wxT("\n");continue;}
                SamplePoolItem* item=m_samplePool.GetAtMutable(m_samplePool.GetCount()-1); item->id=id; item->displayName=ProjectUnescape(f[3]); item->generatedByFestival=f[4]==wxT("1");
                long idx=-1;f[5].ToLong(&idx);item->sourceEventIndex=static_cast<int>(idx);f[6].ToDouble(&item->sourceBeats);f[7].ToDouble(&item->sourceBpm);item->sourcePitch=ProjectUnescape(f[8]);item->sourcePhoneme=ProjectUnescape(f[9]);item->sourceVoice=ProjectUnescape(f[10]);
            }
            else if(f[0]==wxT("SLICE")&&f.GetCount()>=10)
            {
                AudioSlice x;x.id=ProjectUnescape(f[1]);x.sourceId=ProjectUnescape(f[2]);x.name=ProjectUnescape(f[3]);wxULongLong_t a=0,b=0,c=0,d=0;f[4].ToULongLong(&a);f[5].ToULongLong(&b);f[6].ToULongLong(&c);f[7].ToULongLong(&d);x.startFrame=a;x.loopInFrame=b;x.loopOutFrame=c;x.endFrame=d;long n=60,l=1;f[8].ToLong(&n);f[9].ToLong(&l);x.rootMidiNote=static_cast<int>(n);x.loopEnabled=l!=0;m_sliceModel.Items().push_back(x);
            }
            else if(f[0]==wxT("EVENT")&&f.GetCount()>=7)
            {
                SliceRollEvent e;e.id=ProjectUnescape(f[1]);e.sliceId=ProjectUnescape(f[2]);long n=60,v=100;f[3].ToLong(&n);f[4].ToLong(&e.startTick);f[5].ToLong(&e.durationTicks);f[6].ToLong(&v);e.midiNote=static_cast<int>(n);e.velocity=static_cast<int>(v);events.push_back(e);
            }
        }
        m_song=loadedSong;m_voice->SetValue(m_song.voice);m_bpm->SetValue(m_song.bpm);m_selectedIndex=m_song.events.empty()?-1:0;
        SlicerPianoRollFrame* roll=EnsureSlicerPianoRoll();roll->SetEvents(events);roll->SetBpm(projectBpm);
        RefreshAll();RefreshSampleList(m_samplePool.GetCount()?0:-1);RefreshSliceList();roll->RefreshSlices();return true;
    }

    void OnProjectSaveFolder(wxCommandEvent&)
    {
        wxDirDialog d(this, wxT("Choose or create the FestivalVirtualSinger project folder"));
        if (d.ShowModal() != wxID_OK)
            return;
        wxString error;
        if (!SaveProjectFolder(d.GetPath(), &error))
            wxMessageBox(error, wxT("Save Project"), wxOK | wxICON_ERROR, this);
        else
            wxMessageBox(wxT("Project folder saved successfully."),
                         wxT("Save Project"), wxOK | wxICON_INFORMATION, this);
    }

    void OnProjectOpenFolder(wxCommandEvent&)
    {
        wxDirDialog d(this,wxT("Open FestivalVirtualSinger project folder")); if(d.ShowModal()!=wxID_OK)return;
        wxString error,warnings;if(!LoadProjectFolder(d.GetPath(),&error,&warnings))wxMessageBox(error,wxT("Open Project"),wxOK|wxICON_ERROR,this);else if(!warnings.empty())wxMessageBox(wxT("Project opened with warnings:\n\n")+warnings,wxT("Missing files"),wxOK|wxICON_WARNING,this);
    }

    void OnProjectRenderWav(wxCommandEvent&)
    {
        SlicerPianoRollFrame* roll=EnsureSlicerPianoRoll(); wxFileDialog d(this,wxT("Render Slicer Piano Roll"),wxEmptyString,wxT("slicer_mix.wav"),wxT("WAV audio (*.wav)|*.wav"),wxFD_SAVE|wxFD_OVERWRITE_PROMPT);if(d.ShowModal()!=wxID_OK)return;
        wxString path=d.GetPath();if(wxFileName(path).GetExt().IsEmpty())path+=wxT(".wav");wxString error;if(!roll->ExportWav(path,&error))wxMessageBox(error,wxT("Render Slicer WAV"),wxOK|wxICON_ERROR,this);else wxMessageBox(wxT("Slicer WAV rendered successfully."),wxT("Render Slicer WAV"),wxOK|wxICON_INFORMATION,this);
    }

    void OnSliceNoteOn(wxCommandEvent&)
    {
        AudioSlice* slice = SelectedSlice();
        if (!slice)
        {
            wxMessageBox(wxT("Select a slice first."), wxT("Slice audition"),
                         wxOK | wxICON_INFORMATION, this);
            return;
        }
        const SamplePoolItem* source = FindSourceById(slice->sourceId);
        if (!source)
        {
            wxMessageBox(wxT("The source WAV for this slice is unavailable."),
                         wxT("Slice audition"), wxOK | wxICON_WARNING, this);
            return;
        }
        wxString error;
        const SampleEngine::PlayMode mode =
            m_playMode->GetSelection() == 1 ? SampleEngine::ModeLegato : SampleEngine::ModeRetrigger;
        if (!m_sampleEngine.NoteOn(*source, *slice, m_auditionNote->GetValue(), mode, &error))
        {
            wxMessageBox(error, wxT("Audio engine"), wxOK | wxICON_ERROR, this);
            return;
        }
        m_engineStatus->SetLabel(wxString::Format(
            wxT("Playing %s at MIDI %d (%s)"), slice->name.c_str(),
            m_auditionNote->GetValue(),
            mode == SampleEngine::ModeLegato ? wxT("Legato") : wxT("Retrigger")));
    }

    void OnSliceNoteOff(wxCommandEvent&)
    {
        m_sampleEngine.NoteOff();
        m_engineStatus->SetLabel(wxT("Note Off requested"));
    }

    void OnSliceStopAll(wxCommandEvent&)
    {
        m_sampleEngine.StopAll();
        m_engineStatus->SetLabel(wxT("Engine stopped"));
    }

    void OnOpenSlicerPianoRoll(wxCommandEvent&)
    {
        EnsureSlicerPianoRoll();
        m_slicerPianoRollFrame->RefreshSlices();
        m_slicerPianoRollFrame->Show(true);
        m_slicerPianoRollFrame->Raise();
    }

    void OnSampleImport(wxCommandEvent&)
    {
        wxFileDialog dialog(this, wxT("Import WAV into Sample Pool"),
                            wxEmptyString, wxEmptyString,
                            wxT("WAV audio (*.wav)|*.wav"),
                            wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
        if (dialog.ShowModal() != wxID_OK)
            return;

        wxArrayString paths;
        dialog.GetPaths(paths);
        long lastAdded = -1;
        wxString failures;

        for (size_t i = 0; i < paths.GetCount(); ++i)
        {
            SamplePoolItem added;
            wxString error;
            if (m_samplePool.AddWav(paths[i], &added, &error))
                lastAdded = static_cast<long>(m_samplePool.GetCount()) - 1;
            else
                failures += wxString::Format(wxT("%s: %s\n"),
                                             paths[i].c_str(), error.c_str());
        }

        RefreshSampleList(lastAdded);
        if (!failures.empty())
            wxMessageBox(failures, wxT("Some files were not imported"),
                         wxOK | wxICON_WARNING, this);
    }

    void OnSamplePreview(wxCommandEvent&)
    {
        const long selected = SelectedSampleIndex();
        const SamplePoolItem* item = selected >= 0
            ? m_samplePool.GetAt(static_cast<size_t>(selected)) : NULL;
        if (!item)
        {
            wxMessageBox(wxT("Select a sample first."), wxT("Sample Pool"),
                         wxOK | wxICON_INFORMATION, this);
            return;
        }

        // Preview the complete WAV through the same ALSA-backed SampleEngine
        // used by slice audition and the Slicer Piano Roll.
        AudioSlice previewSlice;
        previewSlice.id = wxT("__sample_preview__");
        previewSlice.sourceId = item->id;
        previewSlice.name = item->displayName;
        previewSlice.startFrame = 0;
        previewSlice.loopInFrame = 0;
        previewSlice.loopOutFrame = item->wav.frameCount;
        previewSlice.endFrame = item->wav.frameCount;
        previewSlice.rootMidiNote = 60;
        previewSlice.loopEnabled = false;

        m_sampleEngine.StopAll();

        wxString error;
        if (!m_sampleEngine.NoteOn(
                *item,
                previewSlice,
                60,
                SampleEngine::ModeRetrigger,
                &error))
        {
            wxMessageBox(error,
                         wxT("Sample preview"),
                         wxOK | wxICON_ERROR,
                         this);
            return;
        }

        if (m_waveformEditor)
            m_waveformEditor->StartPlayback();
    }

    void OnSampleStop(wxCommandEvent&)
    {
        m_sampleEngine.StopAll();
        if (m_waveformEditor)
            m_waveformEditor->StopPlayback();
    }

    void OnSampleRemove(wxCommandEvent&)
    {
        const long selected = SelectedSampleIndex();
        if (selected < 0)
            return;

        m_sampleEngine.StopAll();
        if (m_waveformEditor)
            m_waveformEditor->StopPlayback();
        m_samplePool.RemoveAt(static_cast<size_t>(selected));
        const long next = std::min(selected,
            static_cast<long>(m_samplePool.GetCount()) - 1);
        RefreshSampleList(next);
    }

    void OnSampleSelection(wxListEvent& event)
    {
        // Keep Singer and Sample Pool selection events isolated.  The event
        // index is authoritative here; querying the list too early can still
        // return the previously selected row on some wxWidgets/Win32 builds.
        const long selected = event.GetIndex();
        if (selected < 0 ||
            selected >= static_cast<long>(m_samplePool.GetCount()))
        {
            UpdateSampleDetails();
            return;
        }

        m_sampleEngine.StopAll();
        if (m_waveformEditor)
        {
            m_waveformEditor->StopPlayback();
            const SamplePoolItem* item =
                m_samplePool.GetAt(static_cast<size_t>(selected));
            wxString waveformError;
            if (!item || !m_waveformEditor->SetSample(item, &waveformError))
            {
                if (!waveformError.empty())
                    AppendLog(wxT("Waveform could not be loaded: ") + waveformError);
                m_waveformEditor->ClearSample();
            }
        }

        UpdateSampleDetails(false);
        RefreshSliceList();
    }

    void OnFestivalStatus(wxThreadEvent& event)
    {
        const wxString message = event.GetString();
        const int code = event.GetInt();

        m_status->SetLabel(message);
        SetStatusText(message, 0);
        AppendLog(message);

        const wxString exportPrefix = wxT("Audio exported: ");
        if (code == FestivalStatus_Ready && message.StartsWith(exportPrefix))
        {
            const wxString exportedPath = message.Mid(exportPrefix.length());
            for (std::vector<PendingFestivalRender>::iterator it =
                     m_pendingFestivalRenders.begin();
                 it != m_pendingFestivalRenders.end(); ++it)
            {
                if (it->filePath.CmpNoCase(exportedPath) != 0)
                    continue;

                SamplePoolItem added;
                wxString error;
                if (m_samplePool.AddFestivalWav(
                        exportedPath, it->eventIndex, it->event,
                        it->voice, it->bpm, &added, &error))
                {
                    RefreshSampleList(
                        static_cast<long>(m_samplePool.GetCount()) - 1);
                    AppendLog(wxT("Festival event imported into Sample Pool: ") +
                              added.displayName);
                }
                else
                {
                    AppendLog(wxT("Festival event WAV was rendered but could not be imported: ") +
                              error);
                }
                m_pendingFestivalRenders.erase(it);
                break;
            }
        }

        if (code == FestivalStatus_Error)
            m_status->SetForegroundColour(wxColour(184, 47, 47));
        else if (code == FestivalStatus_Playing)
            m_status->SetForegroundColour(wxColour(168, 103, 14));
        else
            m_status->SetForegroundColour(wxColour(43, 112, 78));

        m_status->Refresh();
    }

    void OnExit(wxCommandEvent&)
    {
        Close(true);
    }

    void OnClose(wxCloseEvent& event)
    {
        if (!ConfirmDiscardChanges())
        {
            event.Veto();
            return;
        }

        m_tonePreview.Stop();
        m_sampleEngine.StopAll();
        m_sampleEngine.Shutdown();
        m_sampleEngine.StopAll();
        m_festival.Shutdown();
        event.Skip();
    }

    void OnAbout(wxCommandEvent&)
    {
        wxMessageBox(
            wxT("Festival Sing Mode Frontend — wxWidgets\n\n")
            wxT("Reimplementation of the Festival singing-mode frontend.\n")
            wxT("The editing tone, phoneme, and voice timbre use separate paths.\n")
            wxT("Phonemes and sequences use FestivalTTSCOM directly;\n")
            wxT("no WAV file is created for an external player.\n\n")
            wxT("Conceptually derived from Festival-sing-mode-frontend\n")
            wxT("(GNU GPL v3)."),
            wxT("About"),
            wxOK | wxICON_INFORMATION,
            this);
    }

    SingingSong m_song;
    int m_selectedIndex;
    wxString m_currentFilePath;
    SingingSong m_savedSong;
    bool m_hasSavedSong;
    std::vector<HistoryState> m_undoStack;
    std::vector<HistoryState> m_redoStack;

    PianoRollPanel* m_pianoRoll;
    wxListCtrl* m_eventList;
    wxComboBox* m_voice;
    wxSpinCtrlDouble* m_bpm;
    wxComboBox* m_pitch;
    wxSpinCtrlDouble* m_beats;
    wxTextCtrl* m_phoneme;
    wxCheckBox* m_editPreview;
    wxStaticText* m_status;
    wxTextCtrl* m_log;
    wxListCtrl* m_sampleList;
    wxStaticText* m_sampleDetails;
    WaveformEditorPanel* m_waveformEditor;
    wxListCtrl* m_sliceList;
    wxTextCtrl* m_sliceName;
    wxTextCtrl* m_sliceStart;
    wxTextCtrl* m_sliceLoopIn;
    wxTextCtrl* m_sliceLoopOut;
    wxTextCtrl* m_sliceEnd;
    wxSpinCtrl* m_sliceRootNote;
    wxCheckBox* m_sliceLoopEnabled;
    wxSpinCtrl* m_auditionNote;
    wxChoice* m_playMode;
    wxStaticText* m_engineStatus;
    wxChoice* m_autoSliceMode;
    wxSpinCtrl* m_autoSliceDivisions;
    wxSpinCtrlDouble* m_autoSliceSensitivity;
    wxSpinCtrlDouble* m_autoSliceGapMs;
    wxStaticText* m_autoSliceStatus;
    std::vector<unsigned long long> m_autoSlicePreview;
    SamplePool m_samplePool;
    SliceModel m_sliceModel;
    SampleEngine m_sampleEngine;
    SlicerPianoRollFrame* m_slicerPianoRollFrame;
    std::vector<PendingFestivalRender> m_pendingFestivalRenders;

    TonePreview m_tonePreview;
    FestivalBridge m_festival;
};

class SingModeApp : public wxApp
{
public:
    virtual bool OnInit()
    {
        if (!wxApp::OnInit())
            return false;

        SetAppName(wxT("FestivalSingModeWx"));
        SingModeFrame* frame = new SingModeFrame;
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(SingModeApp);
