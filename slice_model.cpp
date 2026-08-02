#include "slice_model.h"

#include <ctime>

const AudioSlice* SliceModel::GetAt(size_t index) const
{
    return index < m_items.size() ? &m_items[index] : NULL;
}

AudioSlice* SliceModel::GetAt(size_t index)
{
    return index < m_items.size() ? &m_items[index] : NULL;
}

wxString SliceModel::MakeId(const wxString& sourceId,
                            unsigned long long startFrame,
                            unsigned long long endFrame) const
{
    static unsigned long sequence = 0;
    ++sequence;

    unsigned long hash = 2166136261u;
    const unsigned long timestamp = static_cast<unsigned long>(std::time(NULL));
    const wxString seed = wxString::Format(wxT("%s|%llu|%llu|%lu|%lu"),
                                            sourceId.c_str(), startFrame, endFrame,
                                            timestamp, sequence);
    for (size_t i = 0; i < seed.length(); ++i)
    {
        hash ^= static_cast<unsigned long>(seed[i]);
        hash *= 16777619u;
    }
    return wxString::Format(wxT("slice-%08lx-%04lx"), hash, sequence & 0xffffu);
}

bool SliceModel::AddFromSelection(const wxString& sourceId,
                                  unsigned long long startFrame,
                                  unsigned long long endFrame,
                                  AudioSlice* added,
                                  wxString* errorMessage)
{
    if (sourceId.IsEmpty() || endFrame <= startFrame)
    {
        if (errorMessage) *errorMessage = wxT("Create a non-empty waveform selection first.");
        return false;
    }

    AudioSlice slice;
    slice.id = MakeId(sourceId, startFrame, endFrame);
    slice.sourceId = sourceId;
    slice.name = wxString::Format(wxT("Slice %u"), static_cast<unsigned int>(m_items.size() + 1));
    slice.startFrame = startFrame;
    slice.endFrame = endFrame;
    const unsigned long long length = endFrame - startFrame;
    slice.loopInFrame = startFrame + length / 3;
    slice.loopOutFrame = startFrame + (length * 2) / 3;
    if (slice.loopOutFrame <= slice.loopInFrame)
        slice.loopOutFrame = slice.loopInFrame + 1;
    if (slice.loopOutFrame > slice.endFrame)
        slice.loopOutFrame = slice.endFrame;

    m_items.push_back(slice);
    if (added) *added = slice;
    return true;
}

bool SliceModel::RemoveAt(size_t index)
{
    if (index >= m_items.size()) return false;
    m_items.erase(m_items.begin() + index);
    return true;
}

void SliceModel::RemoveForSource(const wxString& sourceId)
{
    for (size_t i = m_items.size(); i > 0; --i)
    {
        if (m_items[i - 1].sourceId == sourceId)
            m_items.erase(m_items.begin() + (i - 1));
    }
}
