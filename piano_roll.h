#pragma once

#include "song_model.h"

#include <wx/scrolwin.h>
#include <wx/timer.h>
#include <functional>
#include <vector>

class PianoRollPanel : public wxScrolledWindow
{
public:
    typedef std::function<void(int)> SelectionCallback;
    typedef std::function<void()> BeforeChangeCallback;
    typedef std::function<void(int)> ChangedCallback;
    typedef std::function<void(const SingingEvent&, const wxString&)> PreviewCallback;
    typedef std::function<void()> DeleteCallback;

    explicit PianoRollPanel(wxWindow* parent);

    void SetSong(SingingSong* song);
    void SetSelectedIndex(int index);
    int GetSelectedIndex() const;
    void SetSnapBeats(double value);

    void SetSelectionCallback(const SelectionCallback& callback);
    void SetBeforeChangeCallback(const BeforeChangeCallback& callback);
    void SetChangedCallback(const ChangedCallback& callback);
    void SetPreviewCallback(const PreviewCallback& callback);
    void SetDeleteCallback(const DeleteCallback& callback);

private:
    enum DragMode
    {
        Drag_None,
        Drag_Move,
        Drag_Resize
    };

    struct NoteGeometry
    {
        wxRect rect;
        double startBeat;
    };

    void RebuildGeometry();
    int HitTestNote(const wxPoint& logicalPoint) const;
    int PitchRow(const wxString& pitch) const;
    wxString RowPitch(int row) const;
    int InsertionIndexFromBeat(double beat, int movingIndex) const;
    double EffectiveSnapBeats() const;
    double Snap(double value) const;
    int SnapPixelDelta(int pixelDelta) const;
    void RequestPreview(const SingingEvent& event, const wxString& editKind);

    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMotion(wxMouseEvent& event);
    void OnLeftDClick(wxMouseEvent& event);
    void OnCaptureLost(wxMouseCaptureLostEvent& event);
    void OnKeyDown(wxKeyEvent& event);

    SingingSong* m_song;
    std::vector<wxString> m_pitches;
    std::vector<NoteGeometry> m_geometry;

    int m_selectedIndex;
    DragMode m_dragMode;
    wxPoint m_dragStart;
    wxRect m_originRect;
    int m_originPitchRow;
    double m_originBeats;
    double m_originStartBeat;

    SingingEvent m_previewEvent;
    wxRect m_previewRect;
    bool m_hasPreview;
    wxString m_lastPreviewSignature;

    int m_labelWidth;
    int m_rowHeight;
    int m_beatWidth;
    double m_snapBeats;

    SelectionCallback m_onSelection;
    BeforeChangeCallback m_onBeforeChange;
    ChangedCallback m_onChanged;
    PreviewCallback m_onPreview;
    DeleteCallback m_onDelete;
};
