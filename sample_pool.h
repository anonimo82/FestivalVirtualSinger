#pragma once

#include "song_model.h"

#include <wx/string.h>
#include <vector>

struct WavMetadata
{
    WavMetadata()
        : audioFormat(0), channelCount(0), sampleRate(0), bitsPerSample(0),
          dataBytes(0), dataOffset(0), frameCount(0), durationSeconds(0.0)
    {
    }

    unsigned short audioFormat;
    unsigned short channelCount;
    unsigned int sampleRate;
    unsigned short bitsPerSample;
    unsigned int dataBytes;
    unsigned long long dataOffset;
    unsigned long long frameCount;
    double durationSeconds;
};

struct SamplePoolItem
{
    SamplePoolItem()
        : generatedByFestival(false), sourceEventIndex(-1),
          sourceBeats(0.0), sourceBpm(0.0)
    {
    }

    wxString id;
    wxString filePath;
    wxString displayName;
    WavMetadata wav;

    bool generatedByFestival;
    int sourceEventIndex;
    wxString sourcePitch;
    wxString sourcePhoneme;
    double sourceBeats;
    wxString sourceVoice;
    double sourceBpm;
};

class SamplePool
{
public:
    bool AddWav(const wxString& filePath,
                SamplePoolItem* addedItem,
                wxString* errorMessage);

    bool AddFestivalWav(const wxString& filePath,
                        int sourceEventIndex,
                        const SingingEvent& sourceEvent,
                        const wxString& sourceVoice,
                        double sourceBpm,
                        SamplePoolItem* addedItem,
                        wxString* errorMessage);

    bool RemoveAt(size_t index);
    void Clear();

    size_t GetCount() const;
    const SamplePoolItem* GetAt(size_t index) const;
    SamplePoolItem* GetAtMutable(size_t index);

private:
    bool ReadWavMetadata(const wxString& filePath,
                         WavMetadata* metadata,
                         wxString* errorMessage) const;
    wxString MakeStableId(const wxString& filePath) const;

    std::vector<SamplePoolItem> m_items;
};
