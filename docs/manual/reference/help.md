---
title: "Getting help"
description: "Where to ask a question, and how to report a bug."
sidebar:
  order: 5
---

Northlight is made in the open, and the people who make it read what you write. If
this manual has not answered your question, or something is not working, here is
where to go.

## Asking a question

- **Discord**: [discord.gg/dkW3NSenhP](https://discord.gg/dkW3NSenhP). The quickest
  place for a question, a chat about how to do something, or sharing what you made.
- **GitHub Discussions**: the **Discussions** tab of the
  [Northlight repository](https://github.com/northlightaudio/northlight). Better for
  longer questions and ideas, and the answers stay findable for the next person.

Before asking, the [Troubleshooting](troubleshooting.md) page covers the most common
problems with sound, plugins and projects.

## Reporting a bug

A bug is anything that crashes, sounds wrong, or does something other than what this
manual says. Report it as an issue on GitHub: go to the
[Issues](https://github.com/northlightaudio/northlight/issues) tab and choose **New
issue**, then the bug report form. You need a free GitHub account.

A report that lets someone make the problem happen on their own computer is the one
that gets fixed. Include:

1. **What happened**, and what you expected instead. "The track's meter stops moving
   after I add a second send" is more useful than "the mixer is broken".
2. **The steps to make it happen**, from opening Northlight, as few as possible. Say
   whether it happens every time or only sometimes.
3. **The Northlight version**, from **About Northlight** in the app menu. It looks
   like `0.1.0` and may have a short code after it; copy all of it.
4. **Your Windows version**.
5. **Your audio setup**, if the problem is to do with sound: the interface, the driver
   (ASIO or Windows Audio), the sample rate and the buffer size. **Copy system report**
   in [Preferences > Diagnostics](preferences.md#diagnostics) copies these, with your
   plugins and system details, ready to paste.
6. **Any plugins involved**: name, maker, version and format (VST3 or CLAP).
7. **Files**, if you can share them:
   - after a crash, the newest `.dmp` and `.txt` files from
     `%APPDATA%\Northlight\Crashes` (or its `Seen` folder, if you have already opened
     the report from Northlight's notice);
   - for clicks and dropouts, the text from **Copy report** in the Diagnostics panel
     (F7);
   - a small project that shows the problem, or a short recording of it.

   Zip files to attach them to the issue. A crash report holds a snapshot of
   Northlight's memory at the moment it crashed, which can include parts of the
   project you had open; only attach one you are happy to share publicly.

Search the existing issues first: if someone has reported the same thing, add what
you know to theirs rather than opening a new one.

## Security problems

If you find a problem that could let a project, audio, MIDI or preset file, or the
installer, do harm (run code, or read or write files it should not), do not report it
in a public issue. Follow
[SECURITY.md](https://github.com/northlightaudio/northlight/blob/main/SECURITY.md)
instead: it explains how to report it privately, through the repository's
**Security** tab.

## Helping out

Northlight is free and open source, and help is welcome: fixing bugs, improving the
manual, testing with your own interface and plugins, or reporting what you find. [CONTRIBUTING.md](https://github.com/northlightaudio/northlight/blob/main/CONTRIBUTING.md)
explains how to build Northlight and what a good change looks like. The manual lives
in the repository's `docs/manual` folder; if a page is wrong or unclear, you can open
an issue or edit it there.
