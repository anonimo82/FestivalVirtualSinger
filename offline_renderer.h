#pragma once

#include "sample_pool.h"
#include "slice_model.h"
#include "slicer_piano_roll.h"
#include <wx/string.h>

class OfflineSlicerRenderer
{
public:
    static bool Render(const wxString& path,
                       const SamplePool& samples,
                       const SliceModel& slices,
                       const std::vector<SliceRollEvent>& events,
                       double bpm,
                       wxString* errorMessage);
};
