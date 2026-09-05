#include "song_model.h"

#include <wx/strconv.h>
#include <algorithm>
#include <cmath>

namespace
{
    const wxChar* const NOTE_NAMES[] =
    {
        wxT("C"), wxT("C#"), wxT("D"), wxT("D#"), wxT("E"), wxT("F"),
        wxT("F#"), wxT("G"), wxT("G#"), wxT("A"), wxT("A#"), wxT("B")
    };

    int NoteOffset(const wxString& name)
    {
        if (name == wxT("C"))  return 0;
        if (name == wxT("C#") || name == wxT("Db")) return 1;
        if (name == wxT("D"))  return 2;
        if (name == wxT("D#") || name == wxT("Eb")) return 3;
        if (name == wxT("E"))  return 4;
        if (name == wxT("F"))  return 5;
        if (name == wxT("F#") || name == wxT("Gb")) return 6;
        if (name == wxT("G"))  return 7;
        if (name == wxT("G#") || name == wxT("Ab")) return 8;
        if (name == wxT("A"))  return 9;
        if (name == wxT("A#") || name == wxT("Bb")) return 10;
        if (name == wxT("B"))  return 11;
        return -1;
    }

    wxString Number(double value)
    {
        wxString text = wxString::Format(wxT("%.6f"), value);
        while (text.Contains(wxT(".")) && text.EndsWith(wxT("0")))
            text.RemoveLast();
        if (text.EndsWith(wxT(".")))
            text.RemoveLast();
        return text;
    }

    size_t FindNoCase(const wxString& text,
                      const wxString& needle,
                      size_t start = 0)
    {
        return text.Upper().find(needle.Upper(), start);
    }

    size_t FindXmlTagEnd(const wxString& text, size_t start)
    {
        wxChar quote = 0;

        for (size_t i = start; i < text.Length(); ++i)
        {
            const wxChar ch = text[i];

            if (quote != 0)
            {
                if (ch == quote)
                    quote = 0;
                continue;
            }

            if (ch == wxT('"') || ch == wxT('\''))
            {
                quote = ch;
                continue;
            }

            if (ch == wxT('>'))
                return i;
        }

        return wxString::npos;
    }

    wxString XmlUnescape(const wxString& value)
    {
        wxString result = value;
        result.Replace(wxT("&quot;"), wxT("\""));
        result.Replace(wxT("&apos;"), wxT("'"));
        result.Replace(wxT("&lt;"), wxT("<"));
        result.Replace(wxT("&gt;"), wxT(">"));
        result.Replace(wxT("&amp;"), wxT("&"));
        return result;
    }

    wxString XmlCommentEscape(const wxString& value)
    {
        wxString result = value;
        result.Replace(wxT("--"), wxT("- -"));
        return XmlEscape(result);
    }

    bool IsAsciiAlphaNumeric(wxChar ch)
    {
        return (ch >= wxT('A') && ch <= wxT('Z')) ||
               (ch >= wxT('a') && ch <= wxT('z')) ||
               (ch >= wxT('0') && ch <= wxT('9'));
    }

    bool IsXmlWhitespace(wxChar ch)
    {
        return ch == wxT(' ') ||
               ch == wxT('\t') ||
               ch == wxT('\r') ||
               ch == wxT('\n');
    }

    bool ExtractXmlAttribute(const wxString& tag,
                             const wxString& attributeName,
                             wxString* value)
    {
        const wxString upper = tag.Upper();
        const wxString wanted = attributeName.Upper();

        size_t search = 0;
        while (search < upper.Length())
        {
            const size_t found = upper.find(wanted, search);
            if (found == wxString::npos)
                return false;

            const bool validLeft =
                found == 0 ||
                !(IsAsciiAlphaNumeric(upper[found - 1]) ||
                  upper[found - 1] == wxT('_') ||
                  upper[found - 1] == wxT('-'));

            const size_t afterName = found + wanted.Length();
            const bool validRight =
                afterName >= upper.Length() ||
                !(IsAsciiAlphaNumeric(upper[afterName]) ||
                  upper[afterName] == wxT('_') ||
                  upper[afterName] == wxT('-'));

            if (!validLeft || !validRight)
            {
                search = afterName;
                continue;
            }

            size_t equals = afterName;
            while (equals < tag.Length() &&
                   IsXmlWhitespace(tag[equals]))
            {
                ++equals;
            }

            if (equals >= tag.Length() ||
                tag[equals] != wxT('='))
            {
                search = afterName;
                continue;
            }

            ++equals;
            while (equals < tag.Length() &&
                   IsXmlWhitespace(tag[equals]))
            {
                ++equals;
            }

            if (equals >= tag.Length() ||
                (tag[equals] != wxT('"') &&
                 tag[equals] != wxT('\'')))
            {
                search = afterName;
                continue;
            }

            const wxChar quote = tag[equals];
            const size_t end = tag.find(quote, equals + 1);
            if (end == wxString::npos)
                return false;

            if (value != NULL)
            {
                *value = XmlUnescape(
                    tag.Mid(equals + 1, end - equals - 1));
            }
            return true;
        }

        return false;
    }

    bool ParsePositiveNumber(const wxString& text, double* value)
    {
        wxString normalized = text;
        normalized.Trim(true);
        normalized.Trim(false);
        normalized.Replace(wxT(","), wxT("."));

        double parsed = 0.0;
        if (!normalized.ToDouble(&parsed) || parsed <= 0.0)
            return false;

        if (value != NULL)
            *value = parsed;
        return true;
    }

    bool DurationBeatsFromTag(const wxString& tag,
                              double musicalBpm,
                              double* beats)
    {
        wxString value;
        double parsed = 0.0;

        if (ExtractXmlAttribute(tag, wxT("BEATS"), &value) &&
            ParsePositiveNumber(value, &parsed))
        {
            if (beats != NULL)
                *beats = parsed;
            return true;
        }

        if (ExtractXmlAttribute(tag, wxT("SECONDS"), &value) &&
            ParsePositiveNumber(value, &parsed))
        {
            const double safeBpm =
                musicalBpm > 1.0 ? musicalBpm : 120.0;
            if (beats != NULL)
                *beats = parsed * safeBpm / 60.0;
            return true;
        }

        return false;
    }

    wxString PitchFromTag(const wxString& tag)
    {
        wxString value;

        if (ExtractXmlAttribute(tag, wxT("NOTE"), &value) &&
            !value.IsEmpty() &&
            value.Upper() != wxT("X"))
        {
            return NormalizePitch(value);
        }

        double frequency = 0.0;
        if (ExtractXmlAttribute(tag, wxT("FREQ"), &value) &&
            ParsePositiveNumber(value, &frequency))
        {
            const double midiValue =
                69.0 +
                12.0 *
                (std::log(frequency / 440.0) /
                 std::log(2.0));

            const int midi = static_cast<int>(
                std::floor(midiValue + 0.5));
            return MidiToPitch(midi);
        }

        return wxT("C4");
    }

    wxString EventXml(const SingingEvent& event)
    {
        const double beats =
            event.beats > 0.0 ? event.beats : 0.25;

        wxString xml;
        if (IsPauseEvent(event))
        {
            xml << wxT("  <REST BEATS=\"")
                << Number(beats)
                << wxT("\"></REST>\n");
            return xml;
        }

        wxString phoneme = event.phoneme;
        phoneme.Trim(true);
        phoneme.Trim(false);

        xml << wxT("  <PITCH NOTE=\"")
            << XmlEscape(NormalizePitch(event.pitch))
            << wxT("\">\n");
        xml << wxT("    <DURATION BEATS=\"")
            << Number(beats)
            << wxT("\">")
            << XmlEscape(phoneme)
            << wxT("</DURATION>\n");
        xml << wxT("  </PITCH>\n");
        return xml;
    }
}

SingingEvent::SingingEvent()
    : pitch(wxT("C4")),
      beats(1.0),
      phoneme(wxT("la"))
{
}

SingingEvent::SingingEvent(const wxString& pitchValue,
                           double beatsValue,
                           const wxString& phonemeValue)
    : pitch(NormalizePitch(pitchValue)),
      beats(beatsValue > 0.0 ? beatsValue : 0.25),
      phoneme(phonemeValue)
{
}

SingingSong::SingingSong()
    : title(wxT("New Song")),
      bpm(90.0),
      voice(wxT("kal_diphone"))
{
}

SingingSong SingingSong::Demo()
{
    SingingSong song;
    song.title = wxT("Do re mi");
    song.bpm = 90.0;
    song.voice = wxT("kal_diphone");
    song.events.push_back(SingingEvent(wxT("C4"), 1.0, wxT("doe")));
    song.events.push_back(SingingEvent(wxT("D4"), 1.0, wxT("ray")));
    song.events.push_back(SingingEvent(wxT("E4"), 1.0, wxT("me")));
    song.events.push_back(SingingEvent(wxT("F4"), 1.0, wxT("fah")));
    song.events.push_back(SingingEvent(wxT("G4"), 2.0, wxT("so")));
    return song;
}

double FestivalSingingBpm(double musicalBpm)
{
    // The singing-mode bundled with Festival uses a 50-second minute.
    return musicalBpm * 50.0 / 60.0;
}

bool IsPauseEvent(const SingingEvent& event)
{
    wxString phoneme = event.phoneme;
    phoneme.Trim(true);
    phoneme.Trim(false);
    return phoneme.IsEmpty();
}

void ApplyEventEditPreservingFollowingTiming(
    std::vector<SingingEvent>* events,
    size_t index,
    const SingingEvent& updatedEvent)
{
    if (events == NULL || index >= events->size())
        return;

    const double epsilon = 0.000001;
    const SingingEvent originalEvent = (*events)[index];

    (*events)[index] = updatedEvent;

    // Pauses are timeline spacers themselves. Editing one must therefore
    // move the following events normally. A final note also needs no spacer,
    // because there is nothing after it whose position must be preserved.
    if (IsPauseEvent(originalEvent) ||
        IsPauseEvent(updatedEvent) ||
        index + 1 >= events->size())
    {
        return;
    }

    const double removedBeats =
        originalEvent.beats - updatedEvent.beats;

    if (removedBeats > epsilon)
    {
        // The note became shorter: preserve every later start position by
        // filling the vacated interval with one pause.
        if (IsPauseEvent((*events)[index + 1]))
        {
            (*events)[index + 1].beats += removedBeats;
        }
        else
        {
            events->insert(
                events->begin() + index + 1,
                SingingEvent(
                    updatedEvent.pitch,
                    removedBeats,
                    wxEmptyString));
        }
        return;
    }

    if (removedBeats >= -epsilon)
        return;

    // The note became longer. Consume immediately following pauses first;
    // only the part exceeding the available silence moves later notes right.
    double requiredBeats = -removedBeats;
    size_t following = index + 1;

    while (requiredBeats > epsilon &&
           following < events->size() &&
           IsPauseEvent((*events)[following]))
    {
        SingingEvent& pause = (*events)[following];

        if (pause.beats > requiredBeats + epsilon)
        {
            pause.beats -= requiredBeats;
            requiredBeats = 0.0;
        }
        else
        {
            requiredBeats -= pause.beats;
            events->erase(events->begin() + following);
        }
    }
}

wxString NormalizePitch(const wxString& input)
{
    wxString pitch = input;
    pitch.Trim(true);
    pitch.Trim(false);
    pitch.Replace(wxT("♯"), wxT("#"));
    pitch.Replace(wxT("♭"), wxT("b"));

    if (pitch.Length() < 2)
        return wxT("C4");

    wxString octaveText = pitch.Right(1);
    long octave = 4;
    if (!octaveText.ToLong(&octave) || octave < 0 || octave > 8)
        return wxT("C4");

    wxString name = pitch.Left(pitch.Length() - 1);
    if (!name.IsEmpty())
    {
        wxString firstCharacter = name.Left(1);
        firstCharacter.MakeUpper();
        name = firstCharacter + name.Mid(1);
    }

    if (NoteOffset(name) < 0)
        return wxT("C4");

    return name + wxString::Format(wxT("%ld"), octave);
}

int PitchToMidi(const wxString& value)
{
    const wxString pitch = NormalizePitch(value);
    long octave = 4;
    pitch.Right(1).ToLong(&octave);
    const wxString name = pitch.Left(pitch.Length() - 1);
    const int offset = NoteOffset(name);
    return static_cast<int>((octave + 1) * 12 + offset);
}

wxString MidiToPitch(int midi)
{
    midi = std::max(0, std::min(127, midi));
    const int octave = midi / 12 - 1;
    return wxString(NOTE_NAMES[midi % 12]) +
           wxString::Format(wxT("%d"), octave);
}

std::vector<wxString> BuildPitchList(int lowestMidi, int highestMidi)
{
    std::vector<wxString> pitches;
    if (lowestMidi > highestMidi)
        std::swap(lowestMidi, highestMidi);

    for (int midi = lowestMidi; midi <= highestMidi; ++midi)
        pitches.push_back(MidiToPitch(midi));

    return pitches;
}

wxString SanitizeFestivalVoice(const wxString& input)
{
    wxString voice = input;
    voice.Trim(true);
    voice.Trim(false);

    if (voice.StartsWith(wxT("voice_")))
        voice = voice.Mid(6);

    wxString result;
    for (size_t i = 0; i < voice.Length(); ++i)
    {
        const wxChar ch = voice[i];
        const bool isAsciiLetter =
            (ch >= wxT('A') && ch <= wxT('Z')) ||
            (ch >= wxT('a') && ch <= wxT('z'));
        const bool isAsciiDigit = ch >= wxT('0') && ch <= wxT('9');

        if (isAsciiLetter || isAsciiDigit ||
            ch == wxT('_') || ch == wxT('-'))
        {
            result += ch;
        }
    }

    return result.IsEmpty() ? wxT("kal_diphone") : result;
}

wxString XmlEscape(const wxString& value)
{
    wxString result = value;
    result.Replace(wxT("&"), wxT("&amp;"));
    result.Replace(wxT("<"), wxT("&lt;"));
    result.Replace(wxT(">"), wxT("&gt;"));
    result.Replace(wxT("\""), wxT("&quot;"));
    result.Replace(wxT("'"), wxT("&apos;"));
    return result;
}

wxString SchemeEscape(const wxString& value)
{
    wxString result = value;
    result.Replace(wxT("\\"), wxT("/"));
    result.Replace(wxT("\""), wxT("\\\""));
    return result;
}

wxString BuildSingingXml(const std::vector<SingingEvent>& events,
                         double musicalBpm)
{
    wxString xml;
    xml << wxT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    xml << wxT("<!DOCTYPE SINGING PUBLIC \"-//SINGING//DTD SINGING mark up//EN\" ");
    xml << wxT("\"Singing.v0_1.dtd\" []>\n");
    xml << wxT("<SINGING BPM=\"")
        << Number(FestivalSingingBpm(musicalBpm))
        << wxT("\">\n");

    for (size_t i = 0; i < events.size(); ++i)
        xml << EventXml(events[i]);

    xml << wxT("</SINGING>\n");
    return xml;
}


wxString BuildSingingSongFileXml(const SingingSong& song)
{
    const double safeBpm =
        song.bpm > 1.0 ? song.bpm : 120.0;
    const wxString voice =
        SanitizeFestivalVoice(song.voice);

    wxString xml;
    xml << wxT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    xml << wxT("<!-- FestivalSingModeWx")
        << wxT(" TITLE=\"")
        << XmlCommentEscape(song.title)
        << wxT("\" VOICE=\"")
        << XmlCommentEscape(voice)
        << wxT("\" MUSICAL_BPM=\"")
        << Number(safeBpm)
        << wxT("\" -->\n");
    xml << wxT("<!DOCTYPE SINGING PUBLIC \"-//SINGING//DTD SINGING mark up//EN\" ");
    xml << wxT("\"Singing.v0_1.dtd\" []>\n");
    xml << wxT("<SINGING BPM=\"")
        << Number(FestivalSingingBpm(safeBpm))
        << wxT("\">\n");

    for (size_t i = 0; i < song.events.size(); ++i)
        xml << EventXml(song.events[i]);

    xml << wxT("</SINGING>\n");
    return xml;
}


bool ParseSingingSongFileXml(const wxString& xml,
                             SingingSong* song,
                             wxString* errorMessage)
{
    if (song == NULL)
    {
        if (errorMessage != NULL)
            *errorMessage = wxT("Invalid song destination.");
        return false;
    }

    const size_t rootStart =
        FindNoCase(xml, wxT("<SINGING"));
    if (rootStart == wxString::npos)
    {
        if (errorMessage != NULL)
            *errorMessage = wxT("Root <SINGING> element not found.");
        return false;
    }

    const size_t rootTagEnd =
        FindXmlTagEnd(xml, rootStart);
    if (rootTagEnd == wxString::npos)
    {
        if (errorMessage != NULL)
            *errorMessage = wxT("Unclosed <SINGING> tag.");
        return false;
    }

    const size_t rootClose =
        FindNoCase(xml, wxT("</SINGING>"), rootTagEnd + 1);
    if (rootClose == wxString::npos)
    {
        if (errorMessage != NULL)
            *errorMessage = wxT("Closing </SINGING> tag not found.");
        return false;
    }

    SingingSong parsed;
    parsed.events.clear();

    const wxString rootTag =
        xml.Mid(rootStart, rootTagEnd - rootStart + 1);

    wxString attribute;
    double xmlBpm = 120.0;
    if (ExtractXmlAttribute(rootTag, wxT("BPM"), &attribute))
    {
        double parsedXmlBpm = 0.0;
        if (ParsePositiveNumber(attribute, &parsedXmlBpm))
            xmlBpm = parsedXmlBpm;
    }

    // Without our metadata, convert Festival's 50-second-minute BPM back
    // to the musical BPM represented by the actual playback duration.
    parsed.bpm = xmlBpm * 60.0 / 50.0;

    const size_t metadataStart =
        FindNoCase(xml, wxT("<!-- FestivalSingModeWx"));
    if (metadataStart != wxString::npos &&
        metadataStart < rootStart)
    {
        const size_t metadataEnd =
            xml.find(wxT("-->"), metadataStart + 4);

        if (metadataEnd != wxString::npos)
        {
            const wxString metadata =
                xml.Mid(metadataStart,
                        metadataEnd - metadataStart + 3);

            if (ExtractXmlAttribute(
                    metadata, wxT("TITLE"), &attribute))
            {
                parsed.title = attribute;
            }

            if (ExtractXmlAttribute(
                    metadata, wxT("VOICE"), &attribute))
            {
                parsed.voice =
                    SanitizeFestivalVoice(attribute);
            }

            if (ExtractXmlAttribute(
                    metadata, wxT("MUSICAL_BPM"), &attribute))
            {
                double musicalBpm = 0.0;
                if (ParsePositiveNumber(attribute, &musicalBpm))
                    parsed.bpm = musicalBpm;
            }
        }
    }

    if (parsed.bpm < 30.0)
        parsed.bpm = 30.0;
    if (parsed.bpm > 300.0)
        parsed.bpm = 300.0;

    wxString lastPitch = wxT("C4");
    size_t position = rootTagEnd + 1;

    while (position < rootClose)
    {
        const size_t tagStart = xml.find(wxT("<"), position);
        if (tagStart == wxString::npos || tagStart >= rootClose)
            break;

        if (xml.Mid(tagStart, 4) == wxT("<!--"))
        {
            const size_t commentEnd =
                xml.find(wxT("-->"), tagStart + 4);
            if (commentEnd == wxString::npos)
            {
                if (errorMessage != NULL)
                    *errorMessage = wxT("Unclosed XML comment.");
                return false;
            }

            position = commentEnd + 3;
            continue;
        }

        if (FindNoCase(xml.Mid(tagStart, 7), wxT("<PITCH")) == 0)
        {
            const size_t pitchTagEnd =
                FindXmlTagEnd(xml, tagStart);
            if (pitchTagEnd == wxString::npos ||
                pitchTagEnd >= rootClose)
            {
                if (errorMessage != NULL)
                    *errorMessage = wxT("Unclosed <PITCH> tag.");
                return false;
            }

            const size_t pitchClose =
                FindNoCase(xml, wxT("</PITCH>"), pitchTagEnd + 1);
            if (pitchClose == wxString::npos ||
                pitchClose > rootClose)
            {
                if (errorMessage != NULL)
                    *errorMessage = wxT("Closing </PITCH> tag not found.");
                return false;
            }

            const wxString pitchTag =
                xml.Mid(tagStart, pitchTagEnd - tagStart + 1);
            const wxString pitch = PitchFromTag(pitchTag);
            lastPitch = pitch;

            size_t durationPosition = pitchTagEnd + 1;
            bool foundDuration = false;

            while (durationPosition < pitchClose)
            {
                const size_t durationStart =
                    FindNoCase(xml, wxT("<DURATION"),
                               durationPosition);
                if (durationStart == wxString::npos ||
                    durationStart >= pitchClose)
                {
                    break;
                }

                const size_t durationTagEnd =
                    FindXmlTagEnd(xml, durationStart);
                if (durationTagEnd == wxString::npos ||
                    durationTagEnd >= pitchClose)
                {
                    if (errorMessage != NULL)
                        *errorMessage =
                            wxT("Unclosed <DURATION> tag.");
                    return false;
                }

                const size_t durationClose =
                    FindNoCase(xml, wxT("</DURATION>"),
                               durationTagEnd + 1);
                if (durationClose == wxString::npos ||
                    durationClose > pitchClose)
                {
                    if (errorMessage != NULL)
                        *errorMessage =
                            wxT("Closing </DURATION> tag not found.");
                    return false;
                }

                const wxString durationTag =
                    xml.Mid(durationStart,
                            durationTagEnd - durationStart + 1);

                double beats = 1.0;
                DurationBeatsFromTag(
                    durationTag, parsed.bpm, &beats);

                wxString phoneme =
                    XmlUnescape(
                        xml.Mid(durationTagEnd + 1,
                                durationClose -
                                durationTagEnd - 1));
                phoneme.Trim(true);
                phoneme.Trim(false);

                parsed.events.push_back(
                    SingingEvent(pitch, beats, phoneme));
                foundDuration = true;
                durationPosition =
                    durationClose +
                    wxString(wxT("</DURATION>")).Length();
            }

            // A PITCH without DURATION is ignored, matching singing-mode's
            // expectation that duration carries the sung text.
            position =
                pitchClose + wxString(wxT("</PITCH>")).Length();
            continue;
        }

        if (FindNoCase(xml.Mid(tagStart, 6), wxT("<REST")) == 0)
        {
            const size_t restTagEnd =
                FindXmlTagEnd(xml, tagStart);
            if (restTagEnd == wxString::npos ||
                restTagEnd >= rootClose)
            {
                if (errorMessage != NULL)
                    *errorMessage = wxT("Unclosed <REST> tag.");
                return false;
            }

            const wxString restTag =
                xml.Mid(tagStart, restTagEnd - tagStart + 1);

            double beats = 1.0;
            DurationBeatsFromTag(restTag, parsed.bpm, &beats);

            parsed.events.push_back(
                SingingEvent(lastPitch, beats, wxEmptyString));

            const bool selfClosing =
                restTag.Length() >= 2 &&
                restTag[restTag.Length() - 2] == wxT('/');

            if (selfClosing)
            {
                position = restTagEnd + 1;
            }
            else
            {
                const size_t restClose =
                    FindNoCase(xml, wxT("</REST>"), restTagEnd + 1);

                position =
                    restClose == wxString::npos ||
                    restClose > rootClose
                        ? restTagEnd + 1
                        : restClose +
                          wxString(wxT("</REST>")).Length();
            }
            continue;
        }

        const size_t unknownEnd =
            FindXmlTagEnd(xml, tagStart);
        if (unknownEnd == wxString::npos)
            break;

        position = unknownEnd + 1;
    }

    *song = parsed;
    if (errorMessage != NULL)
        errorMessage->clear();
    return true;
}


wxString BuildDirectSingingScheme(const std::vector<SingingEvent>& events,
                                  double musicalBpm)
{
    if (events.empty())
        return wxEmptyString;

    struct DirectToken
    {
        DirectToken()
            : frequency(0.0),
              duration(0.0),
              rest(0.0),
              voiced(false)
        {
        }

        wxString text;
        double frequency;
        double duration;
        double rest;
        bool voiced;
    };

    const double safeBpm =
        musicalBpm > 1.0 ? musicalBpm : 120.0;
    const wxString separator =
        wxT("\n@@FESTIVAL_STEP@@\n");

    std::vector<DirectToken> tokens;
    tokens.reserve(events.size());

    for (size_t i = 0; i < events.size(); ++i)
    {
        const SingingEvent& event = events[i];

        const double safeBeats =
            event.beats > 0.0 ? event.beats : 0.25;
        const double seconds =
            safeBeats * 60.0 / safeBpm;

        wxString token = event.phoneme;
        token.Trim(true);
        token.Trim(false);
        token.Replace(wxT("\t"), wxT(""));
        token.Replace(wxT("\r"), wxT(""));
        token.Replace(wxT("\n"), wxT(""));
        token.Replace(wxT(" "), wxT(""));

        if (token.IsEmpty())
        {
            // singing-mode represents an initial rest with an empty Token.
            // Later rests are stored on the preceding voiced token.
            if (tokens.empty())
                tokens.push_back(DirectToken());

            tokens.back().rest += seconds;
            continue;
        }

        DirectToken directToken;
        directToken.text = token;
        directToken.voiced = true;
        directToken.duration = seconds;

        const int midi = PitchToMidi(event.pitch);
        directToken.frequency =
            440.0 * std::pow(
                2.0,
                (static_cast<double>(midi) - 69.0) / 12.0);

        tokens.push_back(directToken);
    }

    wxString scheme;

    scheme << wxT("(singing_init_func)") << separator;
    scheme << wxT("(set! singing_global_time 0.0)") << separator;
    scheme << wxT("(set! festival_direct_utt (Utterance Text \"\"))")
           << separator;

    // The relation is built directly, so synthesis must start at Tokens.
    scheme << wxT("(utt.set_feat festival_direct_utt 'type \"Tokens\")")
           << separator;
    scheme << wxT("(utt.relation.create festival_direct_utt 'Token)")
           << separator;

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        const wxString escapedToken =
            SchemeEscape(tokens[i].text);
        const wxString whitespace =
            i == 0 ? wxString() : wxString(wxT(" "));

        scheme << wxT("(utt.relation.append festival_direct_utt 'Token '(\"")
               << escapedToken
               << wxT("\" ((name \"")
               << escapedToken
               << wxT("\") (whitespace \"")
               << SchemeEscape(whitespace)
               << wxT("\") (prepunctuation \"\") (punc \"\"))))")
               << separator;
    }

    scheme << wxT("(set! festival_direct_tokens ")
           << wxT("(utt.relation.items festival_direct_utt 'Token))")
           << separator;

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        const DirectToken& token = tokens[i];

        if (token.voiced)
        {
            scheme << wxT("(item.set_feat ")
                   << wxT("(nth ") << static_cast<unsigned int>(i)
                   << wxT(" festival_direct_tokens) ")
                   << wxT("'freq (list (list (list ")
                   << Number(token.frequency)
                   << wxT("))))")
                   << separator;

            scheme << wxT("(item.set_feat ")
                   << wxT("(nth ") << static_cast<unsigned int>(i)
                   << wxT(" festival_direct_tokens) ")
                   << wxT("'dur (list (list (list ")
                   << Number(token.duration)
                   << wxT("))))")
                   << separator;
        }

        if (token.rest > 0.0)
        {
            // Same feature shape generated by singing-mode for XML REST.
            scheme << wxT("(item.set_feat ")
                   << wxT("(nth ") << static_cast<unsigned int>(i)
                   << wxT(" festival_direct_tokens) ")
                   << wxT("'rest (list ")
                   << Number(token.rest)
                   << wxT("))")
                   << separator;
        }
    }

    // Use synchronous playback so Festival keeps the waveform alive until
    // audio output has completed. "shutup" also stops any older waveform
    // before starting the new request.
    scheme << wxT("(audio_mode 'shutup)")
           << separator;
    scheme << wxT("(audio_mode 'sync)")
           << separator;
    scheme << wxT("(utt.synth festival_direct_utt)")
           << separator;
    scheme << wxT("(utt.play festival_direct_utt)")
           << separator;
    scheme << wxT("(singing_exit_func)");

    return scheme;
}


wxString BuildDirectSingingRenderScheme(
    const std::vector<SingingEvent>& events,
    double musicalBpm,
    const wxString& waveFilePath)
{
    if (waveFilePath.IsEmpty())
        return wxEmptyString;

    wxString scheme =
        BuildDirectSingingScheme(events, musicalBpm);

    if (scheme.IsEmpty())
        return wxEmptyString;

    const wxString separator =
        wxT("\n@@FESTIVAL_STEP@@\n");

    const wxString playbackTail =
        wxT("(audio_mode 'shutup)") +
        separator +
        wxT("(audio_mode 'sync)") +
        separator +
        wxT("(utt.synth festival_direct_utt)") +
        separator +
        wxT("(utt.play festival_direct_utt)") +
        separator +
        wxT("(singing_exit_func)");

    if (!scheme.EndsWith(playbackTail))
        return wxEmptyString;

    scheme.Remove(
        scheme.Length() - playbackTail.Length(),
        playbackTail.Length());

    scheme << wxT("(audio_mode 'shutup)")
           << separator;
    scheme << wxT("(utt.synth festival_direct_utt)")
           << separator;
    scheme << wxT("(utt.save.wave festival_direct_utt \"")
           << SchemeEscape(waveFilePath)
           << wxT("\")")
           << separator;
    scheme << wxT("(singing_exit_func)");

    return scheme;
}
