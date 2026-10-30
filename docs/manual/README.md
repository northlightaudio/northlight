# The manual

The user manual, published at [northlightaudio.org/manual](https://northlightaudio.org/manual/).
It lives here, beside the code, so each version of Northlight has the manual that
matches it: change the manual in the same commit as the behaviour it describes.

The website copies this folder in when it builds (`scripts/sync-manual.mjs` in the
northlightaudio.org repository), from the latest release's tag, or from `main` before
the first release. This README is not published.

## Layout

- One folder per section, one Markdown file per page. `index.md` is the manual's
  front page; a section's own `index.md` is that section's first page.
- `sidebar.json` lists the sections in order, with the label the sidebar shows for
  each folder. A new section needs an entry there.
- Pages are ordered within a section by `sidebar.order` in their front matter.
- Images go in an `images` folder beside the pages that use them and are linked
  relatively: `![The mixer](images/mixer.png)`. Use 2x captures from the app's
  `--capture` mode.

## Writing a page

Every page starts with front matter:

```yaml
---
title: Lumen
description: A modelled electric piano.
sidebar:
  order: 4
draft: true
---
```

`draft: true` keeps a page off the published site; take it out when the page is
ready. A section whose pages are all drafts is left out of the sidebar.

- Link to other pages by their file, relatively: `[Takes](../recording/takes.md)`,
  with an anchor if you like (`takes.md#comping`). The links work here on GitHub,
  and the site rewrites them to its own addresses.
- Write plain Markdown (`.md`), so pages read well on GitHub too. Starlight's asides
  work and read as plain text here:

  ```md
  :::note
  Arming is not an undo step.
  :::
  ```

- Instrument and effect pages follow one pattern: what it's for (one sentence), a
  screenshot of its face, what the display shows, the controls in the order they
  appear, presets, and tips.

## Voice

As in [the brand guide](../design/brand.md#voice): plain, specific and calm. Write to
the reader ("you"), in short sentences and British spelling (colour, normalise).
Explain a term the first time a page uses it (bus, send, latency), or link to the
[glossary](reference/glossary.md). Name controls exactly as the app labels them, in
bold. Give the default keys, since every key can be rebound.
