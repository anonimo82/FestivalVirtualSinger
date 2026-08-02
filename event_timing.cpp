#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "event_timing.h"
#include <algorithm>
#include <cmath>

namespace
{
    double FramesToSeconds(unsigned long long frames, unsigned int sampleRate, double playbackRate)
    {
        if (sampleRate == 0 || playbackRate <= 0.0) return 0.0;
        return static_cast<double>(frames) / static_cast<double>(sampleRate) / playbackRate;
    }

    long SecondsToTicks(double seconds, long ppq, double bpm)
    {
        if (seconds <= 0.0 || ppq <= 0 || bpm <= 0.0) return 1;
        return std::max<long>(1, static_cast<long>(std::floor(seconds * bpm / 60.0 * ppq + 0.5)));
    }
}

SliceEventTiming SliceEventTimingCalculator::Calculate(const AudioSlice& slice,
                                                        unsigned int sampleRate,
                                                        int midiNote,
                                                        long durationTicks,
                                                        long ppq,
                                                        double bpm)
{
    SliceEventTiming result;
    if (sampleRate == 0 || ppq <= 0 || bpm <= 0.0 || durationTicks <= 0 ||
        slice.endFrame <= slice.startFrame)
        return result;

    result.valid = true;
    result.eventSeconds = static_cast<double>(durationTicks) / static_cast<double>(ppq) * 60.0 / bpm;
    result.playbackRate = std::pow(2.0, static_cast<double>(midiNote - slice.rootMidiNote) / 12.0);

    const unsigned long long attackFrames = slice.loopInFrame >= slice.startFrame
        ? slice.loopInFrame - slice.startFrame : 0;
    const unsigned long long loopFrames = slice.loopOutFrame > slice.loopInFrame
        ? slice.loopOutFrame - slice.loopInFrame : 0;
    const unsigned long long tailFrames = slice.endFrame >= slice.loopOutFrame
        ? slice.endFrame - slice.loopOutFrame : 0;
    const unsigned long long fullFrames = slice.endFrame - slice.startFrame;

    result.attackSeconds = FramesToSeconds(attackFrames, sampleRate, result.playbackRate);
    result.loopSeconds = FramesToSeconds(loopFrames, sampleRate, result.playbackRate);
    result.tailSeconds = FramesToSeconds(tailFrames, sampleRate, result.playbackRate);
    result.naturalEndSeconds = FramesToSeconds(fullFrames, sampleRate, result.playbackRate);

    if (!slice.loopEnabled || result.loopSeconds <= 0.0)
    {
        result.earlyStop = result.eventSeconds < result.naturalEndSeconds;
        result.tailStartSeconds = std::min(result.eventSeconds, result.naturalEndSeconds);
        return result;
    }

    // If the event cannot contain both the complete attack and tail, playback
    // starts normally and is stopped at the event edge with the anti-click fade.
    if (result.eventSeconds < result.attackSeconds + result.tailSeconds)
    {
        result.earlyStop = true;
        result.tailStartSeconds = result.eventSeconds;
        return result;
    }

    result.loopUsed = true;
    result.availableLoopSeconds = result.eventSeconds - result.attackSeconds - result.tailSeconds;
    result.fullLoopCount = static_cast<unsigned int>(std::floor(result.availableLoopSeconds / result.loopSeconds));
    result.residualSeconds = result.availableLoopSeconds - result.fullLoopCount * result.loopSeconds;
    if (result.residualSeconds < 0.0000001) result.residualSeconds = 0.0;
    result.tailStartSeconds = result.eventSeconds - result.tailSeconds;
    return result;
}

long SliceEventTimingCalculator::SnapDurationToWholeLoops(const AudioSlice& slice,
                                                           unsigned int sampleRate,
                                                           int midiNote,
                                                           long requestedTicks,
                                                           long ppq,
                                                           double bpm)
{
    SliceEventTiming timing = Calculate(slice, sampleRate, midiNote, requestedTicks, ppq, bpm);
    if (!timing.valid || !slice.loopEnabled || timing.loopSeconds <= 0.0)
        return requestedTicks;

    const double minimum = timing.attackSeconds + timing.tailSeconds;
    const double requestedSeconds = static_cast<double>(requestedTicks) / ppq * 60.0 / bpm;
    if (requestedSeconds <= minimum) return SecondsToTicks(minimum, ppq, bpm);

    const double requestedLoopArea = requestedSeconds - minimum;
    unsigned int loops = static_cast<unsigned int>(std::floor(requestedLoopArea / timing.loopSeconds + 0.5));
    const double snappedSeconds = minimum + loops * timing.loopSeconds;
    return SecondsToTicks(snappedSeconds, ppq, bpm);
}
