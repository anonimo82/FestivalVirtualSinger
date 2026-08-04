# Festival Virtual Singer — Typical Workflow

This tutorial presents the two normal ways to use Festival Virtual Singer. You can use either workflow independently or combine them in the same project.

## 1. Start a Project

Launch the application. Use **File > New** for a clean Festival song, or open a previously saved singing-mode XML document. For a complete Sample Slicer session, use **Open Project Folder...** or create a new folder later with **Save Project Folder...**.

## 2. Compose a Festival Vocal Line

1. Select a Festival voice and set the song BPM.
2. Add a pitched event with **+ Pitch** or add silence with **+ Rest**.
3. Select the event in the list or piano roll.
4. Edit its pitch, beat duration, and phoneme or syllable.
5. Apply the changes.
6. Drag notes horizontally to change timing, vertically to change pitch, and resize them to change duration.
7. Enable grid snapping when you need rhythmically regular placement.
8. Use the local tone-preview option while dragging or resizing for immediate pitch feedback.

An empty phoneme is treated as a rest. The local editing tone is only a pitch guide; it does not reproduce the selected Festival voice.

## 3. Preview and Export Festival Singing

Use **Preview Selected Phoneme** to synthesize the currently selected event. Use the sequence playback control to send the complete song directly to Festival. Stop playback with the Festival stop control when required.

Save the editable song as singing-mode XML. Use **Export WAV Audio...** when you need a rendered audio file. The XML stores Festival-compatible events and application metadata such as title, selected voice, and musical BPM.

## 4. Build a Sample Pool

Open the Sample Slicer workspace and choose **Import WAV...**. Import one or more PCM or IEEE-float WAV files. The Sample Pool displays file and format information and lets you preview or remove sources.

Select a source to load its waveform. Zoom, fit the complete file, scroll through it, or drag across the waveform to define a selection.

## 5. Create Slices

For a manual slice:

1. Select a waveform range.
2. Click **Create Slice**.
3. Edit the slice name and root MIDI note.
4. Adjust start, loop-in, loop-out, and end markers.
5. Enable or disable looping.
6. Click **Apply Slice**.

For automatic slicing, choose **Transients** or **Uniform**. Configure sensitivity and minimum gap for transient detection, or the number of divisions for uniform slicing. Preview the markers before applying them.

## 6. Audition a Slice

Select a slice, choose a MIDI note, and select **Retrigger** or **Legato** mode. Use **Note On**, **Note Off**, and **Stop All** to test pitch transposition, attack, loop, and tail behavior through the realtime WinMM engine.

## 7. Arrange the Slicer Piano Roll

Open **Slicer Piano Roll...** and choose an active slice. Add events and place them on the grid. Each event stores:

- start beat;
- length;
- MIDI note;
- velocity;
- source slice.

Drag events to move or transpose them. Use rectangular marquee selection for multiple events. Duplicate, delete, cut, copy, and paste as needed. Undo and redo are available for arrangement edits.

Choose **Free** duration for unrestricted lengths or **Loop Snap** to align event duration to complete slice loops. Configure BPM, grid snap, playback start, and optional loop playback, then use **Play** and **Stop** to audition the arrangement.

## 8. Save the Complete Project

Choose **Save Project Folder...**. The application creates a portable project containing:

- `project.fvsp` for samples, slices, and Slicer events;
- `singer.xml` for the Festival song;
- an `audio` directory containing project-owned WAV copies.

Reopen the folder with **Open Project Folder...**. Missing audio files are reported as warnings instead of being silently ignored.

## 9. Render the Slicer Arrangement

Choose **Render Slicer WAV...** and select an output path. The offline renderer creates a stereo 44.1 kHz, 16-bit PCM WAV file and preserves overlapping events, event velocity, pitch transposition, loop behavior, and tails.

## 10. Troubleshoot When Needed

When Festival playback or export fails, inspect `festival_path_diagnostics.txt` and `festival_singing_trace.txt`. For Slicer drag diagnostics, inspect `M11_DRAG_DIAGNOSTIC.log`. The Wiki contains detailed setup and troubleshooting guidance.
