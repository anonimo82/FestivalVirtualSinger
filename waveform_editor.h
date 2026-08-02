#pragma once

#include "sample_pool.h"

#include <wx/panel.h>
#include <wx/scrolbar.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/stopwatch.h>
#include <vector>

class WaveformCanvas;

class WaveformEditorPanel : public wxPanel
{
public:
    WaveformEditorPanel(wxWindow* parent, wxWindowID id = wxID_ANY);

    bool SetSample(const SamplePoolItem* item, wxString* errorMessage);
    void ClearSample();

    void StartPlayback();
    void StopPlayback();

    void SetSliceMarkers(bool visible, unsigned long long startFrame,
                         unsigned long long loopInFrame,
                         unsigned long long loopOutFrame,
                         unsigned long long endFrame);

    bool HasSelection() const;
    unsigned long long GetSelectionStartFrame() const;
    unsigned long long GetSelectionEndFrame() const;

    void OnCanvasViewChanged();
    void OnCanvasSelectionChanged();

private:
    void OnZoomIn(wxCommandEvent& event);
    void OnZoomOut(wxCommandEvent& event);
    void OnFit(wxCommandEvent& event);
    void OnClearSelection(wxCommandEvent& event);
    void OnScroll(wxScrollEvent& event);
    void OnPlaybackTimer(wxTimerEvent& event);
    void UpdateControls();

    WaveformCanvas* m_canvas;
    wxScrollBar* m_scrollBar;
    wxStaticText* m_positionLabel;
    wxStaticText* m_selectionLabel;
    wxTimer m_playbackTimer;
    wxStopWatch m_playbackClock;
    bool m_playbackActive;
};
