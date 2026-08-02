# FestivalVirtualSinger M11 — Final consolidation

## Added

- Permanent project-folder workflow; no archive container is used.
- `project.fvsp` manifest for Sample Pool sources, stable slice IDs and Slicer Piano Roll events.
- `singer.xml` stored alongside the Slicer project data.
- Project-owned `audio/` folder containing copied WAV sources.
- Restore of Singer data, source metadata, slices, events and Slicer BPM.
- Missing-WAV reporting without terminating the application.
- Offline Slicer Piano Roll rendering to stereo 44.1 kHz/16-bit PCM WAV.
- Offline rendering supports pitch, velocity, overlapping events, loop repetition and tail fitting within the event edge.

## Timing domains

- Festival singing mode preserves the intentional `musicalBpm * 50 / 60` conversion.
- Slicer transport and offline rendering use normal musical BPM.

## Known deferred issues

1. Realtime slice audition may sound slower than the source at Root MIDI note.
2. Mouse dragging of Slicer Piano Roll events remains unreliable; numeric editing works.
3. Festival playback Stop may not interrupt immediately.

These are documented limitations and are not represented as fixed in M11.

## M11.1 project-folder save fix

- Fixed an accidental recursive call in `EnsureSlicerPianoRoll()` that caused a stack-overflow crash while saving a project folder.
- Restored lazy construction of the Slicer Piano Roll before serializing its BPM/events.
- Removed a duplicated destination-folder dialog declaration.
- `project.fvsp` is now reached and written after `singer.xml` and the `audio` folder are prepared.
- No tests or musical features were added; `TEST_M11.txt` is unchanged.

## M11.2 corrective build

- Saving over an already opened project folder no longer attempts to copy a project-owned WAV onto itself.
- Offline velocity now uses a clearly audible MIDI response curve.
- Each offline event is rendered into an independent buffer before additive mixing, so overlapping events cannot replace one another.
- Final output uses gentle soft clipping rather than hard clipping when overlaps exceed full scale.

## M11.3 corrective build

This corrective build keeps `TEST_M11.txt` unchanged and addresses the reported
M11 regressions:

- the Slicer Piano Roll canvas is now a scrollable window with horizontal and
  vertical scrollbars, a 128-beat virtual timeline, and mouse-coordinate
  conversion that remains correct after scrolling;
- offline rendering applies MIDI velocity independently to each event before
  mixing, using an explicit 0..127 gain curve;
- overlapping offline events are accumulated additively in independent event
  buffers; the completed mix is peak-scaled only when necessary, instead of
  being compressed sample-by-sample.

The previously deferred realtime root-note playback-rate issue and Festival
Stop latency are not changed by this corrective build.


## M11.4 corrective build
- Removed peak normalization from offline rendering; fixed output headroom now preserves MIDI velocity differences.
- Overlapping event buffers remain additive and are no longer rescaled as a whole to the same peak.
- Corrected the piano-roll event-table base class to `wxScrolledWindow`.
- Clicking an already-selected event now preserves the multi-selection for group dragging.
- Ctrl-click toggles selection without silently collapsing the group.
- Removed a duplicated `SetVirtualSize` call introduced in M11.3.
- `TEST_M11.txt` is unchanged.

## M11 diagnostic build

This package adds runtime diagnostics only. It does not claim to fix the deferred
offline velocity, overlapping-event mix, or piano-roll drag issues. Rendering
writes `<output.wav>.diagnostic.txt`; piano-roll mouse handling writes
`M11_DRAG_DIAGNOSTIC.log` beside the executable.

## M11.5 realtime voice-pool correction
- Replaced the single realtime voice with a 32-voice WinMM mixer.
- Each piano-roll event now receives an independent voice id.
- MIDI velocity is applied per realtime voice.
- Overlapping events are mixed additively in the same output buffer.
- Note Off is routed to the specific event voice at that event's right edge.
- The output device runs at 44.1 kHz; each voice compensates for the source WAV sample rate in its playback step.
- Offline rendering was not changed.
