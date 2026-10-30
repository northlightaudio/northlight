---
title: "Media and missing files"
description: "Where a project's audio lives, keeping a project together, and finding missing files."
sidebar:
  order: 1
---

A project file doesn't contain its audio. The recordings and imported files that its
clips play, its **media**, are separate audio files, and the project remembers where
each one is. This page explains where those files go, how to keep a project's files
in one place, and what to do when one goes missing.

## The media folder

Every saved project has a media folder beside it, named after the project: `Song
Media` beside `Song.northlight`. Northlight puts there:

- every recording you make in the project;
- a copy of every audio file you import, while **Copy media** is on;
- the renders of frozen tracks, in a `Freeze` folder inside it.

**Copy media** is on by default, in **Preferences > General**. With it on, importing
a file from your Downloads folder or a sample library copies it into the media folder,
and the project plays the copy. Your original stays where it was. Importing the same
file twice uses the copy already there. With **Copy media** off, the project plays
imported files from wherever they are.

Keeping everything in the media folder means the project folder holds the project's
audio: you can back it up, or move it to another computer, as one folder.

:::caution
The media folder holds the audio of clips and takes. Two kinds of file stay where they
are and are not copied, gathered or listed as missing: the samples loaded into the
[Sampler](../instruments/sampler.md) and impulse responses loaded into
[Corona](../effects/corona.md). The project remembers where they are, so keep them in
the same place, and take them with you when you move a project to another computer.
:::

### Before the first save

A project that hasn't been saved has no folder yet. Its recordings go to
`Documents\Northlight\Recordings`, and imported files are played from where they are.
When you first save, the recordings come into the new media folder, and, with
**Copy media** on, so do the imported files.

If you close an unsaved project without saving, the recordings it made that nothing can
use any more go to the Recycle Bin.

### Saving to another folder

**Save As** to a different folder makes a new media folder for the new name. If the
project uses files outside it, Northlight asks "Bring the media along?" and says how
many files and how much space. **Copy media** copies them in; **Keep where they are**
leaves the new project using the old files.

## Gather media

**Gather media into the project folder** (in the project menu, under **Media**) copies
every file the project uses from elsewhere into its media folder, and points the clips
at the copies. Use it when you have imported files with **Copy media** off, or opened
an older project whose audio is scattered, and want the project in one place. Then save.

The project must be saved first. If everything is already in the folder, a message
says so.

## Remove unused media

Over time a media folder collects files no clip uses: takes you deleted, imports you
removed. **Remove unused media** (in the project menu, under **Media**) lists how many
there are and how much space they take, and **Move to Recycle Bin** moves them there.

Every take in the take lanes counts as used, even the ones not playing, and so does a
frozen track's render. The files go to the Recycle Bin rather than being deleted, so if
you later undo the deletion of a clip, its file can still be put back from there.

In a project not saved yet, the command removes only the recordings that project made
in the shared Recordings folder, never other projects' files.

## Missing files

The project remembers each file by its full path, such as
`D:\Music\Song Media\Vox 1.wav`. A file goes missing if it is moved, renamed or
deleted, or if the drive it is on isn't connected. That includes moving or renaming the
project folder itself: the project still looks in the old place.

When a project opens with files missing, a message says so: "3 files this project uses
are missing. Their clips play silence until you locate them." The clips stay in place
and play silence.

### Locating a missing file

1. Click **Locate...** on the message, or on a missing clip.
2. Find the file and open it. The dialog starts in the folder where the file used to
   be, or as close to it as still exists.
3. If other missing files are in the same folder under the same names, Northlight asks
   "Relink the other missing files?". **Relink all** relinks them too; **Just this one**
   relinks only the file you chose.

A message says how many files were relinked, and how many are still missing. Relinking
is one undo step. Save the project to keep the new paths.

:::tip
Moved a whole project folder? Open the project, click **Locate...**, and choose the
first file in the new `Song Media` folder. **Relink all** finds the rest at once.
:::
