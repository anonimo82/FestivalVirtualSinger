#pragma once

#include "slice_model.h"

struct SliceEventTiming
{
    SliceEventTiming()
        : valid(false), loopUsed(false), earlyStop(false), eventSeconds(0.0),
          playbackRate(1.0), attackSeconds(0.0), loopSeconds(0.0),
          tailSeconds(0.0), availableLoopSeconds(0.0), fullLoopCount(0),
          residualSeconds(0.0), tailStartSeconds(0.0), naturalEndSeconds(0.0)
    {
    }

    bool valid;
    bool loopUsed;
    bool earlyStop;
    double eventSeconds;
    double playbackRate;
    double attackSeconds;
    double loopSeconds;
    double tailSeconds;
    double availableLoopSeconds;
    unsigned int fullLoopCount;
    double residualSeconds;
    double tailStartSeconds;
    double naturalEndSeconds;
};

class SliceEventTimingCalculator
{
public:
    static SliceEventTiming Calculate(const AudioSlice& slice,
                                      unsigned int sampleRate,
                                      int midiNote,
                                      long durationTicks,
                                      long ppq,
                                      double bpm);

    static long SnapDurationToWholeLoops(const AudioSlice& slice,
                                         unsigned int sampleRate,
                                         int midiNote,
                                         long requestedTicks,
                                         long ppq,
                                         double bpm);
};
