#pragma once

#include <wx/string.h>
#include <vector>

struct AudioSlice
{
    AudioSlice()
        : startFrame(0), loopInFrame(0), loopOutFrame(0), endFrame(0),
          rootMidiNote(60), loopEnabled(true)
    {
    }

    wxString id;
    wxString sourceId;
    wxString name;
    unsigned long long startFrame;
    unsigned long long loopInFrame;
    unsigned long long loopOutFrame;
    unsigned long long endFrame;
    int rootMidiNote;
    bool loopEnabled;
};

class SliceModel
{
public:
    const std::vector<AudioSlice>& Items() const { return m_items; }
    std::vector<AudioSlice>& Items() { return m_items; }

    const AudioSlice* GetAt(size_t index) const;
    AudioSlice* GetAt(size_t index);
    size_t GetCount() const { return m_items.size(); }

    bool AddFromSelection(const wxString& sourceId,
                          unsigned long long startFrame,
                          unsigned long long endFrame,
                          AudioSlice* added,
                          wxString* errorMessage);
    bool RemoveAt(size_t index);
    void RemoveForSource(const wxString& sourceId);

private:
    wxString MakeId(const wxString& sourceId,
                    unsigned long long startFrame,
                    unsigned long long endFrame) const;
    std::vector<AudioSlice> m_items;
};
