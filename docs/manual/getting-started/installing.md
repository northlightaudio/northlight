---
title: "Installing Northlight"
description: "Download Northlight, install it or run it from a folder, update it and remove it."
sidebar:
  order: 0
---

Northlight runs on 64-bit Windows 10 and Windows 11. It comes in two forms: an
installer, which is what most people want, and a portable zip for running it from a
folder without installing anything. Both are free.

## Downloading

Download Northlight from [northlightaudio.org/download](https://northlightaudio.org/download)
or from the [releases page on GitHub](https://github.com/northlightaudio/northlight/releases).
Each release has three files:

| File | What it is |
|---|---|
| `Northlight-0.1.0-Setup-x64.exe` | The installer |
| `Northlight-0.1.0-x64.zip` | The portable app: one folder to unzip anywhere |
| `SHA256SUMS.txt` | Checksums of the other two, if you want to check your download |

Neither needs anything else installed first. In particular you don't need the
Visual C++ Redistributable that many Windows programs ask for: what Northlight needs
from it is built in.

## Installing

1. Run `Northlight-0.1.0-Setup-x64.exe`.
2. Choose who to install it for. **Install for me only** puts Northlight in your own
   user folder and needs no administrator password. **Install for all users** puts it
   in Program Files for everyone on the computer, and Windows asks for an
   administrator's permission.
3. Choose the extra tasks. **Open .northlight projects with Northlight** is ticked, so
   that double-clicking a project in Explorer opens it. **Create a desktop shortcut**
   is offered but not ticked.
4. Finish, and leave **Launch Northlight** ticked to start it straight away.

The installer adds Northlight to the Start menu. The first time Northlight starts it
asks for your audio device: [Setting up audio](audio-setup.md) explains the choices.

:::note
A project you double-click while Northlight is already running opens in the window
you have, rather than in a second copy. If the project already open has unsaved
changes, Northlight asks first.
:::

## The portable zip

Unzip `Northlight-0.1.0-x64.zip` to any folder, a USB drive for example, and run
`Northlight.exe` inside it. Keep the folder together: the program needs the `assets`
folder beside it.

The zip adds no Start menu shortcut and doesn't open `.northlight` files from
Explorer. Open projects from Northlight's project menu, or drop them on its window.

"Portable" here means only that nothing is installed. Your settings and your own
files still go to the same places in your user profile as an installed Northlight's
(see [Where your settings and files live](#where-your-settings-and-files-live)), so a
zip and an installed copy on the same computer share them.

## Updating

Northlight doesn't check for updates by itself. New versions are announced on
[northlightaudio.org](https://northlightaudio.org) and the
[releases page](https://github.com/northlightaudio/northlight/releases), and the
[changelog](https://github.com/northlightaudio/northlight/blob/main/CHANGELOG.md)
says what changed in each.

To update an installed Northlight, download the new installer and run it. It installs
over the old version, in the same folder. If Northlight is open, the installer offers
to close it, so save your project before you start. Your settings, presets, templates
and projects are kept.

To update the portable version, unzip the new zip and use its folder in place of the
old one. Your settings are not in that folder, so nothing is lost.

## Uninstalling

Open Windows **Settings > Apps > Installed apps**, find **Northlight** and choose
**Uninstall**. This removes the program, its Start menu shortcut and the
`.northlight` file association.

The portable version has no uninstaller: delete its folder.

## Where your settings and files live

Uninstalling leaves your own things where they are, so a later install picks up where
you left off and no project loses its recordings.

| What | Where |
|---|---|
| Settings, the plugin list, autosaves, crash reports and the window layout | `%APPDATA%\Northlight` |
| Templates, your effect and channel strip presets, and the recordings of projects not yet saved | `Documents\Northlight` |
| A project's own recordings and imported audio | The project's media folder, beside the project file (see [Media and missing files](../projects/media.md)) |

To open the settings folder, type `%APPDATA%\Northlight` into Explorer's address bar,
or use **Show in Explorer** on the Diagnostics page of Preferences.

To remove Northlight completely, uninstall it and then delete those two folders.
Look in `Documents\Northlight\Recordings` first: it can hold recordings made in a
project you never saved.

:::caution
Deleting `%APPDATA%\Northlight` resets every preference, forgets your audio device
and the plugin scan, and throws away any autosave waiting to be restored.
:::
