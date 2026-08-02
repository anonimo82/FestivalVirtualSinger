#pragma once

#include "sample_pool.h"
#include <wx/string.h>
#include <vector>

struct AutoSliceSettings
{
    AutoSliceSettings() : uniformDivisions(8), sensitivity(0.35), minimumGapMs(60.0) {}
    int uniformDivisions;
    double sensitivity;
    double minimumGapMs;
};

class AutoSlicer
{
public:
    static bool Uniform(const SamplePoolItem& source, const AutoSliceSettings& settings,
                        std::vector<unsigned long long>* boundaries, wxString* errorMessage);
    static bool Transients(const SamplePoolItem& source, const AutoSliceSettings& settings,
                           std::vector<unsigned long long>* boundaries, wxString* errorMessage);
};
