# Festival Virtual Singer

Festival Virtual Singer is a native Windows application for composing simple vocal sequences with the Festival Speech Synthesis System and for building sample-based performances from WAV recordings.

It combines two complementary workflows:

- **Festival Singer** — write notes and rests, assign phonemes, choose a Festival voice, preview the result, and export singing-mode XML or WAV audio.
- **Sample Slicer** — import WAV files, inspect their waveforms, create and loop slices, arrange them in a polyphonic piano roll, and render the arrangement offline.

## Main Features

- Piano-roll note editing with pitch, duration, phoneme, rests, snapping, drag and resize operations.
- Festival voice selection, direct sequence playback, selected-phoneme preview, and WAV export.
- Singing-mode XML import and export with application metadata.
- Undo and redo in the Festival editor.
- WAV Sample Pool with metadata inspection and Windows preview.
- Waveform zooming, selection, manual slicing, and transient or uniform auto-slicing.
- Editable slice start, loop-in, loop-out, end, root note, and loop state.
- Realtime WinMM slice audition with retrigger and legato modes.
- A dedicated polyphonic Slicer Piano Roll with velocity, loop-aware duration snapping, clipboard editing, marquee selection, and transport controls.
- Project folders that keep the song, sample copies, slices, and arrangement together.
- Offline stereo WAV rendering of the Slicer arrangement.
- Diagnostic files for Festival discovery, synthesis commands, and piano-roll drag operations.

## Requirements

The supplied Visual Studio project targets a legacy-compatible Windows toolchain:

- Windows 10 or later
- Win32 / x86
- Visual Studio 2013 and platform toolset `v120`
- wxWidgets 3.2.x static libraries
- Festival 2.4 and Edinburgh Speech Tools 2.4
- A compatible 32-bit `FestivalTTSCOM.dll`

Festival, the COM component, and the application must use the same 32-bit architecture.

## Getting Started

1. Build or install the required Festival runtime and wxWidgets libraries.
2. Build `FestivalSingModeWx.vcxproj` as `Release | Win32`.
3. Place the Festival runtime beside the executable as described in the Wiki.
4. Start the application and choose either the Festival Singer or Sample Slicer workflow.

For a practical walkthrough, see [Tutorial.md](Tutorial.md). Full reference documentation is available in the project Wiki.

## Repository Overview

The application is split into focused C++ modules for the song model, Festival bridge, piano rolls, sample pool, waveform editor, slice model, realtime sample engine, automatic slicing, event timing, and offline rendering.

## Diagnostics

Runtime diagnostics are written next to the executable:

- `festival_path_diagnostics.txt`
- `festival_singing_trace.txt`
- `M11_DRAG_DIAGNOSTIC.log`

These files are useful when Festival cannot be discovered, synthesis fails, or Slicer drag behavior needs inspection.

## License

Festival Virtual Singer is distributed under the GNU General Public License version 3. See [LICENSE](LICENSE).
