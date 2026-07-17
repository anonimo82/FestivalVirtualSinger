#pragma once

#include <wx/string.h>
#include <vector>

struct SingingEvent
{
    SingingEvent();
    SingingEvent(const wxString& pitchValue,
                 double beatsValue,
                 const wxString& phonemeValue);

    wxString pitch;
    double beats;
    wxString phoneme;
};

struct SingingSong
{
    SingingSong();

    wxString title;
    double bpm;
    wxString voice;
    std::vector<SingingEvent> events;

    static SingingSong Demo();
};

double FestivalSingingBpm(double musicalBpm);
bool IsPauseEvent(const SingingEvent& event);
void ApplyEventEditPreservingFollowingTiming(
    std::vector<SingingEvent>* events,
    size_t index,
    const SingingEvent& updatedEvent);
wxString NormalizePitch(const wxString& pitch);
int PitchToMidi(const wxString& pitch);
wxString MidiToPitch(int midi);
std::vector<wxString> BuildPitchList(int lowestMidi, int highestMidi);
wxString SanitizeFestivalVoice(const wxString& voice);
wxString XmlEscape(const wxString& value);
wxString SchemeEscape(const wxString& value);
wxString BuildSingingXml(const std::vector<SingingEvent>& events,
                         double musicalBpm);
wxString BuildSingingSongFileXml(const SingingSong& song);
bool ParseSingingSongFileXml(const wxString& xml,
                             SingingSong* song,
                             wxString* errorMessage);
wxString BuildDirectSingingScheme(const std::vector<SingingEvent>& events,
                                  double musicalBpm);
wxString BuildDirectSingingRenderScheme(
    const std::vector<SingingEvent>& events,
    double musicalBpm,
    const wxString& waveFilePath);
