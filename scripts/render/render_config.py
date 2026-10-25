"""Where the render scripts find what is not in the repository: Blender, the HDRI their
reflections use, and a machine with a GPU to render on remotely.

Each setting comes from its environment variable if set, else from
scripts/render/render.local.json (yours, not committed: copy render.example.json), else a
default. Plain Python, so the Blender scripts can import it too.

| Setting      | Environment variable       | Default                               |
|--------------|----------------------------|---------------------------------------|
| hdri         | NORTHLIGHT_HDRI            | the bundled hdri/lythwood_lounge_2k   |
| blender      | BLENDER                    | `blender` on the PATH                 |
| remote_host  | NORTHLIGHT_RENDER_HOST     | none (render locally)                 |
| remote_dir   | NORTHLIGHT_RENDER_DIR      | none: needed with a remote host       |
| ssh_prefix   | NORTHLIGHT_SSH_PREFIX      | none (run ssh and scp directly)       |
"""
import json
import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
LOCAL = os.path.join(HERE, "render.local.json")
HDRI_NAME = "lythwood_lounge_2k.hdr"  # Poly Haven, CC0
_local = None


def setting(key, env, default=None):
    """A setting: its environment variable, else render.local.json, else `default`."""
    global _local
    value = os.environ.get(env)
    if value:
        return value
    if _local is None:
        try:
            with open(LOCAL, encoding="utf-8") as f:
                _local = json.load(f)
        except (OSError, ValueError):
            _local = {}
    value = _local.get(key)
    return value if value not in (None, "") else default


def hdri_path():
    """The HDRI every part's reflections come from: the bundled one unless set otherwise."""
    path = setting("hdri", "NORTHLIGHT_HDRI", os.path.join(HERE, "hdri", HDRI_NAME))
    if not os.path.exists(path):
        raise SystemExit(f"No HDRI at {path}: set NORTHLIGHT_HDRI, or \"hdri\" in {LOCAL}, to one "
                         f"(scripts/render/hdri holds {HDRI_NAME}).")
    return path


def blender_path():
    """The Blender to run (4.2, as the scripts were written for)."""
    return setting("blender", "BLENDER") or shutil.which("blender") or "blender"
