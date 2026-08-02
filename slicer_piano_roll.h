#ifndef FESTIVAL_VIRTUAL_SINGER_SLICER_PIANO_ROLL_H
#define FESTIVAL_VIRTUAL_SINGER_SLICER_PIANO_ROLL_H

#include "slice_model.h"
#include "sample_pool.h"
#include "event_timing.h"
#include "sample_engine.h"
#include <wx/wx.h>
#include <wx/spinctrl.h>
#include <wx/choice.h>
#include <wx/stopwatch.h>
#include <wx/scrolwin.h>
#include <vector>

struct SliceRollEvent
{
    wxString id;
    wxString sliceId;
    int midiNote;
    long startTick;
    long durationTicks;
    int velocity;
    bool selected;

    SliceRollEvent()
        : midiNote(60), startTick(0), durationTicks(480), velocity(100), selected(false) {}
};

class SlicerPianoRollFrame;

class SlicerPianoRollCanvas : public wxScrolledWindow
{
public:
    SlicerPianoRollCanvas(wxWindow* parent, std::vector<SliceRollEvent>* events, SliceModel* slices, SamplePool* samples, double* bpm);

    void SetSnapTicks(long ticks);
    long GetPrimarySelection() const;
    void ClearSelection();
    void RefreshView();
    void SetDurationMode(bool loopSnap);
    void SetPlayheadTick(long tick);
    void CancelInteraction();

private:
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMotion(wxMouseEvent& event);
    void OnEraseBackground(wxEraseEvent& event);

    int EventAt(const wxPoint& point) const;
    wxRect EventRect(const SliceRollEvent& event) const;
    long Snap(long value) const;

    const AudioSlice* FindSlice(const wxString& id) const;
    unsigned int FindSampleRate(const wxString& sourceId) const;

    std::vector<SliceRollEvent>* m_events;
    SliceModel* m_slices;
    SamplePool* m_samples;
    double* m_bpm;
    bool m_loopSnap;
    long m_snapTicks;
    int m_dragIndex;
    wxPoint m_dragStart;
    long m_originalStart;
    int m_originalNote;
    bool m_dragging;
    bool m_marqueeSelecting;
    wxPoint m_marqueeStart;
    wxPoint m_marqueeCurrent;
    std::vector<bool> m_marqueeBaseSelection;
    std::vector<long> m_dragOriginalStarts;
    std::vector<int> m_dragOriginalNotes;
    long m_playheadTick;

    wxDECLARE_EVENT_TABLE();
};

class SlicerPianoRollFrame : public wxFrame
{
public:
    SlicerPianoRollFrame(wxWindow* parent, SliceModel* slices, SamplePool* samples, SampleEngine* engine);
    void RefreshSlices();
    const std::vector<SliceRollEvent>& Events() const { return m_events; }
    void SetEvents(const std::vector<SliceRollEvent>& events);
    double GetBpm() const { return m_bpm; }
    void SetBpm(double bpm);
    bool ExportWav(const wxString& path, wxString* errorMessage) const;
    void BeginCanvasEdit();
    void EndCanvasEdit();

private:
    enum
    {
        ID_Add = wxID_HIGHEST + 700,
        ID_Delete,
        ID_Duplicate,
        ID_Copy,
        ID_Cut,
        ID_Paste,
        ID_Undo,
        ID_Redo,
        ID_Apply,
        ID_SelectAll,
        ID_ClearSelection,
        ID_Snap,
        ID_Bpm,
        ID_DurationMode,
        ID_Play,
        ID_Stop,
        ID_TransportTimer,
        ID_Canvas
    };

    void OnAdd(wxCommandEvent& event);
    void OnDelete(wxCommandEvent& event);
    void OnDuplicate(wxCommandEvent& event);
    void OnCopy(wxCommandEvent& event);
    void OnCut(wxCommandEvent& event);
    void OnPaste(wxCommandEvent& event);
    void OnUndo(wxCommandEvent& event);
    void OnRedo(wxCommandEvent& event);
    void OnApply(wxCommandEvent& event);
    void OnSelectAll(wxCommandEvent& event);
    void OnClearSelection(wxCommandEvent& event);
    void OnSnap(wxCommandEvent& event);
    void OnBpm(wxSpinDoubleEvent& event);
    void OnDurationMode(wxCommandEvent& event);
    void OnPlay(wxCommandEvent& event);
    void OnStop(wxCommandEvent& event);
    void OnTransportTimer(wxTimerEvent& event);
    void OnCanvasClick(wxMouseEvent& event);
    void OnClose(wxCloseEvent& event);

    void RefreshInspector();
    void SelectEvent(long index);
    long PrimarySelection() const;
    long CurrentSnapTicks() const;
    wxString MakeEventId();
    void PushUndoState();
    void RestoreState(const std::vector<SliceRollEvent>& state);
    static bool EventStatesEqual(const std::vector<SliceRollEvent>& a, const std::vector<SliceRollEvent>& b);

    const AudioSlice* FindSlice(const wxString& id) const;
    unsigned int FindSampleRate(const wxString& sourceId) const;
    const SamplePoolItem* FindSource(const wxString& sourceId) const;
    wxString TimingSummary(const SliceRollEvent& event) const;

    SliceModel* m_slices;
    SamplePool* m_samples;
    SampleEngine* m_engine;
    std::vector<SliceRollEvent> m_events;
    std::vector<SliceRollEvent> m_clipboard;
    std::vector< std::vector<SliceRollEvent> > m_undoStack;
    std::vector< std::vector<SliceRollEvent> > m_redoStack;
    std::vector<SliceRollEvent> m_pendingCanvasState;
    bool m_canvasEditPending;
    SlicerPianoRollCanvas* m_canvas;
    wxChoice* m_sliceChoice;
    wxChoice* m_snapChoice;
    wxSpinCtrlDouble* m_bpmControl;
    wxChoice* m_durationMode;
    double m_bpm;
    wxSpinCtrlDouble* m_startBeat;
    wxSpinCtrlDouble* m_lengthBeat;
    wxSpinCtrl* m_note;
    wxSpinCtrl* m_velocity;
    wxStaticText* m_status;
    unsigned long m_nextId;
    wxTimer m_transportTimer;
    wxStopWatch m_transportClock;
    wxSpinCtrlDouble* m_startPosition;
    wxCheckBox* m_loopPlayback;
    wxSpinCtrlDouble* m_loopLength;
    bool m_playing;
    long m_playStartTick;
    long m_lastTransportTick;
    std::vector<unsigned long> m_activeVoiceIds;
    std::vector<bool> m_eventStarted;

    wxDECLARE_EVENT_TABLE();
};

#endif
