#include "piano_roll.h"

#include <wx/dcbuffer.h>
#include <wx/settings.h>
#include <algorithm>
#include <cmath>

namespace
{
    const double SNAP_SUBDIVISIONS = 4.0;

    bool SameEvent(const SingingEvent& left,
                   const SingingEvent& right)
    {
        return left.pitch == right.pitch &&
               std::fabs(left.beats - right.beats) < 0.000001 &&
               left.phoneme == right.phoneme;
    }
}

PianoRollPanel::PianoRollPanel(wxWindow* parent)
    : wxScrolledWindow(parent, wxID_ANY,
                       wxDefaultPosition, wxDefaultSize,
                       wxHSCROLL | wxVSCROLL | wxBORDER_SIMPLE),
      m_song(NULL),
      m_selectedIndex(-1),
      m_dragMode(Drag_None),
      m_originPitchRow(0),
      m_originBeats(1.0),
      m_originStartBeat(0.0),
      m_hasPreview(false),
      m_labelWidth(66),
      m_rowHeight(24),
      m_beatWidth(92),
      m_snapBeats(0.25)
{
    // C2..B6, displayed from high to low.
    m_pitches = BuildPitchList(36, 95);

    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetScrollRate(12, 12);
    SetMinSize(wxSize(550, 360));

    Bind(wxEVT_PAINT, &PianoRollPanel::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &PianoRollPanel::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &PianoRollPanel::OnLeftUp, this);
    Bind(wxEVT_MOTION, &PianoRollPanel::OnMotion, this);
    Bind(wxEVT_LEFT_DCLICK, &PianoRollPanel::OnLeftDClick, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &PianoRollPanel::OnCaptureLost, this);
    Bind(wxEVT_KEY_DOWN, &PianoRollPanel::OnKeyDown, this);
}

void PianoRollPanel::SetSong(SingingSong* song)
{
    m_song = song;
    if (m_song == NULL || m_song->events.empty())
        m_selectedIndex = -1;
    else if (m_selectedIndex < 0 ||
             m_selectedIndex >= static_cast<int>(m_song->events.size()))
        m_selectedIndex = 0;

    RebuildGeometry();
    Refresh();
}

void PianoRollPanel::SetSelectedIndex(int index)
{
    if (m_song == NULL || index < 0 ||
        index >= static_cast<int>(m_song->events.size()))
        m_selectedIndex = -1;
    else
        m_selectedIndex = index;

    Refresh();
}

int PianoRollPanel::GetSelectedIndex() const
{
    return m_selectedIndex;
}

void PianoRollPanel::SetSnapBeats(double value)
{
    m_snapBeats = value > 0.0 ? value : 0.25;
}

void PianoRollPanel::SetSelectionCallback(
    const SelectionCallback& callback)
{
    m_onSelection = callback;
}

void PianoRollPanel::SetBeforeChangeCallback(
    const BeforeChangeCallback& callback)
{
    m_onBeforeChange = callback;
}

void PianoRollPanel::SetChangedCallback(
    const ChangedCallback& callback)
{
    m_onChanged = callback;
}

void PianoRollPanel::SetPreviewCallback(
    const PreviewCallback& callback)
{
    m_onPreview = callback;
}

void PianoRollPanel::SetDeleteCallback(
    const DeleteCallback& callback)
{
    m_onDelete = callback;
}

void PianoRollPanel::RebuildGeometry()
{
    m_geometry.clear();

    double startBeat = 0.0;
    if (m_song != NULL)
    {
        for (size_t i = 0; i < m_song->events.size(); ++i)
        {
            const SingingEvent& note = m_song->events[i];
            const int row = PitchRow(note.pitch);
            const int x = m_labelWidth +
                          static_cast<int>(std::floor(startBeat * m_beatWidth)) + 2;
            const int y = row * m_rowHeight + 2;
            const int width = std::max(
                24,
                static_cast<int>(std::floor(note.beats * m_beatWidth)) - 4);

            NoteGeometry geometry;
            geometry.rect = wxRect(x, y, width, m_rowHeight - 4);
            geometry.startBeat = startBeat;
            m_geometry.push_back(geometry);
            startBeat += note.beats;
        }
    }

    const int visibleBeats =
        std::max(16, static_cast<int>(std::ceil(startBeat)) + 4);
    SetVirtualSize(m_labelWidth + visibleBeats * m_beatWidth,
                   static_cast<int>(m_pitches.size()) * m_rowHeight);
}

int PianoRollPanel::HitTestNote(const wxPoint& point) const
{
    for (int i = static_cast<int>(m_geometry.size()) - 1; i >= 0; --i)
    {
        if (m_geometry[i].rect.Contains(point))
            return i;
    }
    return -1;
}

int PianoRollPanel::PitchRow(const wxString& pitch) const
{
    const int midi = PitchToMidi(pitch);
    const int lowest = PitchToMidi(m_pitches.front());
    const int highest = PitchToMidi(m_pitches.back());
    const int clamped = std::max(lowest, std::min(highest, midi));
    return highest - clamped;
}

wxString PianoRollPanel::RowPitch(int row) const
{
    row = std::max(0, std::min(
        static_cast<int>(m_pitches.size()) - 1, row));
    return m_pitches[m_pitches.size() - 1 - row];
}

double PianoRollPanel::EffectiveSnapBeats() const
{
    const double gridStep = m_snapBeats > 0.0 ? m_snapBeats : 0.25;
    return gridStep / SNAP_SUBDIVISIONS;
}

double PianoRollPanel::Snap(double value) const
{
    const double snapStep = EffectiveSnapBeats();
    return std::floor(value / snapStep + 0.5) * snapStep;
}

int PianoRollPanel::SnapPixelDelta(int pixelDelta) const
{
    const double beatDelta =
        Snap(static_cast<double>(pixelDelta) / m_beatWidth);

    return static_cast<int>(
        std::floor(beatDelta * m_beatWidth +
                   (beatDelta >= 0.0 ? 0.5 : -0.5)));
}

int PianoRollPanel::InsertionIndexFromBeat(double targetBeat,
                                           int movingIndex) const
{
    if (m_song == NULL)
        return 0;

    double cursor = 0.0;
    int insertion = 0;

    for (int i = 0; i < static_cast<int>(m_song->events.size()); ++i)
    {
        if (i == movingIndex)
            continue;

        const double beats = m_song->events[i].beats;
        if (targetBeat > cursor + beats / 2.0)
            ++insertion;
        cursor += beats;
    }

    return insertion;
}

void PianoRollPanel::RequestPreview(const SingingEvent& event,
                                    const wxString& editKind)
{
    const wxString signature =
        event.pitch + wxT("|") +
        wxString::Format(wxT("%.4f"), event.beats) +
        wxT("|") + editKind;

    if (signature == m_lastPreviewSignature)
        return;

    m_lastPreviewSignature = signature;
    if (m_onPreview)
        m_onPreview(event, editKind);
}

void PianoRollPanel::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    PrepareDC(dc);

    dc.SetBackground(wxBrush(wxColour(250, 251, 253)));
    dc.Clear();

    const int totalRows = static_cast<int>(m_pitches.size());
    const wxSize virtualSize = GetVirtualSize();
    const int beats =
        std::max(16, (virtualSize.GetWidth() - m_labelWidth) / m_beatWidth);

    dc.SetFont(wxFont(8, wxFONTFAMILY_SWISS,
                      wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));

    for (int row = 0; row < totalRows; ++row)
    {
        const wxString pitch = RowPitch(row);
        const bool blackKey = pitch.Contains(wxT("#"));
        const wxColour background = blackKey
            ? wxColour(239, 242, 246)
            : ((row % 2) ? wxColour(248, 249, 251)
                         : wxColour(253, 253, 254));

        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(background));
        dc.DrawRectangle(0, row * m_rowHeight,
                         virtualSize.GetWidth(), m_rowHeight);

        dc.SetPen(wxPen(wxColour(222, 226, 232), 1));
        dc.DrawLine(0, row * m_rowHeight,
                    virtualSize.GetWidth(), row * m_rowHeight);

        dc.SetTextForeground(wxColour(67, 74, 84));
        dc.DrawText(pitch, 10, row * m_rowHeight + 4);
    }

    dc.SetPen(wxPen(wxColour(184, 191, 201), 1));
    dc.DrawLine(m_labelWidth, 0, m_labelWidth,
                totalRows * m_rowHeight);

    // Draw the selected grid step, plus quarter-step guides when there
    // is enough horizontal room for them to remain readable.
    const double gridStep = m_snapBeats > 0.0 ? m_snapBeats : 0.25;
    const double fineStep = EffectiveSnapBeats();
    const double gridPixels = gridStep * m_beatWidth;
    const double finePixels = fineStep * m_beatWidth;
    const double epsilon = 0.000001;

    if (finePixels >= 3.0)
    {
        for (double position = fineStep;
             position < static_cast<double>(beats);
             position += fineStep)
        {
            const double nearestGrid =
                std::floor(position / gridStep + 0.5) * gridStep;
            const double nearestBeat =
                std::floor(position + 0.5);

            if (std::fabs(position - nearestGrid) < epsilon ||
                std::fabs(position - nearestBeat) < epsilon)
            {
                continue;
            }

            const int x = m_labelWidth +
                static_cast<int>(std::floor(
                    position * m_beatWidth + 0.5));

            dc.SetPen(wxPen(wxColour(238, 240, 244), 1));
            dc.DrawLine(x, 0, x, totalRows * m_rowHeight);
        }
    }

    if (gridPixels >= 3.0)
    {
        for (double position = gridStep;
             position < static_cast<double>(beats);
             position += gridStep)
        {
            const double nearestBeat =
                std::floor(position + 0.5);

            if (std::fabs(position - nearestBeat) < epsilon)
                continue;

            const int x = m_labelWidth +
                static_cast<int>(std::floor(
                    position * m_beatWidth + 0.5));

            dc.SetPen(wxPen(wxColour(222, 226, 232), 1));
            dc.DrawLine(x, 0, x, totalRows * m_rowHeight);
        }
    }

    for (int beat = 0; beat <= beats; ++beat)
    {
        const int x = m_labelWidth + beat * m_beatWidth;
        const bool measure = beat % 4 == 0;
        dc.SetPen(wxPen(measure
                           ? wxColour(133, 143, 157)
                           : wxColour(213, 217, 224),
                       measure ? 2 : 1));
        dc.DrawLine(x, 0, x, totalRows * m_rowHeight);

        dc.SetTextForeground(wxColour(95, 102, 113));
        dc.DrawText(wxString::Format(wxT("%d"), beat + 1),
                    x + 5, 2);
    }

    if (m_song != NULL)
    {
        for (size_t i = 0; i < m_geometry.size(); ++i)
        {
            const bool selected =
                static_cast<int>(i) == m_selectedIndex;
            const wxRect& rect = m_geometry[i].rect;

            const bool pause =
                IsPauseEvent(m_song->events[i]);

            const wxColour fill = pause
                ? (selected
                    ? wxColour(181, 154, 202)
                    : wxColour(133, 139, 151))
                : (selected
                    ? wxColour(236, 156, 54)
                    : wxColour(49, 137, 173));
            const wxColour outline = pause
                ? (selected
                    ? wxColour(88, 60, 112)
                    : wxColour(72, 77, 87))
                : (selected
                    ? wxColour(121, 71, 8)
                    : wxColour(22, 86, 112));

            dc.SetPen(wxPen(outline, selected ? 3 : 1));
            dc.SetBrush(wxBrush(fill));
            dc.DrawRoundedRectangle(rect, 4);

            wxRect handle(rect.GetRight() - 7, rect.GetTop(),
                          8, rect.GetHeight());
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(selected
                                    ? wxColour(190, 105, 7)
                                    : wxColour(24, 99, 127)));
            dc.DrawRectangle(handle);

            dc.SetTextForeground(*wxWHITE);
            dc.SetFont(wxFont(8, wxFONTFAMILY_SWISS,
                              wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
            dc.DrawText(
                pause ? wxT("REST") : m_song->events[i].phoneme,
                rect.GetLeft() + 6,
                rect.GetTop() + 3);
        }
    }

    if (m_hasPreview)
    {
        dc.SetPen(wxPen(wxColour(187, 65, 32), 2, wxPENSTYLE_SHORT_DASH));
        dc.SetBrush(wxBrush(wxColour(255, 211, 128), wxBRUSHSTYLE_BDIAGONAL_HATCH));
        dc.DrawRoundedRectangle(m_previewRect, 4);

        dc.SetTextForeground(wxColour(94, 43, 20));
        const wxString previewLabel =
            IsPauseEvent(m_previewEvent)
                ? wxT("REST")
                : m_previewEvent.pitch;

        dc.DrawText(
            previewLabel +
            wxString::Format(wxT("  %.2f"), m_previewEvent.beats),
            m_previewRect.GetLeft() + 5,
            m_previewRect.GetTop() + 3);
    }
}

void PianoRollPanel::OnLeftDown(wxMouseEvent& event)
{
    SetFocus();

    int logicalX = 0;
    int logicalY = 0;
    CalcUnscrolledPosition(event.GetX(), event.GetY(),
                           &logicalX, &logicalY);
    const wxPoint point(logicalX, logicalY);

    const int index = HitTestNote(point);
    if (index < 0)
    {
        m_selectedIndex = -1;
        if (m_onSelection)
            m_onSelection(-1);
        Refresh();
        return;
    }

    m_selectedIndex = index;
    if (m_onSelection)
        m_onSelection(index);

    const wxRect rect = m_geometry[index].rect;
    m_dragMode = point.x >= rect.GetRight() - 10
        ? Drag_Resize
        : Drag_Move;
    m_dragStart = point;
    m_originRect = rect;
    m_originPitchRow = PitchRow(m_song->events[index].pitch);
    m_originBeats = m_song->events[index].beats;
    m_originStartBeat = m_geometry[index].startBeat;
    m_previewEvent = m_song->events[index];
    m_previewRect = rect;
    m_hasPreview = true;
    m_lastPreviewSignature.clear();

    if (!HasCapture())
        CaptureMouse();

    Refresh();
}

void PianoRollPanel::OnMotion(wxMouseEvent& event)
{
    if (m_dragMode == Drag_None || !event.Dragging() ||
        !event.LeftIsDown() || m_song == NULL ||
        m_selectedIndex < 0)
        return;

    int logicalX = 0;
    int logicalY = 0;
    CalcUnscrolledPosition(event.GetX(), event.GetY(),
                           &logicalX, &logicalY);

    const int dx = logicalX - m_dragStart.x;
    const int dy = logicalY - m_dragStart.y;

    if (m_dragMode == Drag_Resize)
    {
        const double rawDelta =
            static_cast<double>(dx) / m_beatWidth;
        const double snappedBeats =
            Snap(m_originBeats + rawDelta);

        m_previewEvent.beats =
            std::max(EffectiveSnapBeats(), snappedBeats);

        m_previewRect = m_originRect;
        m_previewRect.SetWidth(std::max(
            24,
            static_cast<int>(m_previewEvent.beats * m_beatWidth) - 4));

        RequestPreview(m_previewEvent,
                       dx >= 0 ? wxT("lengthening")
                               : wxT("shortening"));
    }
    else
    {
        const int rowDelta =
            static_cast<int>(std::floor(
                static_cast<double>(dy) / m_rowHeight +
                (dy >= 0 ? 0.5 : -0.5)));
        const int newRow = std::max(
            0,
            std::min(static_cast<int>(m_pitches.size()) - 1,
                     m_originPitchRow + rowDelta));

        const int snappedDx = SnapPixelDelta(dx);

        m_previewEvent.pitch = RowPitch(newRow);
        m_previewRect = m_originRect;
        m_previewRect.Offset(
            snappedDx,
            (newRow - m_originPitchRow) * m_rowHeight);

        RequestPreview(
            m_previewEvent,
            std::abs(snappedDx) >=
                std::max(1,
                    static_cast<int>(
                        EffectiveSnapBeats() * m_beatWidth / 2.0))
                ? wxT("moving")
                : wxT("pitch change"));
    }

    Refresh();
}

void PianoRollPanel::OnLeftUp(wxMouseEvent& event)
{
    if (m_dragMode == Drag_None)
        return;

    if (HasCapture())
        ReleaseMouse();

    bool changed = false;

    if (m_song != NULL && m_selectedIndex >= 0 &&
        m_selectedIndex < static_cast<int>(m_song->events.size()))
    {
        const int oldIndex = m_selectedIndex;
        int targetIndex = oldIndex;

        if (m_dragMode == Drag_Move)
        {
            int logicalX = 0;
            int logicalY = 0;
            CalcUnscrolledPosition(event.GetX(), event.GetY(),
                                   &logicalX, &logicalY);
            const int dx = logicalX - m_dragStart.x;
            const int snappedDx = SnapPixelDelta(dx);
            const double snappedBeatDelta =
                Snap(static_cast<double>(dx) / m_beatWidth);

            if (std::abs(snappedDx) >=
                std::max(1,
                    static_cast<int>(
                        EffectiveSnapBeats() * m_beatWidth / 2.0)))
            {
                const double centerBeat =
                    m_originStartBeat +
                    m_originBeats / 2.0 +
                    snappedBeatDelta;

                const int insertion =
                    InsertionIndexFromBeat(
                        std::max(0.0, centerBeat), oldIndex);

                targetIndex = std::max(
                    0,
                    std::min(insertion,
                             static_cast<int>(m_song->events.size()) - 1));
            }
        }

        const bool eventChanged =
            !SameEvent(m_song->events[oldIndex], m_previewEvent);
        const bool orderChanged = targetIndex != oldIndex;

        if (eventChanged || orderChanged)
        {
            if (m_onBeforeChange)
                m_onBeforeChange();

            if (m_dragMode == Drag_Resize)
            {
                ApplyEventEditPreservingFollowingTiming(
                    &m_song->events,
                    static_cast<size_t>(oldIndex),
                    m_previewEvent);
            }
            else
            {
                m_song->events[oldIndex].pitch =
                    m_previewEvent.pitch;
            }

            if (orderChanged)
            {
                SingingEvent moving = m_song->events[oldIndex];
                m_song->events.erase(m_song->events.begin() + oldIndex);
                m_song->events.insert(
                    m_song->events.begin() + targetIndex, moving);
                m_selectedIndex = targetIndex;
            }

            RequestPreview(m_song->events[m_selectedIndex],
                           m_dragMode == Drag_Resize
                               ? wxT("duration confirmed")
                               : wxT("position confirmed"));
            changed = true;
        }
    }

    m_dragMode = Drag_None;
    m_hasPreview = false;
    RebuildGeometry();

    if (changed && m_onChanged)
        m_onChanged(m_selectedIndex);

    Refresh();
}

void PianoRollPanel::OnLeftDClick(wxMouseEvent& event)
{
    if (m_song == NULL)
        return;

    int logicalX = 0;
    int logicalY = 0;
    CalcUnscrolledPosition(event.GetX(), event.GetY(),
                           &logicalX, &logicalY);
    const wxPoint point(logicalX, logicalY);

    if (HitTestNote(point) >= 0)
        return;

    const int row = std::max(
        0,
        std::min(static_cast<int>(m_pitches.size()) - 1,
                 logicalY / m_rowHeight));
    const double beat =
        std::max(0.0,
                 static_cast<double>(logicalX - m_labelWidth) /
                 m_beatWidth);

    const int insertion = InsertionIndexFromBeat(beat, -1);

    if (m_onBeforeChange)
        m_onBeforeChange();

    m_song->events.insert(
        m_song->events.begin() + insertion,
        SingingEvent(RowPitch(row), 1.0, wxT("la")));
    m_selectedIndex = insertion;

    RebuildGeometry();

    if (m_onSelection)
        m_onSelection(m_selectedIndex);
    if (m_onChanged)
        m_onChanged(m_selectedIndex);

    Refresh();
}

void PianoRollPanel::OnCaptureLost(wxMouseCaptureLostEvent&)
{
    m_dragMode = Drag_None;
    m_hasPreview = false;
    Refresh();
}


void PianoRollPanel::OnKeyDown(wxKeyEvent& event)
{
    if (event.GetKeyCode() == WXK_DELETE &&
        m_song != NULL &&
        m_selectedIndex >= 0 &&
        m_selectedIndex <
            static_cast<int>(m_song->events.size()) &&
        m_onDelete)
    {
        m_onDelete();
        return;
    }

    event.Skip();
}
