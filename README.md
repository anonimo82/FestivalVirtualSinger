# Festival Sing Mode Frontend

A native Windows frontend for creating and playing simple sung phoneme sequences with the Festival Speech Synthesis System.

The application provides a piano-roll editor, per-note phoneme and duration editing, direct Festival playback, WAV export, XML import/export compatible with Festival singing mode, undo/redo support, and a local tone preview while editing.

## Features

- Piano-roll editing with drag, resize, pitch changes, and configurable grid snapping.
- A synchronized event list for notes and rests.
- Per-event pitch, duration, and phoneme/syllable editing.
- Local tone preview during editing, without invoking Festival.
- Direct playback through `FestivalTTSCOM.dll`.
- Full-sequence playback without creating a temporary WAV file.
- WAV export through Festival.
- Singing-mode XML import and export.
- Voice selection and BPM control.
- Undo and redo support.
- Runtime diagnostics in `festival_path_diagnostics.txt` and `festival_singing_trace.txt`.

## Platform

The current project targets:

- Windows 10 or later
- Win32 / x86
- Visual Studio 2013
- Platform toolset `v120`
- wxWidgets 3.2.x
- Festival 2.4
- Edinburgh Speech Tools 2.4

The Festival COM component and the frontend must both use the 32-bit architecture.

## Repository Layout

```text
Festival-sing-mode-frontend/
├── FestivalSingModeWx.vcxproj
├── FestivalSingModeWx.rc
├── main.cpp
├── festival_bridge.cpp
├── festival_bridge.h
├── piano_roll.cpp
├── piano_roll.h
├── song_model.cpp
├── song_model.h
├── tone_preview.cpp
├── tone_preview.h
├── wxWidgets/
└── festival_runtime/
    ├── FestivalTTSCOM.dll
    └── festival_home/
        ├── festival/
        │   ├── lib/
        │   │   ├── init.scm
        │   │   ├── singing-mode.scm
        │   │   ├── Singing.v0_1.dtd
        │   │   └── voices and lexicons
        │   ├── src/include/
        │   └── src/lib/libfestival.lib
        └── speech_tools/
            ├── include/
            └── lib/
                ├── libestbase.lib
                ├── libestools.lib
                └── libeststring.lib
```

The executable first looks for the COM DLL and Festival library relative to its own directory. It can also fall back to a Festival installation under `C:\festival`.

## Prerequisites

### Visual Studio 2013

Install Visual Studio 2013 with the Visual C++ toolchain. Use the **VS2013 x86 Native Tools Command Prompt** when compiling Festival and Speech Tools.

### Cygwin packages

Install Cygwin with the following packages selected in addition to the default base installation:

```text
bash
binutils
diffutils
gcc-core
gcc-g++
grep
gzip
make
patch
sed
tar
gawk
```

These packages provide the shell, compiler utilities, GNU Make, archive tools, and text-processing commands used by `configure` and by the `VCMakefile` generation step.

Festival and Speech Tools are compiled with the Visual Studio 2013 toolchain through `nmake`; Cygwin is used to unpack the sources, run `configure`, and generate the Microsoft-compatible makefiles.

### Ubuntu packages

For building Festival and Speech Tools natively on Ubuntu, enable the `universe` repository and install:

```sh
sudo add-apt-repository universe
sudo apt update
sudo apt install \
  build-essential \
  gcc \
  g++ \
  make \
  libasound2-dev \
  libncurses-dev \
  libx11-dev
```

Optional packages useful for development and documentation are:

```sh
sudo apt install git patch texinfo
```

Ubuntu also provides prebuilt packages:

```sh
sudo apt install festival festival-dev libestools-dev
```

The prebuilt Ubuntu packages may use a newer Festival/Speech Tools release than the Windows runtime described here. Build from source when compatibility with Festival 2.4 is required.

### wxWidgets

Place wxWidgets in:

```text
wxWidgets/
```

The project expects:

```text
wxWidgets/include
wxWidgets/include/msvc
wxWidgets/lib/vc_lib
```

The current project file is configured for wxWidgets 3.2.x and links against the static Visual C++ libraries in `wxWidgets\lib\vc_lib`.

## Building Festival and Speech Tools

The Windows port used by this project is:

```text
https://github.com/techiaith/Festival_Windows.git
```

Clone or extract it so that the root is exactly:

```text
C:\festival
```

The expected source directories are:

```text
C:\festival\festival
C:\festival\speech_tools
C:\festival\visualstudio
```

The build uses Cygwin to generate Visual C++ makefiles and `nmake` to compile Microsoft-compatible `.lib` files.

### Generate the Speech Tools Visual C++ Makefile

From a Cygwin shell:

```sh
cd /cygdrive/c/festival/speech_tools
./configure
make VCMakefile
cp config/vc_config_make_rules-dist config/vc_config_make_rules
```

### Build Speech Tools

From the **VS2013 x86 Native Tools Command Prompt**:

```bat
cd /d C:\festival\speech_tools
nmake /nologo /FVCMakefile
```

The build must produce:

```text
C:\festival\speech_tools\lib\libestbase.lib
C:\festival\speech_tools\lib\libestools.lib
C:\festival\speech_tools\lib\libeststring.lib
```

### Generate the Festival Visual C++ Makefile

From a Cygwin shell:

```sh
cd /cygdrive/c/festival/festival
./configure
make VCMakefile
cp config/vc_config_make_rules-dist config/vc_config_make_rules
```

### Build Festival

From the **VS2013 x86 Native Tools Command Prompt**:

```bat
cd /d C:\festival\festival
nmake /nologo /FVCMakefile
```

The build must produce:

```text
C:\festival\festival\src\lib\libfestival.lib
C:\festival\festival\src\main\festival.exe
```

Test the executable with an explicit Festival library directory:

```bat
echo hello world | C:\festival\festival\src\main\festival.exe --libdir C:\festival\festival\lib --tts
```

## Building FestivalTTSCOM

Open:

```text
C:\festival\visualstudio\FestivalTTSCOM\FestivalTTSCOM.vcxproj
```

Use:

```text
Configuration: Release
Platform: Win32
Platform Toolset: v120
Runtime Library: Multi-threaded (/MT)
```

Festival and Speech Tools are normally compiled with the static Release runtime. Building the COM project as Debug with `/MTd` causes errors such as:

```text
LNK2038: mismatch detected for '_ITERATOR_DEBUG_LEVEL'
LNK2038: mismatch detected for 'RuntimeLibrary'
```

The COM wrapper must treat every nonzero Festival return value as success. In `FestivalTTSEngine.cpp`, the methods that call Festival should use C truth semantics, for example:

```cpp
return ret != FALSE ? S_OK : S_FALSE;
```

After building, copy the resulting 32-bit Release DLL to:

```text
festival_runtime\FestivalTTSCOM.dll
```

## Preparing the Local Festival Runtime

Copy the following items into `festival_runtime\festival_home`:

```text
festival\lib
festival\src\include
festival\src\lib\libfestival.lib
speech_tools\include
speech_tools\lib\libestbase.lib
speech_tools\lib\libestools.lib
speech_tools\lib\libeststring.lib
```

At runtime, the most important files are:

```text
festival_runtime\FestivalTTSCOM.dll
festival_runtime\festival_home\festival\lib\init.scm
festival_runtime\festival_home\festival\lib\singing-mode.scm
festival_runtime\festival_home\festival\lib\Singing.v0_1.dtd
```

The selected Festival voice and its lexicon must also exist under the Festival library directory. The default voice is `kal_diphone`.

## Building the Frontend

Open `FestivalSingModeWx.vcxproj` in Visual Studio 2013.

Available configurations:

```text
Debug | Win32
Release | Win32
```

Build the project with:

```text
Build > Rebuild Solution
```

The output is written to the project directory:

```text
FestivalSingModeWx_debug.exe
FestivalSingModeWx.exe
```

The COM DLL may remain a Release build even when the frontend is built in Debug, because it is loaded as a separate COM module.

## Usage

1. Start `FestivalSingModeWx.exe` or `FestivalSingModeWx_debug.exe`.
2. Select a Festival voice and set the BPM.
3. Add notes or rests from the event list or piano roll.
4. Select an event and edit its pitch, duration, and phoneme.
5. Enable **Play tone while dragging/resizing** for local editing feedback.
6. Use **Preview Selected Phoneme** to sing only the selected event through Festival.
7. Use **Play sequence directly with Festival** to play the complete sequence.
8. Use **Export WAV** to render the sequence to a WAV file.
9. Save the song as Festival singing-mode XML.

An empty phoneme field represents a rest.

## XML Format

Songs are saved as Festival singing-mode XML. Additional application metadata is stored in an XML comment so that the original musical BPM, title, and voice can be restored when the document is reopened.

The application accepts notes, durations, and rests represented with elements such as:

```xml
<SINGING BPM="120">
  <PITCH NOTE="C4">
    <DURATION BEATS="1">la</DURATION>
  </PITCH>
  <REST BEATS="0.5"></REST>
</SINGING>
```

## Audio Architecture

The application uses two separate audio paths:

- **Editing preview:** a locally generated tone played through WinMM. No Festival voice or phoneme processing is involved.
- **Singing playback:** direct Festival synthesis through `FestivalTTSCOM.dll` and the native `win32audio` backend.

Full-sequence playback uses synchronous Festival audio so that the generated waveform remains valid until playback finishes. WAV export is a separate explicit operation.

## Diagnostics

The application writes these diagnostic files next to the executable:

```text
festival_path_diagnostics.txt
festival_singing_trace.txt
```

`festival_path_diagnostics.txt` records every Festival library directory checked and indicates which one was selected.

`festival_singing_trace.txt` records the generated Scheme commands, voice selection, synthesis steps, playback steps, and HRESULT values returned by the COM engine.

## Troubleshooting

### `Initialization file ... init.scm not found`

Festival is using the wrong library directory. Test it with:

```bat
festival.exe --libdir C:\festival\festival\lib
```

For the frontend, verify that this file exists:

```text
festival_runtime\festival_home\festival\lib\init.scm
```

### `FestivalTTSCOM.dll` cannot be loaded

Confirm that:

- the DLL is Win32/x86;
- it is located in `festival_runtime`;
- the frontend is also built as Win32;
- all Festival and Speech Tools libraries were built with a compatible Visual C++ runtime.

### `SelectVoice -> 0x00000001`

Confirm that the patched COM wrapper uses:

```cpp
return ret != FALSE ? S_OK : S_FALSE;
```

Also verify that the selected voice is installed under the Festival library directory.

### `singing-mode.scm not found`

Verify these two files:

```text
festival_runtime\festival_home\festival\lib\singing-mode.scm
festival_runtime\festival_home\festival\lib\Singing.v0_1.dtd
```

### `_ITERATOR_DEBUG_LEVEL` or `RuntimeLibrary` mismatch

Build `FestivalTTSCOM` as `Release | Win32` with `/MT`. Do not link Release Festival libraries into a `/MTd` Debug COM project.

### No sound during direct playback

Confirm that the Festival Scheme command below succeeds:

```scheme
(Parameter.set 'Audio_Method 'win32audio)
```

Then inspect `festival_singing_trace.txt` for the first step that does not return `S_OK`.

## Credits

This project was developed by Ivano Arrighetta and co-authored with ChatGPT by OpenAI.

It is conceptually derived from the original Festival singing-mode frontend and uses:

- Festival Speech Synthesis System
- Edinburgh Speech Tools
- wxWidgets
- the `techiaith/Festival_Windows` Windows port

## License

GNU General Public License version 3. See the upstream project and third-party dependency licenses for their respective terms.
