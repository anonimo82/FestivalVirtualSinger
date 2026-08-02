#include "waveform_editor.h"

#include <wx/button.h>
#include <wx/dcbuffer.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    enum
    {
        ID_WaveZoomIn = wxID_HIGHEST + 500,
        ID_WaveZoomOut,
        ID_WaveFit,
        ID_WaveClearSelection,
        ID_WaveScroll,
        ID_WavePlaybackTimer
    };

    short ReadS16(const unsigned char* p)
    {
        return static_cast<short>(p[0] | (p[1] << 8));
    }

    int ReadS24(const unsigned char* p)
    {
        int value = static_cast<int>(p[0]) |
                    (static_cast<int>(p[1]) << 8) |
                    (static_cast<int>(p[2]) << 16);
        if (value & 0x00800000)
            value |= 0xFF000000;
        return value;
    }

    int ReadS32(const unsigned char* p)
    {
        return static_cast<int>(
            static_cast<unsigned int>(p[0]) |
            (static_cast<unsigned int>(p[1]) << 8) |
            (static_cast<unsigned int>(p[2]) << 16) |
            (static_cast<unsigned int>(p[3]) << 24));
    }

    float DecodeChannelSample(const unsigned char* data,
                              unsigned short format,
                              unsigned short bits)
    {
        if (format == 1)
        {
            if (bits == 8)
                return (static_cast<int>(data[0]) - 128) / 128.0f;
            if (bits == 16)
                return ReadS16(data) / 32768.0f;
            if (bits == 24)
                return ReadS24(data) / 8388608.0f;
            if (bits == 32)
                return ReadS32(data) / 2147483648.0f;
        }
        else if (format == 3)
        {
            if (bits == 32)
            {
                float value = 0.0f;
                std::memcpy(&value, data, sizeof(value));
                return value;
            }
            if (bits == 64)
            {
                double value = 0.0;
                std::memcpy(&value, data, sizeof(value));
                return static_cast<float>(value);
            }
        }
        return 0.0f;
    }

    wxString FormatTime(double seconds)
    {
        if (seconds < 0.0)
            seconds = 0.0;
        const int minutes = static_cast<int>(seconds / 60.0);
        const double remainder = seconds - minutes * 60.0;
        return wxString::Format(wxT("%02d:%06.3f"), minutes, remainder);
    }
}

class WaveformCanvas : public wxPanel
{
public:
    WaveformCanvas(WaveformEditorPanel* owner, wxWindow* parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 260),
                  wxBORDER_SIMPLE),
          m_owner(owner), m_sampleRate(0), m_frameCount(0),
          m_visibleStart(0), m_visibleFrames(0), m_zoom(1.0),
          m_selecting(false), m_hasSelection(false),
          m_selectionStart(0), m_selectionEnd(0),
          m_playheadFrame(0), m_showPlayhead(false),
          m_showSliceMarkers(false), m_sliceStart(0), m_loopIn(0),
          m_loopOut(0), m_sliceEnd(0)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(wxColour(250, 251, 253));
        Bind(wxEVT_PAINT, &WaveformCanvas::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &WaveformCanvas::OnLeftDown, this);
        Bind(wxEVT_LEFT_UP, &WaveformCanvas::OnLeftUp, this);
        Bind(wxEVT_MOTION, &WaveformCanvas::OnMotion, this);
        Bind(wxEVT_MOUSEWHEEL, &WaveformCanvas::OnMouseWheel, this);
        Bind(wxEVT_SIZE, &WaveformCanvas::OnSize, this);
    }

    bool Load(const SamplePoolItem* item, wxString* errorMessage)
    {
        Clear();
        if (!item)
            return true;

        wxFFile file(item->filePath, wxT("rb"));
        if (!file.IsOpened())
        {
            if (errorMessage)
                *errorMessage = wxT("Unable to open the WAV for waveform analysis.");
            return false;
        }

        if (!file.Seek(static_cast<wxFileOffset>(item->wav.dataOffset), wxFromStart))
        {
            if (errorMessage)
                *errorMessage = wxT("Unable to seek to the WAV audio data.");
            return false;
        }

        const unsigned int bytesPerSample = item->wav.bitsPerSample / 8;
        const unsigned int bytesPerFrame = bytesPerSample * item->wav.channelCount;
        if (bytesPerSample == 0 || bytesPerFrame == 0)
        {
            if (errorMessage)
                *errorMessage = wxT("Invalid WAV frame size.");
            return false;
        }

        const size_t frameCount = static_cast<size_t>(item->wav.frameCount);
        std::vector<unsigned char> raw(static_cast<size_t>(item->wav.dataBytes));
        if (!raw.empty() && file.Read(&raw[0], raw.size()) != raw.size())
        {
            if (errorMessage)
                *errorMessage = wxT("The WAV audio data is truncated.");
            return false;
        }

        m_samples.resize(frameCount);
        for (size_t frame = 0; frame < frameCount; ++frame)
        {
            const unsigned char* frameData = &raw[frame * bytesPerFrame];
            double mixed = 0.0;
            for (unsigned int channel = 0; channel < item->wav.channelCount; ++channel)
            {
                mixed += DecodeChannelSample(
                    frameData + channel * bytesPerSample,
                    item->wav.audioFormat,
                    item->wav.bitsPerSample);
            }
            mixed /= item->wav.channelCount;
            if (mixed > 1.0) mixed = 1.0;
            if (mixed < -1.0) mixed = -1.0;
            m_samples[frame] = static_cast<float>(mixed);
        }

        m_sampleRate = item->wav.sampleRate;
        m_frameCount = item->wav.frameCount;
        Fit();
        return true;
    }

    void Clear()
    {
        m_samples.clear();
        m_sampleRate = 0;
        m_frameCount = 0;
        m_visibleStart = 0;
        m_visibleFrames = 0;
        m_zoom = 1.0;
        m_selecting = false;
        m_hasSelection = false;
        m_selectionStart = 0;
        m_selectionEnd = 0;
        m_showPlayhead = false;
        m_playheadFrame = 0;
        m_showSliceMarkers = false;
        m_autoSliceBoundaries.clear();
        Refresh();
    }

    void Fit()
    {
        m_zoom = 1.0;
        m_visibleStart = 0;
        m_visibleFrames = m_frameCount;
        NotifyViewChanged();
        Refresh();
    }

    void ZoomBy(double factor)
    {
        if (m_frameCount == 0)
            return;
        const unsigned long long oldVisible = EffectiveVisibleFrames();
        const unsigned long long centre = m_visibleStart + oldVisible / 2;
        m_zoom *= factor;
        if (m_zoom < 1.0) m_zoom = 1.0;
        if (m_zoom > 1024.0) m_zoom = 1024.0;
        m_visibleFrames = static_cast<unsigned long long>(m_frameCount / m_zoom);
        if (m_visibleFrames < 64) m_visibleFrames = std::min<unsigned long long>(64, m_frameCount);
        if (m_visibleFrames > m_frameCount) m_visibleFrames = m_frameCount;
        m_visibleStart = centre > m_visibleFrames / 2 ? centre - m_visibleFrames / 2 : 0;
        ClampView();
        NotifyViewChanged();
        Refresh();
    }

    void SetScrollFraction(double fraction)
    {
        if (m_frameCount == 0)
            return;
        if (fraction < 0.0) fraction = 0.0;
        if (fraction > 1.0) fraction = 1.0;
        const unsigned long long visible = EffectiveVisibleFrames();
        const unsigned long long maximum = m_frameCount > visible ? m_frameCount - visible : 0;
        m_visibleStart = static_cast<unsigned long long>(maximum * fraction);
        ClampView();
        Refresh();
    }

    double GetScrollFraction() const
    {
        const unsigned long long visible = EffectiveVisibleFrames();
        const unsigned long long maximum = m_frameCount > visible ? m_frameCount - visible : 0;
        if (maximum == 0)
            return 0.0;
        return static_cast<double>(m_visibleStart) / maximum;
    }

    double GetVisibleFraction() const
    {
        if (m_frameCount == 0)
            return 1.0;
        return static_cast<double>(EffectiveVisibleFrames()) / m_frameCount;
    }

    double GetZoom() const { return m_zoom; }
    unsigned int GetSampleRate() const { return m_sampleRate; }
    unsigned long long GetFrameCount() const { return m_frameCount; }

    bool HasSelection() const { return m_hasSelection; }
    unsigned long long SelectionStart() const { return std::min(m_selectionStart, m_selectionEnd); }
    unsigned long long SelectionEnd() const { return std::max(m_selectionStart, m_selectionEnd); }

    void ClearSelection()
    {
        m_hasSelection = false;
        m_selectionStart = m_selectionEnd = 0;
        if (m_owner) m_owner->OnCanvasSelectionChanged();
        Refresh();
    }

    void SetPlayheadSeconds(double seconds)
    {
        if (m_sampleRate == 0 || m_frameCount == 0)
            return;
        unsigned long long frame = static_cast<unsigned long long>(seconds * m_sampleRate);
        if (frame > m_frameCount) frame = m_frameCount;
        m_playheadFrame = frame;
        m_showPlayhead = true;
        Refresh();
    }

    void HidePlayhead()
    {
        m_showPlayhead = false;
        Refresh();
    }

    void SetSliceMarkers(bool visible, unsigned long long startFrame,
                         unsigned long long loopInFrame,
                         unsigned long long loopOutFrame,
                         unsigned long long endFrame)
    {
        m_showSliceMarkers = visible;
        m_sliceStart = startFrame;
        m_loopIn = loopInFrame;
        m_loopOut = loopOutFrame;
        m_sliceEnd = endFrame;
        Refresh();
    }

    void SetAutoSlicePreview(const std::vector<unsigned long long>& boundaries)
    {
        m_autoSliceBoundaries = boundaries;
        Refresh();
    }

    void ClearAutoSlicePreview()
    {
        m_autoSliceBoundaries.clear();
        Refresh();
    }

private:
    unsigned long long EffectiveVisibleFrames() const
    {
        return m_visibleFrames == 0 ? m_frameCount : m_visibleFrames;
    }

    void ClampView()
    {
        const unsigned long long visible = EffectiveVisibleFrames();
        if (visible >= m_frameCount)
            m_visibleStart = 0;
        else if (m_visibleStart + visible > m_frameCount)
            m_visibleStart = m_frameCount - visible;
    }

    unsigned long long XToFrame(int x) const
    {
        const int width = std::max(1, GetClientSize().GetWidth());
        if (x < 0) x = 0;
        if (x > width) x = width;
        const double fraction = static_cast<double>(x) / width;
        return m_visibleStart + static_cast<unsigned long long>(fraction * EffectiveVisibleFrames());
    }

    int FrameToX(unsigned long long frame) const
    {
        const int width = std::max(1, GetClientSize().GetWidth());
        const unsigned long long visible = EffectiveVisibleFrames();
        if (visible == 0 || frame <= m_visibleStart)
            return 0;
        if (frame >= m_visibleStart + visible)
            return width;
        return static_cast<int>((frame - m_visibleStart) * width / visible);
    }

    void NotifyViewChanged()
    {
        if (m_owner) m_owner->OnCanvasViewChanged();
    }

    void OnPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        const wxSize size = GetClientSize();
        dc.SetBackground(wxBrush(wxColour(250, 251, 253)));
        dc.Clear();

        const int width = size.GetWidth();
        const int height = size.GetHeight();
        const int centreY = height / 2;
        dc.SetPen(wxPen(wxColour(205, 211, 220)));
        dc.DrawLine(0, centreY, width, centreY);

        if (m_samples.empty() || width <= 0 || height <= 0)
        {
            dc.SetTextForeground(wxColour(100, 108, 120));
            dc.DrawLabel(wxT("Select a Sample Pool source to display its waveform."),
                         wxRect(0, 0, width, height), wxALIGN_CENTER);
            return;
        }

        if (m_hasSelection)
        {
            const int left = FrameToX(SelectionStart());
            const int right = FrameToX(SelectionEnd());
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(wxColour(210, 225, 244)));
            dc.DrawRectangle(left, 0, std::max(1, right - left), height);
        }

        dc.SetPen(wxPen(wxColour(43, 92, 145)));
        const unsigned long long visible = EffectiveVisibleFrames();
        for (int x = 0; x < width; ++x)
        {
            const unsigned long long first = m_visibleStart + visible * x / width;
            unsigned long long last = m_visibleStart + visible * (x + 1) / width;
            if (last <= first) last = first + 1;
            if (last > m_frameCount) last = m_frameCount;
            float minimum = 1.0f;
            float maximum = -1.0f;
            for (unsigned long long frame = first; frame < last; ++frame)
            {
                const float value = m_samples[static_cast<size_t>(frame)];
                if (value < minimum) minimum = value;
                if (value > maximum) maximum = value;
            }
            const int top = centreY - static_cast<int>(maximum * (height * 0.44));
            const int bottom = centreY - static_cast<int>(minimum * (height * 0.44));
            dc.DrawLine(x, top, x, bottom);
        }

        if (!m_autoSliceBoundaries.empty())
        {
            dc.SetPen(wxPen(wxColour(32, 160, 150), 1, wxPENSTYLE_SHORT_DASH));
            for (size_t i = 0; i < m_autoSliceBoundaries.size(); ++i)
            {
                const int x = FrameToX(m_autoSliceBoundaries[i]);
                dc.DrawLine(x, 0, x, height);
            }
        }

        if (m_showSliceMarkers)
        {
            struct Marker { unsigned long long frame; wxColour colour; const wxChar* label; };
            const Marker markers[] = {
                {m_sliceStart, wxColour(30, 125, 70), wxT("S")},
                {m_loopIn, wxColour(230, 145, 25), wxT("LI")},
                {m_loopOut, wxColour(210, 85, 25), wxT("LO")},
                {m_sliceEnd, wxColour(135, 50, 155), wxT("E")}
            };
            for (size_t i = 0; i < 4; ++i)
            {
                const int x = FrameToX(markers[i].frame);
                dc.SetPen(wxPen(markers[i].colour, 2));
                dc.DrawLine(x, 0, x, height);
                dc.SetTextForeground(markers[i].colour);
                dc.DrawText(markers[i].label, x + 3, 20 + static_cast<int>(i) * 15);
            }
        }

        if (m_showPlayhead)
        {
            const int x = FrameToX(m_playheadFrame);
            dc.SetPen(wxPen(wxColour(196, 50, 50), 2));
            dc.DrawLine(x, 0, x, height);
        }

        dc.SetTextForeground(wxColour(75, 83, 95));
        const double startSeconds = m_sampleRate ? static_cast<double>(m_visibleStart) / m_sampleRate : 0.0;
        const double endSeconds = m_sampleRate ? static_cast<double>(m_visibleStart + visible) / m_sampleRate : 0.0;
        dc.DrawText(FormatTime(startSeconds), 5, 4);
        const wxString endText = FormatTime(endSeconds);
        const wxSize extent = dc.GetTextExtent(endText);
        dc.DrawText(endText, std::max(5, width - extent.GetWidth() - 5), 4);
    }

    void OnLeftDown(wxMouseEvent& event)
    {
        if (m_frameCount == 0)
            return;
        CaptureMouse();
        m_selecting = true;
        m_hasSelection = true;
        m_selectionStart = m_selectionEnd = XToFrame(event.GetX());
        if (m_owner) m_owner->OnCanvasSelectionChanged();
        Refresh();
    }

    void OnLeftUp(wxMouseEvent& event)
    {
        if (!m_selecting)
            return;
        m_selectionEnd = XToFrame(event.GetX());
        m_selecting = false;
        if (HasCapture()) ReleaseMouse();
        if (m_selectionStart == m_selectionEnd)
            m_hasSelection = false;
        if (m_owner) m_owner->OnCanvasSelectionChanged();
        Refresh();
    }

    void OnMotion(wxMouseEvent& event)
    {
        if (!m_selecting || !event.Dragging() || !event.LeftIsDown())
            return;
        m_selectionEnd = XToFrame(event.GetX());
        if (m_owner) m_owner->OnCanvasSelectionChanged();
        Refresh();
    }

    void OnMouseWheel(wxMouseEvent& event)
    {
        ZoomBy(event.GetWheelRotation() > 0 ? 1.5 : (1.0 / 1.5));
    }

    void OnSize(wxSizeEvent& event)
    {
        Refresh();
        event.Skip();
    }

    WaveformEditorPanel* m_owner;
    std::vector<float> m_samples;
    unsigned int m_sampleRate;
    unsigned long long m_frameCount;
    unsigned long long m_visibleStart;
    unsigned long long m_visibleFrames;
    double m_zoom;
    bool m_selecting;
    bool m_hasSelection;
    unsigned long long m_selectionStart;
    unsigned long long m_selectionEnd;
    unsigned long long m_playheadFrame;
    bool m_showPlayhead;
    bool m_showSliceMarkers;
    unsigned long long m_sliceStart;
    unsigned long long m_loopIn;
    unsigned long long m_loopOut;
    unsigned long long m_sliceEnd;
    std::vector<unsigned long long> m_autoSliceBoundaries;
};

WaveformEditorPanel::WaveformEditorPanel(wxWindow* parent, wxWindowID id)
    : wxPanel(parent, id),
      m_canvas(NULL), m_scrollBar(NULL), m_positionLabel(NULL),
      m_selectionLabel(NULL), m_playbackTimer(this, ID_WavePlaybackTimer),
      m_playbackActive(false)
{
    SetBackgroundColour(wxColour(242, 244, 247));
    wxStaticBoxSizer* box = new wxStaticBoxSizer(wxVERTICAL, this, wxT("Waveform editor"));

    wxBoxSizer* toolbar = new wxBoxSizer(wxHORIZONTAL);
    toolbar->Add(new wxButton(this, ID_WaveZoomIn, wxT("Zoom +")), 0, wxRIGHT, 5);
    toolbar->Add(new wxButton(this, ID_WaveZoomOut, wxT("Zoom -")), 0, wxRIGHT, 5);
    toolbar->Add(new wxButton(this, ID_WaveFit, wxT("Fit")), 0, wxRIGHT, 5);
    toolbar->Add(new wxButton(this, ID_WaveClearSelection, wxT("Clear selection")), 0, wxRIGHT, 12);
    m_positionLabel = new wxStaticText(this, wxID_ANY, wxT("Zoom 1.00x"));
    toolbar->Add(m_positionLabel, 0, wxALIGN_CENTER_VERTICAL);
    toolbar->AddStretchSpacer();
    m_selectionLabel = new wxStaticText(this, wxID_ANY, wxT("No selection"));
    toolbar->Add(m_selectionLabel, 0, wxALIGN_CENTER_VERTICAL);
    box->Add(toolbar, 0, wxEXPAND | wxALL, 6);

    m_canvas = new WaveformCanvas(this, this);
    box->Add(m_canvas, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

    m_scrollBar = new wxScrollBar(this, ID_WaveScroll, wxDefaultPosition,
                                  wxDefaultSize, wxSB_HORIZONTAL);
    m_scrollBar->SetScrollbar(0, 1000, 1000, 1000);
    box->Add(m_scrollBar, 0, wxEXPAND | wxALL, 6);

    SetSizer(box);

    Bind(wxEVT_BUTTON, &WaveformEditorPanel::OnZoomIn, this, ID_WaveZoomIn);
    Bind(wxEVT_BUTTON, &WaveformEditorPanel::OnZoomOut, this, ID_WaveZoomOut);
    Bind(wxEVT_BUTTON, &WaveformEditorPanel::OnFit, this, ID_WaveFit);
    Bind(wxEVT_BUTTON, &WaveformEditorPanel::OnClearSelection, this, ID_WaveClearSelection);
    Bind(wxEVT_SCROLL_THUMBTRACK, &WaveformEditorPanel::OnScroll, this, ID_WaveScroll);
    Bind(wxEVT_SCROLL_CHANGED, &WaveformEditorPanel::OnScroll, this, ID_WaveScroll);
    Bind(wxEVT_SCROLL_PAGEUP, &WaveformEditorPanel::OnScroll, this, ID_WaveScroll);
    Bind(wxEVT_SCROLL_PAGEDOWN, &WaveformEditorPanel::OnScroll, this, ID_WaveScroll);
    Bind(wxEVT_SCROLL_LINEUP, &WaveformEditorPanel::OnScroll, this, ID_WaveScroll);
    Bind(wxEVT_SCROLL_LINEDOWN, &WaveformEditorPanel::OnScroll, this, ID_WaveScroll);
    Bind(wxEVT_TIMER, &WaveformEditorPanel::OnPlaybackTimer, this, ID_WavePlaybackTimer);
}

bool WaveformEditorPanel::SetSample(const SamplePoolItem* item, wxString* errorMessage)
{
    StopPlayback();
    const bool loaded = m_canvas->Load(item, errorMessage);
    UpdateControls();
    return loaded;
}

void WaveformEditorPanel::ClearSample()
{
    StopPlayback();
    m_canvas->Clear();
    UpdateControls();
}

void WaveformEditorPanel::StartPlayback()
{
    if (m_canvas->GetFrameCount() == 0)
        return;
    m_playbackClock.Start(0);
    m_playbackActive = true;
    m_playbackTimer.Start(30);
    m_canvas->SetPlayheadSeconds(0.0);
}

void WaveformEditorPanel::SetSliceMarkers(bool visible, unsigned long long startFrame,
                                              unsigned long long loopInFrame,
                                              unsigned long long loopOutFrame,
                                              unsigned long long endFrame)
{
    if (m_canvas) m_canvas->SetSliceMarkers(visible, startFrame, loopInFrame, loopOutFrame, endFrame);
}

void WaveformEditorPanel::SetAutoSlicePreview(const std::vector<unsigned long long>& boundaries)
{
    if (m_canvas) m_canvas->SetAutoSlicePreview(boundaries);
}

void WaveformEditorPanel::ClearAutoSlicePreview()
{
    if (m_canvas) m_canvas->ClearAutoSlicePreview();
}

void WaveformEditorPanel::StopPlayback()
{
    m_playbackActive = false;
    m_playbackTimer.Stop();
    if (m_canvas)
        m_canvas->HidePlayhead();
}

bool WaveformEditorPanel::HasSelection() const { return m_canvas->HasSelection(); }
unsigned long long WaveformEditorPanel::GetSelectionStartFrame() const { return m_canvas->SelectionStart(); }
unsigned long long WaveformEditorPanel::GetSelectionEndFrame() const { return m_canvas->SelectionEnd(); }

void WaveformEditorPanel::OnCanvasViewChanged() { UpdateControls(); }
void WaveformEditorPanel::OnCanvasSelectionChanged() { UpdateControls(); }

void WaveformEditorPanel::OnZoomIn(wxCommandEvent&) { m_canvas->ZoomBy(2.0); }
void WaveformEditorPanel::OnZoomOut(wxCommandEvent&) { m_canvas->ZoomBy(0.5); }
void WaveformEditorPanel::OnFit(wxCommandEvent&) { m_canvas->Fit(); }
void WaveformEditorPanel::OnClearSelection(wxCommandEvent&) { m_canvas->ClearSelection(); }

void WaveformEditorPanel::OnScroll(wxScrollEvent& event)
{
    const int range = std::max(1, m_scrollBar->GetRange() - m_scrollBar->GetThumbSize());
    m_canvas->SetScrollFraction(static_cast<double>(event.GetPosition()) / range);
}

void WaveformEditorPanel::OnPlaybackTimer(wxTimerEvent&)
{
    if (!m_playbackActive || m_canvas->GetSampleRate() == 0)
        return;
    const double seconds = m_playbackClock.Time() / 1000.0;
    const double duration = static_cast<double>(m_canvas->GetFrameCount()) / m_canvas->GetSampleRate();
    if (seconds >= duration)
    {
        StopPlayback();
        return;
    }
    m_canvas->SetPlayheadSeconds(seconds);
}

void WaveformEditorPanel::UpdateControls()
{
    if (!m_canvas || !m_scrollBar)
        return;
    const int range = 1000;
    int thumb = static_cast<int>(m_canvas->GetVisibleFraction() * range);
    if (thumb < 1) thumb = 1;
    if (thumb > range) thumb = range;
    const int maximumPosition = range - thumb;
    const int position = static_cast<int>(m_canvas->GetScrollFraction() * maximumPosition);
    m_scrollBar->SetScrollbar(position, thumb, range, std::max(1, thumb));

    m_positionLabel->SetLabel(wxString::Format(wxT("Zoom %.2fx"), m_canvas->GetZoom()));
    if (m_canvas->HasSelection() && m_canvas->GetSampleRate() > 0)
    {
        const unsigned long long start = m_canvas->SelectionStart();
        const unsigned long long end = m_canvas->SelectionEnd();
        m_selectionLabel->SetLabel(wxString::Format(
            wxT("Selection: %s - %s (%llu frames)"),
            FormatTime(static_cast<double>(start) / m_canvas->GetSampleRate()).c_str(),
            FormatTime(static_cast<double>(end) / m_canvas->GetSampleRate()).c_str(),
            end - start));
    }
    else
    {
        m_selectionLabel->SetLabel(wxT("No selection"));
    }
    Layout();
}
