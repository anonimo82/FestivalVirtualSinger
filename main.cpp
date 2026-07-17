#include "festival_bridge.h"
#include "piano_roll.h"
#include "song_model.h"
#include "tone_preview.h"

#include <wx/wx.h>
#include <wx/accel.h>
#include <wx/artprov.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/combobox.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/spinctrl.h>
#include <wx/splitter.h>
#include <wx/statline.h>
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
        ID_ExportAudio,
        ID_AddEvent,
        ID_AddPause,
        ID_DuplicateEvent,
        ID_DeleteEvent
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
          m_log(NULL)
    {
        SetMinSize(wxSize(1040, 700));
        SetBackgroundColour(wxColour(242, 244, 247));

        BuildMenu();
        BuildUi();
        CreateStatusBar(2);
        int widths[] = { -3, -1 };
        SetStatusWidths(2, widths);
        SetStatusText(wxT("Starting Festival..."), 0);
        SetStatusText(wxT("Win32 / v120 / wxWidgets 3.2.11"), 1);

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
        Bind(wxEVT_BUTTON, &SingModeFrame::OnExportAudio, this, ID_ExportAudio);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnAddEvent, this, ID_AddEvent);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnAddPause, this, ID_AddPause);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnDuplicateEvent, this, ID_DuplicateEvent);
        Bind(wxEVT_BUTTON, &SingModeFrame::OnDeleteEvent, this, ID_DeleteEvent);
        Bind(wxEVT_LIST_ITEM_SELECTED, &SingModeFrame::OnListSelection, this);
        Bind(EVT_FESTIVAL_STATUS, &SingModeFrame::OnFestivalStatus, this);
        Bind(wxEVT_CLOSE_WINDOW, &SingModeFrame::OnClose, this);

        RefreshAll();
        m_festival.Start(this);
        Centre();
    }

    ~SingModeFrame()
    {
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

        wxBoxSizer* page = new wxBoxSizer(wxVERTICAL);
        page->Add(BuildProjectPanel(root), 0, wxEXPAND | wxALL, 9);

        wxSplitterWindow* outer = new wxSplitterWindow(
            root, wxID_ANY, wxDefaultPosition, wxDefaultSize,
            wxSP_LIVE_UPDATE | wxSP_3D);

        wxPanel* sequencePanel = BuildSequencePanel(outer);
        wxPanel* editorPanel = BuildEditorPanel(outer);

        outer->SplitVertically(sequencePanel, editorPanel, 930);
        outer->SetMinimumPaneSize(310);
        outer->SetSashGravity(0.73);

        page->Add(outer, 1, wxEXPAND | wxLEFT | wxRIGHT, 9);
        page->Add(BuildTransportPanel(root), 0, wxEXPAND | wxALL, 9);

        root->SetSizer(page);
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
            listPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
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

        row->Add(new wxButton(
                     panel,
                     ID_ExportAudio,
                     wxT("Export WAV...")),
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
            includeDocumentIdentity ? m_currentFilePath : wxEmptyString;
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

    void OnFestivalStatus(wxThreadEvent& event)
    {
        const wxString message = event.GetString();
        const int code = event.GetInt();

        m_status->SetLabel(message);
        SetStatusText(message, 0);
        AppendLog(message);

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
