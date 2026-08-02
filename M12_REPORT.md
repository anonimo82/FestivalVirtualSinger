# FestivalVirtualSinger M12 - Piano Roll Editing

## Implemented

- Undo and Redo with a 100-state history.
- Undo grouping for mouse drag: moving a multi-selection is one history operation.
- Undo coverage for add, delete, duplicate, cut, paste and Apply Event.
- Keyboard shortcuts: Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z, Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+A and Delete.
- Clipboard operations preserve slice ID, note, duration, velocity and relative layout.
- Pasting anchors the earliest copied event to the transport Start field, snapped to the active grid.
- Rectangular marquee selection from empty piano-roll space.
- Ctrl/Shift marquee selection extends the existing selection.
- Marquee and event hit-testing account for horizontal and vertical scrolling.
- Dragging directly on an event remains event movement, not marquee selection.

## Unchanged

- Singer and Festival rendering behavior.
- Festival singing-mode BPM conversion (`musicalBpm * 50 / 60`).
- Sample engine, realtime polyphony and velocity.
- Offline rendering and project-folder format.

## Build note

The source was prepared against the existing Visual Studio 2013 / wxWidgets project. Compile and run the tests in `TEST_M12.txt` in the configured Windows environment.
