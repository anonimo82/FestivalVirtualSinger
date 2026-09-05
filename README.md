# Festival Virtual Singer

Festival Virtual Singer is a C++/wxWidgets application for composing simple vocal sequences with the Festival Speech Synthesis System and for building sample-based performances from WAV recordings.

The project supports both **Windows** and **Linux**.

It combines two complementary workflows:

- **Festival Singer** — write notes and rests, assign phonemes, choose a Festival voice, preview the result, and export singing-mode XML or WAV audio.
- **Sample Slicer** — import WAV files, inspect their waveforms, create and loop slices, arrange them in a polyphonic piano roll, and render the arrangement offline.

## Main Features

- Piano-roll note editing with pitch, duration, phoneme, rests, snapping, drag and resize operations.
- Festival voice selection, direct sequence playback, selected-phoneme preview, and WAV export.
- Singing-mode XML import and export with application metadata.
- Undo and redo in the Festival editor.
- WAV Sample Pool with waveform display and realtime preview.
- Waveform zooming, selection, manual slicing, and transient or uniform auto-slicing.
- Editable slice start, loop-in, loop-out, end, root note, and loop state.
- Realtime slice audition with retrigger and legato modes.
- Dedicated polyphonic Slicer Piano Roll with velocity, loop-aware duration snapping, clipboard editing, marquee selection, and transport controls.
- Project folders that keep the song, sample copies, slices, and arrangement together.
- Offline stereo WAV rendering of the Slicer arrangement.

## Linux Build

The Linux port uses:

- CMake
- system wxWidgets
- ALSA for realtime audio
- the native Festival executable instead of the Windows COM bridge

### Debian / Ubuntu dependencies

```bash
sudo apt update
sudo apt install build-essential cmake libwxgtk3.2-dev libasound2-dev festival
```

Install at least one Festival voice package as well. For example:

```bash
sudo apt install festvox-kallpc16k
```

Available voice package names vary by distribution.

### Build

Clone the repository and build with the helper script:

```bash
git clone https://github.com/anonimo82/FestivalVirtualSinger.git
cd FestivalVirtualSinger
./build-linux.sh
```

The executable is created at:

```text
build/FestivalVirtualSinger
```

You can also build manually with CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Then run:

```bash
./build/FestivalVirtualSinger
```

## Windows Build

The original Windows build remains available through the supplied Visual Studio project.

Requirements:

- Windows 10 or later
- Win32 / x86
- Visual Studio 2013 with platform toolset `v120`
- wxWidgets 3.2.x static libraries
- Festival 2.4 and Edinburgh Speech Tools 2.4
- compatible 32-bit `FestivalTTSCOM.dll`

Festival, the COM component, and the application must use the same 32-bit architecture.

Build `FestivalSingModeWx.vcxproj` as:

```text
Release | Win32
```

Place the Festival Windows runtime beside the executable as described in the project documentation.

## Platform Notes

On Linux, Festival playback is performed through the native `festival` command and realtime sample/slice playback uses ALSA.

On Windows, the original Festival COM bridge and WinMM audio path remain part of the historical Windows implementation.

The bundled Windows wxWidgets binaries and Windows Festival runtime are not used by the Linux CMake build.

## Usage

Start the application and choose either the **Singer** or **Slicer** tab.

A practical walkthrough is available in [Tutorial.md](Tutorial.md). Additional Linux-specific notes are available in [README-LINUX.md](README-LINUX.md).

## Repository Overview

The application is split into focused C++ modules for the song model, Festival bridge, piano rolls, sample pool, waveform editor, slice model, realtime sample engine, automatic slicing, event timing, and offline rendering.

## License

Festival Virtual Singer is distributed under the GNU General Public License version 3. See [LICENSE](LICENSE).
