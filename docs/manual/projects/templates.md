---
title: "Templates"
description: "Save a project's setup as a template and start new projects from it."
sidebar:
  order: 2
---

A **template** is a project's setup without its music: the tracks, the mix and the
effects, ready for a new song. If you record the same band, or always start with the
same drum machine, bass synth and reverbs, a template saves building that setup again
each time.

## What a template keeps

A template keeps:

- the tracks, folder tracks, buses and FX tracks, with their names, colours, inputs and
  outputs;
- the routing: sends, sidechains and which instrument each MIDI track plays;
- the instruments, the inserts and the channel strips, with their settings;
- the volume, pan, mute and solo of every channel;
- the tempo, time signature and key.

It leaves out the clips and takes, the markers and regions, and the points on
automation lanes. Tracks are saved disarmed.

## Saving a template

1. Set up a project the way you want new projects to start. It can be an empty
   project you build for the purpose, or a song whose setup you want to reuse: the
   song itself is not changed.
2. Open the project menu (click the project's name in the header bar) and choose
   **Save as template**.
3. Type a name, such as "Band" or "Vocal session", and click **Save**.

The template is saved as a file in `Documents\Northlight\Templates`, such as
`Band.northlight`. Saving a template with a name that already exists replaces the old
one without asking.

## Starting from a template

Open the project menu and choose **New from template**, then the template. (The item is
greyed while there are no templates.) If the project you have open has unsaved
changes, Northlight asks whether to save them first.

The new project opens with the template's setup, untitled: the template file itself is
never changed. Save it under a name of its own before you record, so its recordings get
a media folder (see [Media and missing files](media.md)).

## Managing templates

There is no editor for templates inside Northlight. To change one, start a project from
it, change what you want, and save it as a template again under the same name. To
rename or delete templates, open `Documents\Northlight\Templates` in Explorer and rename
or delete the files there. The **New from template** menu lists whatever is in that
folder, in name order.

:::tip
A template is an ordinary project file, so you can copy one to another computer by
copying it into that computer's `Documents\Northlight\Templates` folder. Plugins it
uses that aren't installed there show as missing until you install them.
:::
