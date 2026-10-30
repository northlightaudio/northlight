---
title: "Presets"
description: "How presets work in every built-in effect: stepping, saving your own, Custom and undo."
sidebar:
  order: 0
---

Every effect that comes with Northlight has presets: named settings you can load with
a click, step through while the song plays, and add to with your own. They work the
same way in every built-in effect, so this page covers them once.

A preset is a starting point, not a finished answer. Load one that sounds close, then
adjust it to the track in front of you.

## The preset bar

The preset bar sits in the effect card's header in the **Editor** panel, between the
effect's name and its menu, and in the effect's own window, beside the title. It shows
the current preset's name in a pill, with an arrow either side:

- **‹** (Previous preset) and **›** (Next preset) step through the whole list, the
  factory presets first and then your own. The list wraps round: Next on the last
  preset goes back to the first.
- Click the **name** to open the list.

Each step loads the preset straight away, so you can play the song and step through
the list until something is close.

When a card is too narrow for the bar, the bar hides. The presets are still in the
card's menu (the **⋮** button), in the **Presets** submenu, which also names the
current preset ("Presets: Vocal Plate").

Some effects with hardware faces have a preset control of their own as well. Patina,
for example, has an amber display between two keys. It uses the same list as the bar,
so the two always show the same preset.

## The list

Clicking the name opens a menu with:

1. The factory presets. The first is always the effect as it is when you add it,
   usually called **Default** (Patina's is **Gentle Age**).
2. Your own presets under a **Yours** heading, in name order.
3. **Save preset...**, **Delete** and **Show presets folder**.

The current preset has a tick beside it.

## Custom

The name in the bar always describes the settings the effect has right now. As soon
as you change a setting, the name changes to **Custom**. If you set everything back to
exactly what a preset has, that preset's name comes back.

From Custom, **›** goes to the first preset in the list and **‹** to the last.

## Saving your own

When you have a setting you want to use again:

1. Click the preset name to open the list.
2. Choose **Save preset...**.
3. Type a name (the field starts with the current preset's name, or "My preset") and
   click **Save**.

Your preset appears under **Yours** in that effect's list, in every project, and every
preset bar for that effect updates at once. If one of your presets already has that
name, the new one replaces it.

Your presets are kept as small files, one per preset, in
`Documents\Northlight\Effect Presets`, in a folder named after the effect.
**Show presets folder** opens that folder, which is handy for backing them up or
copying them to another computer. A preset saved by an older version of Northlight
still loads; any setting it doesn't know about takes its default.

## Deleting a preset

Load one of your presets, open the list and choose **Delete "name"**. The file goes to
the Recycle Bin, so you can recover it from there. Factory presets can't be deleted:
the item is dimmed while one of them is current.

## Undo

Loading a preset changes all of the effect's settings in one step, so **Undo**
(Ctrl+Z) puts back everything the effect had before, all at once. The undo history
names the step after the preset, such as "Preset Vocal Plate". See
[Undo](../editing/undo.md).

:::note
While you are writing automation, loading a preset sets the effect's values but is
not written into the automation lanes. See [Automation modes](../automation/modes.md).
:::

## Channel strip presets

The [channel strip](../mixing/channel-strip.md) on every track has presets of its own,
which set up all six of its modules at once (for example "Lead vocal" or "Drum bus").
They are separate from the effect presets on this page; see
[strip presets](../mixing/channel-strip.md#presets).

Plugins from other makers (VST3 and CLAP) keep their presets in their own window's
bar; see [Plugins](../mixing/plugins.md).
