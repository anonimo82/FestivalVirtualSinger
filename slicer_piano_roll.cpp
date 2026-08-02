#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "slicer_piano_roll.h"
#include "offline_renderer.h"
#include <wx/dcbuffer.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <algorithm>
#include <cmath>

namespace
{
    const long kPPQ = 480;
    const int kKeyboardWidth = 62;
    const int kHeaderHeight = 24;
    const int kRowHeight = 18;
    const int kLowestNote = 36;
    const int kHighestNote = 84;
    const int kPixelsPerBeat = 96;
    const int kTimelineBeats = 128;

    wxString DragDiagnosticPath()
    {
        return wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPathWithSep() + wxT("M11_DRAG_DIAGNOSTIC.log");
    }

    void AppendDragDiagnostic(const wxString& text)
    {
        wxFFile file(DragDiagnosticPath(), wxT("ab"));
        if (!file.IsOpened()) return;
        wxString line = text + wxT("\r\n");
        file.Write(line);
    }

    size_t SelectedCount(const std::vector<SliceRollEvent>* events)
    {
        size_t count = 0;
        if (!events) return 0;
        for (size_t i = 0; i < events->size(); ++i)
            if ((*events)[i].selected) ++count;
        return count;
    }
}

wxBEGIN_EVENT_TABLE(SlicerPianoRollCanvas, wxScrolledWindow)
    EVT_PAINT(SlicerPianoRollCanvas::OnPaint)
    EVT_ERASE_BACKGROUND(SlicerPianoRollCanvas::OnEraseBackground)
    EVT_LEFT_DOWN(SlicerPianoRollCanvas::OnLeftDown)
    EVT_LEFT_UP(SlicerPianoRollCanvas::OnLeftUp)
    EVT_MOTION(SlicerPianoRollCanvas::OnMotion)
wxEND_EVENT_TABLE()

SlicerPianoRollCanvas::SlicerPianoRollCanvas(wxWindow* parent, std::vector<SliceRollEvent>* events, SliceModel* slices, SamplePool* samples, double* bpm)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition,
              wxSize(kKeyboardWidth + 16 * kPixelsPerBeat,
                     kHeaderHeight + (kHighestNote - kLowestNote + 1) * kRowHeight),
              wxBORDER_SIMPLE | wxHSCROLL | wxVSCROLL),
      m_events(events), m_slices(slices), m_samples(samples), m_bpm(bpm), m_loopSnap(false), m_snapTicks(kPPQ / 4), m_dragIndex(-1),
      m_originalStart(0), m_originalNote(60), m_dragging(false), m_marqueeSelecting(false), m_playheadTick(-1)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(760, 430));
    SetScrollRate(kPixelsPerBeat / 4, kRowHeight);
    SetVirtualSize(kKeyboardWidth + kTimelineBeats * kPixelsPerBeat,
                   kHeaderHeight + (kHighestNote - kLowestNote + 1) * kRowHeight);
}

void SlicerPianoRollCanvas::SetSnapTicks(long ticks)
{
    m_snapTicks = ticks > 0 ? ticks : 1;
}

long SlicerPianoRollCanvas::GetPrimarySelection() const
{
    for (size_t i = 0; i < m_events->size(); ++i)
        if ((*m_events)[i].selected) return static_cast<long>(i);
    return -1;
}

void SlicerPianoRollCanvas::ClearSelection()
{
    for (size_t i = 0; i < m_events->size(); ++i) (*m_events)[i].selected = false;
}

void SlicerPianoRollCanvas::RefreshView() { Refresh(false); }
void SlicerPianoRollCanvas::SetPlayheadTick(long tick) { m_playheadTick = tick; Refresh(false); }
void SlicerPianoRollCanvas::SetDurationMode(bool loopSnap) { m_loopSnap = loopSnap; Refresh(false); }
void SlicerPianoRollCanvas::CancelInteraction()
{
    if (HasCapture()) ReleaseMouse();
    m_dragging = false;
    m_marqueeSelecting = false;
    m_dragIndex = -1;
    Refresh(false);
}

const AudioSlice* SlicerPianoRollCanvas::FindSlice(const wxString& id) const
{
    if (!m_slices) return NULL;
    for (size_t i = 0; i < m_slices->GetCount(); ++i)
    {
        const AudioSlice* slice = m_slices->GetAt(i);
        if (slice && slice->id == id) return slice;
    }
    return NULL;
}

unsigned int SlicerPianoRollCanvas::FindSampleRate(const wxString& sourceId) const
{
    if (!m_samples) return 0;
    for (size_t i = 0; i < m_samples->GetCount(); ++i)
    {
        const SamplePoolItem* item = m_samples->GetAt(i);
        if (item && item->id == sourceId) return item->wav.sampleRate;
    }
    return 0;
}
void SlicerPianoRollCanvas::OnEraseBackground(wxEraseEvent&) {}

long SlicerPianoRollCanvas::Snap(long value) const
{
    if (m_snapTicks <= 1) return std::max<long>(0, value);
    return std::max<long>(0, ((value + m_snapTicks / 2) / m_snapTicks) * m_snapTicks);
}

wxRect SlicerPianoRollCanvas::EventRect(const SliceRollEvent& event) const
{
    const int x = kKeyboardWidth + static_cast<int>((double)event.startTick / kPPQ * kPixelsPerBeat);
    const int width = std::max(5, static_cast<int>((double)event.durationTicks / kPPQ * kPixelsPerBeat));
    const int y = kHeaderHeight + (kHighestNote - event.midiNote) * kRowHeight + 2;
    return wxRect(x, y, width, kRowHeight - 4);
}

int SlicerPianoRollCanvas::EventAt(const wxPoint& point) const
{
    for (int i = static_cast<int>(m_events->size()) - 1; i >= 0; --i)
        if (EventRect((*m_events)[i]).Contains(point)) return i;
    return -1;
}

void SlicerPianoRollCanvas::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(wxColour(248, 249, 251)));
    dc.Clear();

    PrepareDC(dc);

    const int width = kKeyboardWidth + kTimelineBeats * kPixelsPerBeat;
    const int height = kHeaderHeight + (kHighestNote - kLowestNote + 1) * kRowHeight;

    dc.SetPen(wxPen(wxColour(190, 196, 205)));
    dc.SetBrush(wxBrush(wxColour(230, 233, 238)));
    dc.DrawRectangle(0, 0, kKeyboardWidth, height);
    dc.DrawRectangle(kKeyboardWidth, 0, width - kKeyboardWidth, kHeaderHeight);

    for (int beat = 0; beat <= kTimelineBeats; ++beat)
    {
        const int x = kKeyboardWidth + beat * kPixelsPerBeat;
        const bool bar = (beat % 4) == 0;
        dc.SetPen(wxPen(bar ? wxColour(112, 120, 132) : wxColour(185, 190, 198), bar ? 2 : 1));
        dc.DrawLine(x, 0, x, height);
        if (beat < kTimelineBeats)
            dc.DrawText(wxString::Format(wxT("%d"), beat + 1), x + 4, 3);
        for (int sub = 1; sub < 4 && beat < kTimelineBeats; ++sub)
        {
            const int sx = x + sub * (kPixelsPerBeat / 4);
            dc.SetPen(wxPen(wxColour(224, 227, 232)));
            dc.DrawLine(sx, kHeaderHeight, sx, height);
        }
    }

    for (int note = kHighestNote; note >= kLowestNote; --note)
    {
        const int y = kHeaderHeight + (kHighestNote - note) * kRowHeight;
        const int pitchClass = note % 12;
        const bool black = pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
        dc.SetBrush(wxBrush(black ? wxColour(72, 77, 86) : wxColour(245, 246, 248)));
        dc.SetPen(wxPen(wxColour(195, 200, 208)));
        dc.DrawRectangle(0, y, kKeyboardWidth, kRowHeight);
        dc.SetTextForeground(black ? *wxWHITE : wxColour(45, 48, 54));
        if (pitchClass == 0)
            dc.DrawText(wxString::Format(wxT("C%d"), note / 12 - 1), 5, y + 1);
        dc.SetPen(wxPen(pitchClass == 0 ? wxColour(170, 176, 185) : wxColour(232, 234, 238)));
        dc.DrawLine(kKeyboardWidth, y, width, y);
    }

    if (m_playheadTick >= 0)
    {
        const int px = kKeyboardWidth + static_cast<int>((double)m_playheadTick / kPPQ * kPixelsPerBeat);
        dc.SetPen(wxPen(wxColour(210, 45, 45), 2));
        dc.DrawLine(px, 0, px, height);
    }

    for (size_t i = 0; i < m_events->size(); ++i)
    {
        const SliceRollEvent& e = (*m_events)[i];
        wxRect rect = EventRect(e);
        dc.SetBrush(wxBrush(e.selected ? wxColour(72, 129, 218) : wxColour(91, 157, 112)));
        dc.SetPen(wxPen(e.selected ? wxColour(34, 79, 150) : wxColour(48, 105, 67), e.selected ? 2 : 1));
        dc.DrawRoundedRectangle(rect, 3);

        const AudioSlice* slice = FindSlice(e.sliceId);
        if (slice && m_bpm)
        {
            const unsigned int sampleRate = FindSampleRate(slice->sourceId);
            const SliceEventTiming timing = SliceEventTimingCalculator::Calculate(*slice, sampleRate, e.midiNote, e.durationTicks, kPPQ, *m_bpm);
            if (timing.valid && timing.eventSeconds > 0.0)
            {
                const double attackRatio = std::min(1.0, timing.attackSeconds / timing.eventSeconds);
                const double tailRatio = timing.loopUsed ? std::min(1.0, timing.tailSeconds / timing.eventSeconds) : 0.0;
                const int attackX = rect.x + static_cast<int>(rect.width * attackRatio + 0.5);
                const int tailX = rect.x + rect.width - static_cast<int>(rect.width * tailRatio + 0.5);
                dc.SetPen(wxPen(wxColour(255, 238, 150), 1));
                if (attackX > rect.x && attackX < rect.GetRight()) dc.DrawLine(attackX, rect.y + 1, attackX, rect.GetBottom());
                if (timing.loopUsed && tailX > rect.x && tailX < rect.GetRight())
                {
                    dc.SetPen(wxPen(wxColour(255, 190, 118), 2));
                    dc.DrawLine(tailX, rect.y + 1, tailX, rect.GetBottom());
                }
                if (timing.residualSeconds > 0.000001)
                {
                    const int residualWidth = std::max(2, static_cast<int>(rect.width * timing.residualSeconds / timing.eventSeconds + 0.5));
                    dc.SetBrush(*wxTRANSPARENT_BRUSH);
                    dc.SetPen(wxPen(wxColour(255, 255, 255), 1, wxDOT));
                    dc.DrawRectangle(std::max(rect.x, tailX - residualWidth), rect.y + 2, residualWidth, std::max(1, rect.height - 4));
                }
            }
        }

        dc.SetTextForeground(*wxWHITE);
        dc.SetClippingRegion(rect);
        dc.DrawText(wxString::Format(wxT("%s  v%d"), e.sliceId.Left(8).c_str(), e.velocity), rect.x + 4, rect.y);
        dc.DestroyClippingRegion();
    }

    if (m_marqueeSelecting)
    {
        const int left = std::min(m_marqueeStart.x, m_marqueeCurrent.x);
        const int top = std::min(m_marqueeStart.y, m_marqueeCurrent.y);
        const int right = std::max(m_marqueeStart.x, m_marqueeCurrent.x);
        const int bottom = std::max(m_marqueeStart.y, m_marqueeCurrent.y);
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(wxColour(55, 105, 185), 1, wxSHORT_DASH));
        dc.DrawRectangle(left, top, std::max(1, right-left), std::max(1, bottom-top));
    }
}

void SlicerPianoRollCanvas::OnLeftDown(wxMouseEvent& event)
{
    int ux = 0, uy = 0;
    CalcUnscrolledPosition(event.GetX(), event.GetY(), &ux, &uy);
    const wxPoint logicalPoint(ux, uy);
    const int hit = EventAt(logicalPoint);
    AppendDragDiagnostic(wxString::Format(wxT("LEFT_DOWN screen=(%d,%d) logical=(%d,%d) hit=%d ctrl=%d selectedBefore=%lu"),
        event.GetX(), event.GetY(), ux, uy, hit, event.ControlDown() ? 1 : 0,
        static_cast<unsigned long>(SelectedCount(m_events))));

    if (hit < 0)
    {
        m_dragging = false;
        m_dragIndex = -1;
        m_marqueeSelecting = true;
        m_marqueeStart = logicalPoint;
        m_marqueeCurrent = logicalPoint;
        m_marqueeBaseSelection.resize(m_events->size());
        for (size_t i = 0; i < m_events->size(); ++i)
            m_marqueeBaseSelection[i] = (*m_events)[i].selected;
        if (!event.ControlDown() && !event.ShiftDown()) ClearSelection();
        SetFocus();
        if (!HasCapture()) CaptureMouse();
        Refresh(false);
        return;
    }

    // A normal click on an already-selected event must preserve the complete
    // multi-selection so the group can be dragged.  Ctrl toggles membership.
    if (event.ControlDown())
    {
        (*m_events)[hit].selected = !(*m_events)[hit].selected;
        if (!(*m_events)[hit].selected)
        {
            m_dragging = false;
            m_dragIndex = -1;
            Refresh(false);
            return;
        }
    }
    else if (!(*m_events)[hit].selected)
    {
        ClearSelection();
        (*m_events)[hit].selected = true;
    }

    SlicerPianoRollFrame* frame = dynamic_cast<SlicerPianoRollFrame*>(wxGetTopLevelParent(this));
    if (frame) frame->BeginCanvasEdit();
    m_dragIndex = hit;
    m_dragStart = logicalPoint;
    m_originalStart = (*m_events)[hit].startTick;
    m_originalNote = (*m_events)[hit].midiNote;
    m_dragOriginalStarts.resize(m_events->size());
    m_dragOriginalNotes.resize(m_events->size());
    for (size_t i = 0; i < m_events->size(); ++i)
    {
        m_dragOriginalStarts[i] = (*m_events)[i].startTick;
        m_dragOriginalNotes[i] = (*m_events)[i].midiNote;
    }
    m_dragging = true;
    AppendDragDiagnostic(wxString::Format(wxT("DRAG_ARMED index=%d id=%s startTick=%ld note=%d selectedNow=%lu"),
        hit, (*m_events)[hit].id.c_str(), m_originalStart, m_originalNote,
        static_cast<unsigned long>(SelectedCount(m_events))));
    SetFocus();
    if (!HasCapture()) CaptureMouse();
    Refresh(false);
}

void SlicerPianoRollCanvas::OnMotion(wxMouseEvent& event)
{
    if (m_marqueeSelecting)
    {
        if (!event.LeftIsDown()) return;
        int ux = 0, uy = 0;
        CalcUnscrolledPosition(event.GetX(), event.GetY(), &ux, &uy);
        m_marqueeCurrent = wxPoint(ux, uy);
        const int left = std::min(m_marqueeStart.x, m_marqueeCurrent.x);
        const int top = std::min(m_marqueeStart.y, m_marqueeCurrent.y);
        const int right = std::max(m_marqueeStart.x, m_marqueeCurrent.x);
        const int bottom = std::max(m_marqueeStart.y, m_marqueeCurrent.y);
        const wxRect marquee(left, top, std::max(1, right-left), std::max(1, bottom-top));
        const bool additive = event.ControlDown() || event.ShiftDown();
        for (size_t i = 0; i < m_events->size(); ++i)
        {
            const bool hit = marquee.Intersects(EventRect((*m_events)[i]));
            (*m_events)[i].selected = additive ? (m_marqueeBaseSelection[i] || hit) : hit;
        }
        Refresh(false);
        return;
    }

    // wxMouseEvent::Dragging() is not reliable on the wxWidgets/MSW version
    // used by this project while the mouse is captured.  The explicit drag
    // state established on LEFT_DOWN is the authoritative condition.
    if (!m_dragging || m_dragIndex < 0)
    {
        AppendDragDiagnostic(wxT("MOTION_IGNORED drag state inactive"));
        return;
    }
    if (!event.LeftIsDown())
    {
        AppendDragDiagnostic(wxT("MOTION_IGNORED LeftIsDown=0"));
        return;
    }

    int ux = 0, uy = 0;
    CalcUnscrolledPosition(event.GetX(), event.GetY(), &ux, &uy);
    const int dx = ux - m_dragStart.x;
    const int dy = uy - m_dragStart.y;
    const long tickDelta = static_cast<long>((double)dx / kPixelsPerBeat * kPPQ);
    const int noteDelta = -static_cast<int>(std::floor((double)dy / kRowHeight + (dy >= 0 ? 0.5 : -0.5)));
    AppendDragDiagnostic(wxString::Format(wxT("MOTION screen=(%d,%d) logical=(%d,%d) dx=%d dy=%d tickDelta=%ld noteDelta=%d left=%d capture=%d"),
        event.GetX(), event.GetY(), ux, uy, dx, dy, tickDelta, noteDelta,
        event.LeftIsDown() ? 1 : 0, HasCapture() ? 1 : 0));

    const long draggedStart = Snap(m_originalStart + tickDelta);
    long appliedTickDelta = draggedStart - m_originalStart;
    int appliedNoteDelta = std::max(kLowestNote, std::min(kHighestNote, m_originalNote + noteDelta)) - m_originalNote;

    // Clamp the whole selection as one group, preserving every relative offset.
    long minimumStart = m_originalStart;
    int minimumNote = m_originalNote;
    int maximumNote = m_originalNote;
    for (size_t i = 0; i < m_events->size(); ++i)
    {
        if (!(*m_events)[i].selected) continue;
        minimumStart = std::min(minimumStart, m_dragOriginalStarts[i]);
        minimumNote = std::min(minimumNote, m_dragOriginalNotes[i]);
        maximumNote = std::max(maximumNote, m_dragOriginalNotes[i]);
    }
    if (minimumStart + appliedTickDelta < 0) appliedTickDelta = -minimumStart;
    if (minimumNote + appliedNoteDelta < kLowestNote) appliedNoteDelta = kLowestNote - minimumNote;
    if (maximumNote + appliedNoteDelta > kHighestNote) appliedNoteDelta = kHighestNote - maximumNote;

    for (size_t i = 0; i < m_events->size(); ++i)
    {
        if (!(*m_events)[i].selected) continue;
        (*m_events)[i].startTick = m_dragOriginalStarts[i] + appliedTickDelta;
        (*m_events)[i].midiNote = m_dragOriginalNotes[i] + appliedNoteDelta;
    }
    AppendDragDiagnostic(wxString::Format(wxT("MOTION_APPLIED tickDelta=%ld noteDelta=%d selected=%lu"),
        appliedTickDelta, appliedNoteDelta, static_cast<unsigned long>(SelectedCount(m_events))));
    Refresh(false);
}

void SlicerPianoRollCanvas::OnLeftUp(wxMouseEvent& event)
{
    AppendDragDiagnostic(wxString::Format(wxT("LEFT_UP dragging=%d index=%d capture=%d selected=%lu"),
        m_dragging ? 1 : 0, m_dragIndex, HasCapture() ? 1 : 0,
        static_cast<unsigned long>(SelectedCount(m_events))));
    const bool completedDrag = m_dragging;
    if ((m_dragging || m_marqueeSelecting) && HasCapture()) ReleaseMouse();
    m_dragging = false;
    m_marqueeSelecting = false;
    m_dragIndex = -1;
    m_dragOriginalStarts.clear();
    m_dragOriginalNotes.clear();
    m_marqueeBaseSelection.clear();
    if (completedDrag)
    {
        SlicerPianoRollFrame* frame = dynamic_cast<SlicerPianoRollFrame*>(wxGetTopLevelParent(this));
        if (frame) frame->EndCanvasEdit();
    }
    Refresh(false);
    event.Skip();
}

wxBEGIN_EVENT_TABLE(SlicerPianoRollFrame, wxFrame)
    EVT_BUTTON(ID_Add, SlicerPianoRollFrame::OnAdd)
    EVT_BUTTON(ID_Delete, SlicerPianoRollFrame::OnDelete)
    EVT_BUTTON(ID_Duplicate, SlicerPianoRollFrame::OnDuplicate)
    EVT_BUTTON(ID_Copy, SlicerPianoRollFrame::OnCopy)
    EVT_BUTTON(ID_Cut, SlicerPianoRollFrame::OnCut)
    EVT_BUTTON(ID_Paste, SlicerPianoRollFrame::OnPaste)
    EVT_BUTTON(ID_Undo, SlicerPianoRollFrame::OnUndo)
    EVT_BUTTON(ID_Redo, SlicerPianoRollFrame::OnRedo)
    EVT_MENU(ID_Copy, SlicerPianoRollFrame::OnCopy)
    EVT_MENU(ID_Cut, SlicerPianoRollFrame::OnCut)
    EVT_MENU(ID_Paste, SlicerPianoRollFrame::OnPaste)
    EVT_MENU(ID_Undo, SlicerPianoRollFrame::OnUndo)
    EVT_MENU(ID_Redo, SlicerPianoRollFrame::OnRedo)
    EVT_MENU(ID_Delete, SlicerPianoRollFrame::OnDelete)
    EVT_MENU(ID_SelectAll, SlicerPianoRollFrame::OnSelectAll)
    EVT_BUTTON(ID_Apply, SlicerPianoRollFrame::OnApply)
    EVT_BUTTON(ID_SelectAll, SlicerPianoRollFrame::OnSelectAll)
    EVT_BUTTON(ID_ClearSelection, SlicerPianoRollFrame::OnClearSelection)
    EVT_CHOICE(ID_Snap, SlicerPianoRollFrame::OnSnap)
    EVT_SPINCTRLDOUBLE(ID_Bpm, SlicerPianoRollFrame::OnBpm)
    EVT_CHOICE(ID_DurationMode, SlicerPianoRollFrame::OnDurationMode)
    EVT_BUTTON(ID_Play, SlicerPianoRollFrame::OnPlay)
    EVT_BUTTON(ID_Stop, SlicerPianoRollFrame::OnStop)
    EVT_TIMER(ID_TransportTimer, SlicerPianoRollFrame::OnTransportTimer)
    EVT_CLOSE(SlicerPianoRollFrame::OnClose)
wxEND_EVENT_TABLE()

SlicerPianoRollFrame::SlicerPianoRollFrame(wxWindow* parent, SliceModel* slices, SamplePool* samples, SampleEngine* engine)
    : wxFrame(parent, wxID_ANY, wxT("Slicer Piano Roll"), wxDefaultPosition, wxSize(1280, 720),
              wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT),
      m_slices(slices), m_samples(samples), m_engine(engine), m_canvas(NULL), m_bpmControl(NULL), m_durationMode(NULL), m_bpm(120.0), m_nextId(1), m_canvasEditPending(false),
      m_transportTimer(this, ID_TransportTimer), m_startPosition(NULL), m_loopPlayback(NULL), m_loopLength(NULL),
      m_playing(false), m_playStartTick(0), m_lastTransportTick(0)
{
    wxPanel* panel = new wxPanel(this);
    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* toolbar = new wxBoxSizer(wxHORIZONTAL);
    toolbar->Add(new wxStaticText(panel, wxID_ANY, wxT("Active slice")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_sliceChoice = new wxChoice(panel, wxID_ANY);
    toolbar->Add(m_sliceChoice, 1, wxRIGHT, 10);
    toolbar->Add(new wxStaticText(panel, wxID_ANY, wxT("Snap")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_snapChoice = new wxChoice(panel, ID_Snap);
    m_snapChoice->Append(wxT("1/4")); m_snapChoice->Append(wxT("1/8"));
    m_snapChoice->Append(wxT("1/16")); m_snapChoice->Append(wxT("1/32"));
    m_snapChoice->SetSelection(2);
    toolbar->Add(m_snapChoice, 0, wxRIGHT, 10);
    toolbar->Add(new wxStaticText(panel, wxID_ANY, wxT("BPM")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_bpmControl = new wxSpinCtrlDouble(panel, ID_Bpm, wxT("120"), wxDefaultPosition, wxSize(78, -1), wxSP_ARROW_KEYS, 20.0, 400.0, 120.0, 1.0);
    toolbar->Add(m_bpmControl, 0, wxRIGHT, 10);
    toolbar->Add(new wxStaticText(panel, wxID_ANY, wxT("Duration")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_durationMode = new wxChoice(panel, ID_DurationMode);
    m_durationMode->Append(wxT("Free"));
    m_durationMode->Append(wxT("Loop Snap"));
    m_durationMode->SetSelection(0);
    toolbar->Add(m_durationMode, 0, wxRIGHT, 10);
    toolbar->Add(new wxStaticText(panel, wxID_ANY, wxT("Start")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_startPosition = new wxSpinCtrlDouble(panel, wxID_ANY, wxT("0"), wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0.0, 9999.0, 0.0, 0.25);
    toolbar->Add(m_startPosition, 0, wxRIGHT, 5);
    m_loopPlayback = new wxCheckBox(panel, wxID_ANY, wxT("Loop"));
    toolbar->Add(m_loopPlayback, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_loopLength = new wxSpinCtrlDouble(panel, wxID_ANY, wxT("4"), wxDefaultPosition, wxSize(65, -1), wxSP_ARROW_KEYS, 0.25, 9999.0, 4.0, 0.25);
    toolbar->Add(m_loopLength, 0, wxRIGHT, 5);
    toolbar->Add(new wxButton(panel, ID_Play, wxT("Play")), 0, wxRIGHT, 4);
    toolbar->Add(new wxButton(panel, ID_Stop, wxT("Stop")), 0, wxRIGHT, 8);
    toolbar->Add(new wxButton(panel, ID_Add, wxT("Add Event")), 0, wxRIGHT, 4);
    toolbar->Add(new wxButton(panel, ID_Delete, wxT("Delete")), 0, wxRIGHT, 4);
    toolbar->Add(new wxButton(panel, ID_Duplicate, wxT("Duplicate")), 0, wxRIGHT, 4);
    toolbar->Add(new wxButton(panel, ID_Undo, wxT("Undo")), 0, wxRIGHT, 4);
    toolbar->Add(new wxButton(panel, ID_Redo, wxT("Redo")), 0, wxRIGHT, 4);
    toolbar->Add(new wxButton(panel, ID_Copy, wxT("Copy")), 0, wxRIGHT, 4);
    toolbar->Add(new wxButton(panel, ID_Cut, wxT("Cut")), 0, wxRIGHT, 4);
    toolbar->Add(new wxButton(panel, ID_Paste, wxT("Paste")), 0, wxRIGHT, 4);
    toolbar->Add(new wxButton(panel, ID_SelectAll, wxT("Select All")), 0, wxRIGHT, 4);
    toolbar->Add(new wxButton(panel, ID_ClearSelection, wxT("Clear")), 0);
    root->Add(toolbar, 0, wxEXPAND | wxALL, 8);

    m_canvas = new SlicerPianoRollCanvas(panel, &m_events, m_slices, m_samples, &m_bpm);
    m_canvas->Bind(wxEVT_LEFT_UP, &SlicerPianoRollFrame::OnCanvasClick, this);
    m_canvas->SetSnapTicks(CurrentSnapTicks());
    root->Add(m_canvas, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

    wxStaticBoxSizer* inspector = new wxStaticBoxSizer(wxHORIZONTAL, panel, wxT("Selected event"));
    inspector->Add(new wxStaticText(panel, wxID_ANY, wxT("Start beat")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    m_startBeat = new wxSpinCtrlDouble(panel, wxID_ANY, wxT("0"), wxDefaultPosition, wxSize(85, -1), wxSP_ARROW_KEYS, 0, 9999, 0, 0.25);
    inspector->Add(m_startBeat, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
    inspector->Add(new wxStaticText(panel, wxID_ANY, wxT("Length")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    m_lengthBeat = new wxSpinCtrlDouble(panel, wxID_ANY, wxT("1"), wxDefaultPosition, wxSize(85, -1), wxSP_ARROW_KEYS, 0.0625, 9999, 1, 0.25);
    inspector->Add(m_lengthBeat, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
    inspector->Add(new wxStaticText(panel, wxID_ANY, wxT("MIDI note")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    m_note = new wxSpinCtrl(panel, wxID_ANY, wxT("60"), wxDefaultPosition, wxSize(72, -1), wxSP_ARROW_KEYS, 0, 127, 60);
    inspector->Add(m_note, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
    inspector->Add(new wxStaticText(panel, wxID_ANY, wxT("Velocity")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    m_velocity = new wxSpinCtrl(panel, wxID_ANY, wxT("100"), wxDefaultPosition, wxSize(72, -1), wxSP_ARROW_KEYS, 1, 127, 100);
    inspector->Add(m_velocity, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
    inspector->Add(new wxButton(panel, ID_Apply, wxT("Apply Event")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    m_status = new wxStaticText(panel, wxID_ANY, wxT("No event selected"));
    inspector->Add(m_status, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 12);
    root->Add(inspector, 0, wxEXPAND | wxALL, 8);

    wxStaticText* scope = new wxStaticText(panel, wxID_ANY,
        wxT("M12 scope: Undo/Redo, Cut/Copy/Paste and rectangular marquee selection are active."));
    scope->SetForegroundColour(wxColour(75, 83, 95));
    root->Add(scope, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    panel->SetSizer(root);
    wxAcceleratorEntry entries[8];
    entries[0].Set(wxACCEL_CTRL, (int)'Z', ID_Undo);
    entries[1].Set(wxACCEL_CTRL, (int)'Y', ID_Redo);
    entries[2].Set(wxACCEL_CTRL | wxACCEL_SHIFT, (int)'Z', ID_Redo);
    entries[3].Set(wxACCEL_CTRL, (int)'C', ID_Copy);
    entries[4].Set(wxACCEL_CTRL, (int)'X', ID_Cut);
    entries[5].Set(wxACCEL_CTRL, (int)'V', ID_Paste);
    entries[6].Set(wxACCEL_CTRL, (int)'A', ID_SelectAll);
    entries[7].Set(wxACCEL_NORMAL, WXK_DELETE, ID_Delete);
    SetAcceleratorTable(wxAcceleratorTable(8, entries));
    RefreshSlices();
    CentreOnParent();
}

void SlicerPianoRollFrame::RefreshSlices()
{
    wxString oldId;
    const int oldSelection = m_sliceChoice->GetSelection();
    if (oldSelection != wxNOT_FOUND) oldId = static_cast<wxString*>(m_sliceChoice->GetClientData(oldSelection)) ? *static_cast<wxString*>(m_sliceChoice->GetClientData(oldSelection)) : wxString();
    for (unsigned int i = 0; i < m_sliceChoice->GetCount(); ++i) delete static_cast<wxString*>(m_sliceChoice->GetClientData(i));
    m_sliceChoice->Clear();
    int newSelection = wxNOT_FOUND;
    for (size_t i = 0; i < m_slices->GetCount(); ++i)
    {
        const AudioSlice* slice = m_slices->GetAt(i);
        if (!slice) continue;
        const int index = m_sliceChoice->Append(slice->name + wxT("  [") + slice->id.Left(8) + wxT("]"), new wxString(slice->id));
        if (slice->id == oldId) newSelection = index;
    }
    if (newSelection == wxNOT_FOUND && m_sliceChoice->GetCount() > 0) newSelection = 0;
    if (newSelection != wxNOT_FOUND) m_sliceChoice->SetSelection(newSelection);
}

long SlicerPianoRollFrame::CurrentSnapTicks() const
{
    const int index = m_snapChoice ? m_snapChoice->GetSelection() : 2;
    if (index == 0) return kPPQ;
    if (index == 1) return kPPQ / 2;
    if (index == 3) return kPPQ / 8;
    return kPPQ / 4;
}

wxString SlicerPianoRollFrame::MakeEventId()
{
    return wxString::Format(wxT("event-%08lu"), m_nextId++);
}

long SlicerPianoRollFrame::PrimarySelection() const { return m_canvas->GetPrimarySelection(); }

void SlicerPianoRollFrame::SelectEvent(long index)
{
    m_canvas->ClearSelection();
    if (index >= 0 && static_cast<size_t>(index) < m_events.size()) m_events[index].selected = true;
    RefreshInspector();
    m_canvas->RefreshView();
}

const AudioSlice* SlicerPianoRollFrame::FindSlice(const wxString& id) const
{
    if (!m_slices) return NULL;
    for (size_t i = 0; i < m_slices->GetCount(); ++i)
    {
        const AudioSlice* slice = m_slices->GetAt(i);
        if (slice && slice->id == id) return slice;
    }
    return NULL;
}

unsigned int SlicerPianoRollFrame::FindSampleRate(const wxString& sourceId) const
{
    if (!m_samples) return 0;
    for (size_t i = 0; i < m_samples->GetCount(); ++i)
    {
        const SamplePoolItem* item = m_samples->GetAt(i);
        if (item && item->id == sourceId) return item->wav.sampleRate;
    }
    return 0;
}

const SamplePoolItem* SlicerPianoRollFrame::FindSource(const wxString& sourceId) const
{
    if (!m_samples) return NULL;
    for (size_t i = 0; i < m_samples->GetCount(); ++i)
    {
        const SamplePoolItem* item = m_samples->GetAt(i);
        if (item && item->id == sourceId) return item;
    }
    return NULL;
}

wxString SlicerPianoRollFrame::TimingSummary(const SliceRollEvent& event) const
{
    const AudioSlice* slice = FindSlice(event.sliceId);
    if (!slice) return wxT("missing slice");
    const SliceEventTiming timing = SliceEventTimingCalculator::Calculate(*slice, FindSampleRate(slice->sourceId), event.midiNote, event.durationTicks, kPPQ, m_bpm);
    if (!timing.valid) return wxT("timing unavailable");
    if (timing.earlyStop)
        return wxString::Format(wxT("%.3fs | too short: stop/fade at note end"), timing.eventSeconds);
    if (!timing.loopUsed)
        return wxString::Format(wxT("%.3fs | loop disabled | natural %.3fs"), timing.eventSeconds, timing.naturalEndSeconds);
    return wxString::Format(wxT("%.3fs | attack %.3f | loops %u | residual %.3f | tail %.3f"),
                            timing.eventSeconds, timing.attackSeconds, timing.fullLoopCount,
                            timing.residualSeconds, timing.tailSeconds);
}

void SlicerPianoRollFrame::RefreshInspector()
{
    const long index = PrimarySelection();
    if (index < 0 || static_cast<size_t>(index) >= m_events.size())
    {
        m_status->SetLabel(wxT("No event selected"));
        return;
    }
    const SliceRollEvent& e = m_events[index];
    m_startBeat->SetValue(static_cast<double>(e.startTick) / kPPQ);
    m_lengthBeat->SetValue(static_cast<double>(e.durationTicks) / kPPQ);
    m_note->SetValue(e.midiNote);
    m_velocity->SetValue(e.velocity);
    m_status->SetLabel(e.id + wxT("  ") + TimingSummary(e));
}

bool SlicerPianoRollFrame::EventStatesEqual(const std::vector<SliceRollEvent>& a, const std::vector<SliceRollEvent>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].id != b[i].id || a[i].sliceId != b[i].sliceId ||
            a[i].midiNote != b[i].midiNote || a[i].startTick != b[i].startTick ||
            a[i].durationTicks != b[i].durationTicks || a[i].velocity != b[i].velocity)
            return false;
    }
    return true;
}

void SlicerPianoRollFrame::PushUndoState()
{
    m_undoStack.push_back(m_events);
    if (m_undoStack.size() > 100) m_undoStack.erase(m_undoStack.begin());
    m_redoStack.clear();
}

void SlicerPianoRollFrame::RestoreState(const std::vector<SliceRollEvent>& state)
{
    if (m_canvas) m_canvas->CancelInteraction();
    m_events = state;
    RefreshInspector();
    if (m_canvas) m_canvas->RefreshView();
}

void SlicerPianoRollFrame::BeginCanvasEdit()
{
    if (m_canvasEditPending) return;
    m_pendingCanvasState = m_events;
    m_canvasEditPending = true;
}

void SlicerPianoRollFrame::EndCanvasEdit()
{
    if (!m_canvasEditPending) return;
    if (!EventStatesEqual(m_pendingCanvasState, m_events))
    {
        m_undoStack.push_back(m_pendingCanvasState);
        if (m_undoStack.size() > 100) m_undoStack.erase(m_undoStack.begin());
        m_redoStack.clear();
    }
    m_pendingCanvasState.clear();
    m_canvasEditPending = false;
    RefreshInspector();
}

void SlicerPianoRollFrame::OnUndo(wxCommandEvent&)
{
    if (m_undoStack.empty()) return;
    m_redoStack.push_back(m_events);
    const std::vector<SliceRollEvent> state = m_undoStack.back();
    m_undoStack.pop_back();
    RestoreState(state);
    m_status->SetLabel(wxT("Undo"));
}

void SlicerPianoRollFrame::OnRedo(wxCommandEvent&)
{
    if (m_redoStack.empty()) return;
    m_undoStack.push_back(m_events);
    const std::vector<SliceRollEvent> state = m_redoStack.back();
    m_redoStack.pop_back();
    RestoreState(state);
    m_status->SetLabel(wxT("Redo"));
}

void SlicerPianoRollFrame::OnAdd(wxCommandEvent&)
{
    const int selection = m_sliceChoice->GetSelection();
    if (selection == wxNOT_FOUND)
    {
        wxMessageBox(wxT("Create at least one slice before adding a piano-roll event."), wxT("Slicer Piano Roll"), wxOK | wxICON_INFORMATION, this);
        return;
    }
    wxString* id = static_cast<wxString*>(m_sliceChoice->GetClientData(selection));
    if (!id) return;
    PushUndoState();
    long endTick = 0;
    for (size_t i = 0; i < m_events.size(); ++i) endTick = std::max(endTick, m_events[i].startTick + m_events[i].durationTicks);
    SliceRollEvent e;
    e.id = MakeEventId(); e.sliceId = *id; e.startTick = ((endTick + CurrentSnapTicks() - 1) / CurrentSnapTicks()) * CurrentSnapTicks();
    e.durationTicks = kPPQ; e.midiNote = 60; e.velocity = 100; e.selected = true;
    m_canvas->ClearSelection(); m_events.push_back(e);
    RefreshInspector(); m_canvas->RefreshView();
}

void SlicerPianoRollFrame::OnDelete(wxCommandEvent&)
{
    bool any = false;
    for (size_t i = 0; i < m_events.size(); ++i) if (m_events[i].selected) { any = true; break; }
    if (!any) return;
    PushUndoState();
    m_events.erase(std::remove_if(m_events.begin(), m_events.end(), [](const SliceRollEvent& e){ return e.selected; }), m_events.end());
    RefreshInspector(); m_canvas->RefreshView();
}

void SlicerPianoRollFrame::OnDuplicate(wxCommandEvent&)
{
    std::vector<SliceRollEvent> added;
    const long delta = CurrentSnapTicks();
    for (size_t i = 0; i < m_events.size(); ++i)
        if (m_events[i].selected) { SliceRollEvent e = m_events[i]; e.id = MakeEventId(); e.startTick += delta; e.selected = false; added.push_back(e); }
    if (added.empty()) return;
    PushUndoState();
    m_canvas->ClearSelection();
    for (size_t i = 0; i < added.size(); ++i) { added[i].selected = true; m_events.push_back(added[i]); }
    RefreshInspector(); m_canvas->RefreshView();
}

void SlicerPianoRollFrame::OnCopy(wxCommandEvent&)
{
    m_clipboard.clear();
    for (size_t i = 0; i < m_events.size(); ++i) if (m_events[i].selected) m_clipboard.push_back(m_events[i]);
    m_status->SetLabel(wxString::Format(wxT("Copied %u event(s)"), static_cast<unsigned int>(m_clipboard.size())));
}

void SlicerPianoRollFrame::OnCut(wxCommandEvent&)
{
    m_clipboard.clear();
    for (size_t i = 0; i < m_events.size(); ++i)
        if (m_events[i].selected) m_clipboard.push_back(m_events[i]);
    if (m_clipboard.empty()) return;
    PushUndoState();
    m_events.erase(std::remove_if(m_events.begin(), m_events.end(), [](const SliceRollEvent& e){ return e.selected; }), m_events.end());
    RefreshInspector();
    m_canvas->RefreshView();
    m_status->SetLabel(wxString::Format(wxT("Cut %u event(s)"), static_cast<unsigned int>(m_clipboard.size())));
}

void SlicerPianoRollFrame::OnPaste(wxCommandEvent&)
{
    if (m_clipboard.empty()) return;
    PushUndoState();
    m_canvas->ClearSelection();
    long earliest = m_clipboard[0].startTick;
    for (size_t i = 1; i < m_clipboard.size(); ++i) earliest = std::min(earliest, m_clipboard[i].startTick);
    long target = earliest + CurrentSnapTicks();
    if (m_startPosition) target = std::max<long>(0, static_cast<long>(std::floor(m_startPosition->GetValue() * kPPQ + 0.5)));
    target = ((target + CurrentSnapTicks() / 2) / CurrentSnapTicks()) * CurrentSnapTicks();
    for (size_t i = 0; i < m_clipboard.size(); ++i)
    {
        SliceRollEvent e = m_clipboard[i];
        e.id = MakeEventId();
        e.startTick = target + (m_clipboard[i].startTick - earliest);
        e.selected = true;
        m_events.push_back(e);
    }
    RefreshInspector(); m_canvas->RefreshView();
}

void SlicerPianoRollFrame::OnApply(wxCommandEvent&)
{
    const long index = PrimarySelection();
    if (index < 0 || static_cast<size_t>(index) >= m_events.size()) return;
    PushUndoState();
    SliceRollEvent& e = m_events[index];
    e.startTick = std::max<long>(0, static_cast<long>(std::floor(m_startBeat->GetValue() * kPPQ + 0.5)));
    e.durationTicks = std::max<long>(1, static_cast<long>(std::floor(m_lengthBeat->GetValue() * kPPQ + 0.5)));
    e.midiNote = m_note->GetValue(); e.velocity = m_velocity->GetValue();
    if (m_durationMode && m_durationMode->GetSelection() == 1)
    {
        const AudioSlice* slice = FindSlice(e.sliceId);
        if (slice)
            e.durationTicks = SliceEventTimingCalculator::SnapDurationToWholeLoops(*slice, FindSampleRate(slice->sourceId), e.midiNote, e.durationTicks, kPPQ, m_bpm);
    }
    RefreshInspector(); m_canvas->RefreshView();
}

void SlicerPianoRollFrame::OnSelectAll(wxCommandEvent&)
{
    for (size_t i = 0; i < m_events.size(); ++i) m_events[i].selected = true;
    RefreshInspector(); m_canvas->RefreshView();
}
void SlicerPianoRollFrame::OnClearSelection(wxCommandEvent&) { m_canvas->ClearSelection(); RefreshInspector(); m_canvas->RefreshView(); }
void SlicerPianoRollFrame::OnSnap(wxCommandEvent&) { m_canvas->SetSnapTicks(CurrentSnapTicks()); }
void SlicerPianoRollFrame::OnBpm(wxSpinDoubleEvent&)
{
    m_bpm = m_bpmControl ? m_bpmControl->GetValue() : 120.0;
    RefreshInspector();
    m_canvas->RefreshView();
}
void SlicerPianoRollFrame::OnDurationMode(wxCommandEvent&)
{
    m_canvas->SetDurationMode(m_durationMode && m_durationMode->GetSelection() == 1);
    RefreshInspector();
}
void SlicerPianoRollFrame::OnCanvasClick(wxMouseEvent& event) { RefreshInspector(); event.Skip(); }
void SlicerPianoRollFrame::OnPlay(wxCommandEvent&)
{
    if (m_events.empty())
    {
        wxMessageBox(wxT("Add at least one slicer event before starting playback."), wxT("Slicer Transport"), wxOK | wxICON_INFORMATION, this);
        return;
    }
    if (!m_engine) return;
    m_engine->StopAll();
    m_playStartTick = std::max<long>(0, static_cast<long>(std::floor(m_startPosition->GetValue() * kPPQ + 0.5)));
    m_lastTransportTick = m_playStartTick;
    m_eventStarted.assign(m_events.size(), false);
    m_activeVoiceIds.assign(m_events.size(), 0);
    for (size_t i = 0; i < m_events.size(); ++i)
        if (m_events[i].startTick < m_playStartTick) m_eventStarted[i] = true;
    m_transportClock.Start(0);
    m_playing = true;
    m_canvas->SetPlayheadTick(m_playStartTick);
    m_transportTimer.Start(10);
    m_status->SetLabel(wxT("Transport playing"));
}

void SlicerPianoRollFrame::OnStop(wxCommandEvent&)
{
    m_transportTimer.Stop();
    m_playing = false;
    m_activeVoiceIds.assign(m_events.size(), 0);
    if (m_engine) m_engine->StopAll();
    m_canvas->SetPlayheadTick(-1);
    m_status->SetLabel(wxT("Transport stopped"));
}

void SlicerPianoRollFrame::OnTransportTimer(wxTimerEvent&)
{
    if (!m_playing || !m_engine) return;
    const double elapsedSeconds = m_transportClock.Time() / 1000.0;
    long currentTick = m_playStartTick + static_cast<long>(std::floor(elapsedSeconds * m_bpm / 60.0 * kPPQ + 0.5));

    const long loopStart = m_playStartTick;
    const long loopTicks = std::max<long>(1, static_cast<long>(std::floor(m_loopLength->GetValue() * kPPQ + 0.5)));
    if (m_loopPlayback->GetValue() && currentTick >= loopStart + loopTicks)
    {
        m_engine->StopAll();
        m_transportClock.Start(0);
        currentTick = loopStart;
        m_lastTransportTick = loopStart;
        m_eventStarted.assign(m_events.size(), false);
        m_activeVoiceIds.assign(m_events.size(), 0);
        for (size_t i = 0; i < m_events.size(); ++i)
            if (m_events[i].startTick < loopStart || m_events[i].startTick >= loopStart + loopTicks) m_eventStarted[i] = true;
    }

    // Release each event's own realtime voice at its own right edge.
    for (size_t i = 0; i < m_events.size(); ++i)
    {
        if (i >= m_activeVoiceIds.size() || m_activeVoiceIds[i] == 0) continue;
        const SliceRollEvent& active = m_events[i];
        if (currentTick >= active.startTick + active.durationTicks)
        {
            m_engine->NoteOff(m_activeVoiceIds[i]);
            m_activeVoiceIds[i] = 0;
        }
    }

    // Start every event reached by the playhead. Overlapping events receive
    // independent voice ids and therefore remain audible together.
    for (size_t i = 0; i < m_events.size(); ++i)
    {
        if (m_eventStarted[i]) continue;
        const SliceRollEvent& e = m_events[i];
        if (e.startTick > currentTick) continue;
        m_eventStarted[i] = true;
        const AudioSlice* slice = FindSlice(e.sliceId);
        const SamplePoolItem* source = slice ? FindSource(slice->sourceId) : NULL;
        if (!slice || !source) continue;
        wxString error;
        unsigned long voiceId = 0;
        if (m_engine->NoteOn(*source, *slice, e.midiNote, e.velocity,
                             SampleEngine::ModeRetrigger, &voiceId, &error))
            m_activeVoiceIds[i] = voiceId;
        else if (!error.empty())
            m_status->SetLabel(error);
    }

    long finalTick = 0;
    for (size_t i = 0; i < m_events.size(); ++i)
        finalTick = std::max(finalTick, m_events[i].startTick + m_events[i].durationTicks);
    m_canvas->SetPlayheadTick(currentTick);
    m_lastTransportTick = currentTick;

    bool anyActive = false;
    for (size_t i = 0; i < m_activeVoiceIds.size(); ++i)
        if (m_activeVoiceIds[i] != 0 && m_engine->IsVoiceActive(m_activeVoiceIds[i])) { anyActive = true; break; }

    if (!m_loopPlayback->GetValue() && currentTick > finalTick && !anyActive)
    {
        wxCommandEvent dummy;
        OnStop(dummy);
    }
}

void SlicerPianoRollFrame::OnClose(wxCloseEvent& event)
{
    m_transportTimer.Stop();
    m_playing = false;
    if (m_engine) m_engine->StopAll();
    if (event.CanVeto()) { Hide(); event.Veto(); }
    else { event.Skip(); }
}


void SlicerPianoRollFrame::SetEvents(const std::vector<SliceRollEvent>& events)
{
    if (m_transportTimer.IsRunning()) m_transportTimer.Stop();
    m_playing = false;
    if (m_engine) m_engine->StopAll();
    if (m_canvas) m_canvas->SetPlayheadTick(-1);
    m_events = events;
    m_undoStack.clear();
    m_redoStack.clear();
    m_pendingCanvasState.clear();
    m_canvasEditPending = false;
    unsigned long highest = 0;
    for (size_t i = 0; i < m_events.size(); ++i)
    {
        m_events[i].selected = false;
        wxString tail = m_events[i].id.AfterLast(wxT('-'));
        unsigned long value = 0;
        if (tail.ToULong(&value)) highest = std::max(highest, value);
    }
    m_nextId = highest + 1;
    RefreshInspector();
    if (m_canvas) m_canvas->RefreshView();
}

void SlicerPianoRollFrame::SetBpm(double bpm)
{
    m_bpm = std::max(20.0, std::min(400.0, bpm));
    if (m_bpmControl) m_bpmControl->SetValue(m_bpm);
    if (m_canvas) m_canvas->RefreshView();
}

bool SlicerPianoRollFrame::ExportWav(const wxString& path, wxString* errorMessage) const
{
    if (!m_samples || !m_slices)
    {
        if (errorMessage) *errorMessage = wxT("Slicer project data is unavailable.");
        return false;
    }
    return OfflineSlicerRenderer::Render(path, *m_samples, *m_slices, m_events, m_bpm, errorMessage);
}
