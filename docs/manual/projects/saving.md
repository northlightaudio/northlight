---
title: "Saving and opening"
description: "Project files, the project menu, autosave, backups and recovery after a crash."
sidebar:
  order: 0
---

A Northlight song is a **project**: one `.northlight` file holding the tracks, clips,
notes, mix and automation, with its audio kept in a media folder beside it. This page
covers the commands that make, open and save projects, and what Northlight does to
keep your work safe when something goes wrong.

## The project menu

Click the project's name at the left of the header bar to open the project menu. The
commands for files are:

| Command | Key | What it does |
|---|---|---|
| **New** | Ctrl+N | Starts an empty project. |
| **New from template** | | Starts a project from a saved setup (see [Templates](templates.md)). |
| **Open** | Ctrl+O | Opens a project file. |
| **Recent** | | The projects you opened last. |
| **Close project** | | Closes the project, leaving an empty one. |
| **Save** | Ctrl+S | Saves the project to its file, or asks for one the first time. |
| **Save As** | Ctrl+Shift+S | Saves the project to a new file and carries on in that one. |
| **Revert to saved** | | Throws away the changes since the last save. |

While a project has unsaved changes, the label over its name in the header bar reads
"PROJECT · EDITED". Hover over the name to see where the file is.

## Saving

The first save asks where to put the project; the dialog starts in your Documents
folder, or the folder set under **Folder** in **Preferences > General**. Saving
`Song.northlight` also makes a folder called `Song Media` beside it for the project's
recordings and imported audio (see [Media and missing files](media.md)). Keep the two
together.

Save early. Until a project is saved, its recordings go to a shared folder,
`Documents\Northlight\Recordings`; the first save brings them into the project's own
media folder.

**Save As** to another folder, when the project uses audio that is outside the new
media folder, asks "Bring the media along?". **Copy media** copies those files into
the new project's media folder, so the new project stands on its own; **Keep where
they are** leaves them where they are and the new project uses them from there.

:::note
Arming a track, choosing its input and changing its monitoring are not undo steps,
but they count as changes and are saved with the project.
:::

## Unsaved changes

Anything that would close or replace a project with unsaved changes asks first: New,
Open, a recent project, New from template, Close project, Quit, the window's close
button, and Windows shutting down. The question is "Save changes to Song?", with
**Save**, **Don't save** and **Cancel**.

**Revert to saved** asks too, and it can't be undone: it reopens the file as it was
last saved.

## Opening

Open a project with **Open** (Ctrl+O), from **Recent**, by double-clicking it in
Explorer (when Northlight was installed with the file association, see
[Installing Northlight](../getting-started/installing.md)), with Explorer's
**Open with**, or by dragging the `.northlight` file onto Northlight's window. If
Northlight is already running, a project opened from Explorer opens in that window.

A large project takes a moment to open. A sheet shows what is loading (the project,
each audio file, each plugin by name) with a progress bar, and the window keeps
drawing. **Cancel** stops the opening and leaves the project you had open as it was,
until the plugins start loading; from then on the opening runs to the end.

If audio files are missing when a project opens, a message says so, with a button to
find them. See [Media and missing files](media.md#missing-files).

### Recent projects

**Recent** lists the projects you opened or saved last, newest first: 8 of them, or the
number set under **Recent** in **Preferences > General** (up to 20). A project that has
been moved or deleted, or is on a drive that isn't connected, stays in the list greyed,
marked "Not found". **Clear recent** empties the list.

Turn on **Reopen last** in **Preferences > General** to have Northlight open the last
project each time it starts.

## Autosave

While a project has unsaved changes, Northlight saves a copy of it in the background
every 2 minutes. The copy is not your project file: it goes to the `Autosave` folder in
`%APPDATA%\Northlight`, and it is deleted when you save or choose **Don't save**. Set
the interval under **Autosave** in **Preferences > General**, from 1 to 60 minutes; 0
turns autosave off.

## Restoring after a crash

If Northlight closes without saving, from a crash or a power cut, the next start offers
the autosave: "Northlight closed with unsaved changes to "Song" (autosaved 14:32)."

- **Restore** opens the autosaved copy. Check it, then save it with **Save** or
  **Save As**.
- **Discard** deletes the autosave.

If you were recording when it happened, a second message offers to recover the
unfinished recordings. See [When a take goes wrong](../recording/problems.md).

When a crash leaves a crash report, a message says so, with **Show** to find the
report. Reports stay on your computer (see [Getting help](../reference/help.md)).

## The backup file

Each save keeps the version it replaces beside the project, as `Song.northlight-bak`.
If you save over a good project with a bad edit, the previous save is still there.

To go back to it:

1. Close the project in Northlight.
2. In Explorer, copy `Song.northlight-bak` and rename the copy so it ends in
   `.northlight`, such as `Song (backup).northlight`. Windows warns about changing
   the extension; accept.
3. Open the renamed copy in Northlight.

The backup is one step deep: the next save replaces it. Turn it off with
**Keep backup** in **Preferences > General**.
