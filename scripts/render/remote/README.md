# Rendering on another machine's GPU

The face renders take minutes on a GPU and much longer on a CPU. This folder is a Docker
Compose project that runs Blender 4.2.3 (the version the scripts are written for, so a
render matches one made locally) on a machine with an NVIDIA GPU, started only for a job:
nothing stays loaded on the GPU between renders.

## Setting up a host

Needs Docker with Compose and the NVIDIA Container Toolkit.

1. Copy `Dockerfile` and `compose.yaml` to a folder on the host and build:
   `docker compose --profile render build`.
2. Check Blender sees the GPU:
   `docker compose run --rm blender --python-expr "import bpy; p = bpy.context.preferences.addons['cycles'].preferences; p.compute_device_type = 'OPTIX'; p.get_devices(); print([d.name for d in p.devices])"`.

## Using it

Tell the render scripts where it is (scripts/render/render_config.py): environment
variables, or `scripts/render/render.local.json` (not committed; copy
`render.example.json`):

| Setting | Environment variable | What |
|---|---|---|
| `remote_host` | `NORTHLIGHT_RENDER_HOST` | `user@host` for ssh and scp (key login) |
| `remote_dir` | `NORTHLIGHT_RENDER_DIR` | The folder on the host holding this project |
| `ssh_prefix` | `NORTHLIGHT_SSH_PREFIX` | A command ssh and scp must run through, if any (e.g. `wsl --` on Windows when only WSL holds the key; local paths are then given as WSL sees them) |

Then `python scripts/render/orbit/render_all.py <folder> --remote`. The scripts, fonts and
HDRI are copied to the project's `work/`, every render runs in one container run, and the
PNGs come back. Render a whole set on one machine: CPU and GPU noise differ a little, and
parts of a face (Orbit's reel frames and its still deck) must match.
