#include "sample_pool.h"

#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/datetime.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace
{
    unsigned short ReadU16(const unsigned char* p)
    {
        return static_cast<unsigned short>(p[0] | (p[1] << 8));
    }

    unsigned int ReadU32(const unsigned char* p)
    {
        return static_cast<unsigned int>(p[0]) |
               (static_cast<unsigned int>(p[1]) << 8) |
               (static_cast<unsigned int>(p[2]) << 16) |
               (static_cast<unsigned int>(p[3]) << 24);
    }
}

bool SamplePool::AddWav(const wxString& filePath,
                        SamplePoolItem* addedItem,
                        wxString* errorMessage)
{
    WavMetadata metadata;
    if (!ReadWavMetadata(filePath, &metadata, errorMessage))
        return false;

    wxFileName normalized(filePath);
    normalized.Normalize(wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS);
    const wxString fullPath = normalized.GetFullPath();

    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].filePath.CmpNoCase(fullPath) == 0)
        {
            if (errorMessage)
                *errorMessage = wxT("This WAV is already in the Sample Pool.");
            return false;
        }
    }

    SamplePoolItem item;
    item.id = MakeStableId(fullPath);
    item.filePath = fullPath;
    item.displayName = normalized.GetFullName();
    item.wav = metadata;
    m_items.push_back(item);

    if (addedItem)
        *addedItem = item;
    return true;
}

bool SamplePool::AddFestivalWav(const wxString& filePath,
                                int sourceEventIndex,
                                const SingingEvent& sourceEvent,
                                const wxString& sourceVoice,
                                double sourceBpm,
                                SamplePoolItem* addedItem,
                                wxString* errorMessage)
{
    SamplePoolItem item;
    if (!AddWav(filePath, &item, errorMessage))
        return false;

    SamplePoolItem& stored = m_items.back();
    stored.generatedByFestival = true;
    stored.sourceEventIndex = sourceEventIndex;
    stored.sourcePitch = sourceEvent.pitch;
    stored.sourcePhoneme = sourceEvent.phoneme;
    stored.sourceBeats = sourceEvent.beats;
    stored.sourceVoice = sourceVoice;
    stored.sourceBpm = sourceBpm;

    if (addedItem)
        *addedItem = stored;
    return true;
}

bool SamplePool::RemoveAt(size_t index)
{
    if (index >= m_items.size())
        return false;
    m_items.erase(m_items.begin() + index);
    return true;
}

void SamplePool::Clear()
{
    m_items.clear();
}

size_t SamplePool::GetCount() const
{
    return m_items.size();
}

const SamplePoolItem* SamplePool::GetAt(size_t index) const
{
    if (index >= m_items.size())
        return NULL;
    return &m_items[index];
}

bool SamplePool::ReadWavMetadata(const wxString& filePath,
                                 WavMetadata* metadata,
                                 wxString* errorMessage) const
{
    if (!metadata)
        return false;

    wxFFile file(filePath, wxT("rb"));
    if (!file.IsOpened())
    {
        if (errorMessage)
            *errorMessage = wxT("Unable to open the selected file.");
        return false;
    }

    unsigned char header[12];
    if (file.Read(header, sizeof(header)) != sizeof(header) ||
        memcmp(header, "RIFF", 4) != 0 ||
        memcmp(header + 8, "WAVE", 4) != 0)
    {
        if (errorMessage)
            *errorMessage = wxT("The selected file is not a valid RIFF/WAVE file.");
        return false;
    }

    bool foundFormat = false;
    bool foundData = false;
    WavMetadata result;

    while (!file.Eof())
    {
        unsigned char chunkHeader[8];
        if (file.Read(chunkHeader, sizeof(chunkHeader)) != sizeof(chunkHeader))
            break;

        const unsigned int chunkSize = ReadU32(chunkHeader + 4);
        const wxString chunkId(reinterpret_cast<const char*>(chunkHeader),
                               wxConvISO8859_1, 4);

        if (chunkId == wxT("fmt "))
        {
            if (chunkSize < 16)
            {
                if (errorMessage)
                    *errorMessage = wxT("Invalid WAV format chunk.");
                return false;
            }

            std::vector<unsigned char> format(chunkSize);
            if (file.Read(&format[0], chunkSize) != chunkSize)
            {
                if (errorMessage)
                    *errorMessage = wxT("The WAV format chunk is truncated.");
                return false;
            }

            result.audioFormat = ReadU16(&format[0]);
            result.channelCount = ReadU16(&format[2]);
            result.sampleRate = ReadU32(&format[4]);
            result.bitsPerSample = ReadU16(&format[14]);
            foundFormat = true;
        }
        else if (chunkId == wxT("data"))
        {
            result.dataBytes = chunkSize;
            result.dataOffset = static_cast<unsigned long long>(file.Tell());
            foundData = true;
            if (!file.Seek(chunkSize, wxFromCurrent))
                break;
        }
        else
        {
            if (!file.Seek(chunkSize, wxFromCurrent))
                break;
        }

        if ((chunkSize & 1U) != 0)
            file.Seek(1, wxFromCurrent);
    }

    if (!foundFormat || !foundData || result.channelCount == 0 ||
        result.sampleRate == 0 || result.bitsPerSample == 0)
    {
        if (errorMessage)
            *errorMessage = wxT("The WAV does not contain usable format and audio data chunks.");
        return false;
    }

    if (result.audioFormat != 1 && result.audioFormat != 3)
    {
        if (errorMessage)
            *errorMessage = wxString::Format(
                wxT("Unsupported WAV encoding (%u). PCM and IEEE float are accepted."),
                static_cast<unsigned int>(result.audioFormat));
        return false;
    }

    const unsigned int bytesPerFrame =
        static_cast<unsigned int>(result.channelCount) *
        static_cast<unsigned int>(result.bitsPerSample / 8);
    if (bytesPerFrame == 0)
    {
        if (errorMessage)
            *errorMessage = wxT("Invalid WAV sample size.");
        return false;
    }

    result.frameCount = result.dataBytes / bytesPerFrame;
    result.durationSeconds =
        static_cast<double>(result.frameCount) /
        static_cast<double>(result.sampleRate);

    *metadata = result;
    return true;
}

wxString SamplePool::MakeStableId(const wxString& filePath) const
{
    unsigned long hash = 2166136261UL;
    for (size_t i = 0; i < filePath.length(); ++i)
    {
        hash ^= static_cast<unsigned long>(filePath[i].GetValue());
        hash *= 16777619UL;
    }

    wxString candidate = wxString::Format(wxT("sample-%08lx"), hash);
    wxString unique = candidate;
    unsigned int suffix = 2;

    bool collision = true;
    while (collision)
    {
        collision = false;
        for (size_t i = 0; i < m_items.size(); ++i)
        {
            if (m_items[i].id == unique)
            {
                collision = true;
                unique = wxString::Format(wxT("%s-%u"), candidate.c_str(), suffix++);
                break;
            }
        }
    }
    return unique;
}
