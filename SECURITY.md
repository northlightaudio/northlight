# Security

## Reporting a vulnerability

Please report security problems privately, not in a public issue. Use GitHub's
private reporting: on the repository's **Security** tab, choose **Report a
vulnerability**. Only the maintainers see the report.

Include what you can of:

- the Northlight version (the About box, such as `0.2.0-dev · 1cb6f30`) and your
  Windows version;
- what an attacker needs: a crafted project, audio, MIDI or preset file, a plugin, or
  something else;
- the steps to reproduce it, and the file that does it if there is one;
- what happens: a crash, code running, a file written somewhere it should not be.

We aim to reply within a week, and to agree with you on when the problem is made
public once a fix is out. Northlight has no bug bounty.

## Versions

Northlight has not had a release yet. Fixes go into `main` and the next build made
from it; there are no older versions that receive fixes.

## What counts

Northlight is a desktop application with no network service, so the problems that
matter most are files and plugins doing more than they should:

- a project, audio, MIDI, preset or template file that crashes Northlight in a way
  that could run code, or that reads or writes files outside what it names;
- a flaw in the installer or the signed executables;
- a way around the plugin scanner's separate process.

A plugin is code you chose to run, with the same rights as Northlight. A plugin that
misbehaves once it is loaded is not a vulnerability in Northlight, although a way for
it to escape the crash containment described in
[docs/architecture.md](docs/architecture.md) is worth a report.
