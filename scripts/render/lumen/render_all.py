"""Renders every layer of Lumen's face into a folder, one Blender run each.

    python render_all.py <out folder> [--only deck,lit,pointers,tines] [--remote [user@host]]

Writes deck.png and lit.png (2320 x 1200), pointers.png (the same size, colour with alpha)
and tine-N.png (1160 x 600), the tines bent from fully down (0) to fully up (8), frame 4 at
rest. package_assets.py turns them into assets/lumen.

Locally it renders at idle priority on the CPU (LUMEN_GPU=1 for this machine's GPU). With
--remote it renders on another machine's GPU, in the Blender container of
scripts/render/remote (its README says how to set one up): the scripts and HDRI are copied to
that compose project's work folder, every layer runs in one container run, and the renders
come back. The host, the project's folder and anything ssh must run through come from
scripts/render/render_config.py (remote_host, remote_dir, ssh_prefix) or --remote and
--remote-dir. Render a whole set on one machine: the tine frames must match the still deck.
"""
import argparse
import os
import shlex
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SCRIPT = os.path.join(HERE, "face_render.py")
sys.path.insert(0, os.path.dirname(HERE))
from render_config import blender_path, hdri_path, setting  # noqa: E402  (scripts/render)

BLENDER = blender_path()
ROOM = hdri_path()
TINE_FRAMES = 9  # src/ui/strip/LumenFace.h: tineFrames
LAYERS = (("deck", 256), ("lit", 256), ("pointers", 128))


def jobs(only):
    """Each render as (file name, scale, samples, environment)."""
    out = [(f"{layer}.png", 2, samples, {"LUMEN_LAYER": layer}) for layer, samples in LAYERS if layer in only]
    if "tines" in only:
        for k in range(TINE_FRAMES):
            swing = -1.0 + 2.0 * k / (TINE_FRAMES - 1)
            out.append((f"tine-{k}.png", 1, 128, {"LUMEN_LAYER": "deck", "LUMEN_SWING": f"{swing:.4f}"}))
    return out


def render_local(out, todo):
    for name, scale, samples, env in todo:
        e = dict(os.environ)
        e.update({k: str(v) for k, v in env.items()})
        flags = 0x00000040 if sys.platform == "win32" else 0  # IDLE_PRIORITY_CLASS
        path = os.path.join(out, name)
        cmd = [BLENDER, "--background", "--factory-startup", "--python", SCRIPT, "--", path, str(scale), str(samples)]
        print("render", name, flush=True)
        result = subprocess.run(cmd, env=e, creationflags=flags, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                text=True)
        if result.returncode != 0 or not os.path.exists(path):
            sys.exit(f"failed: {path}\n{result.stderr[-2000:]}")


def render_remote(out, todo, host, remote_dir):
    prefix = shlex.split(setting("ssh_prefix", "NORTHLIGHT_SSH_PREFIX", ""))
    via_wsl = bool(prefix) and prefix[0].lower().startswith("wsl")

    def local_path(p):  # as scp, run through the prefix, sees it
        p = os.path.abspath(p)
        if via_wsl and len(p) > 1 and p[1] == ":":
            return "/mnt/" + p[0].lower() + p[2:].replace("\\", "/")
        return p

    def run(cmd):
        result = subprocess.run(prefix + cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if result.returncode != 0:
            sys.exit(f"failed: {' '.join(cmd)}\n{result.stdout[-3000:]}")
        return result.stdout

    ssh = ["ssh", "-o", "BatchMode=yes", host]
    work = "/work/northlight-lumen"            # inside the container
    host_work = f"{remote_dir}/work/northlight-lumen"
    print("upload", flush=True)
    run(ssh + [f"rm -rf {host_work} && mkdir -p {host_work}/scripts/render/lumen {host_work}/scripts/render/tapeecho "
               f"{host_work}/hdri {host_work}/out"])
    uploads = [(os.path.join(REPO, "scripts", "render", "render_config.py"), "scripts/render/"),
               (SCRIPT, "scripts/render/lumen/"),
               (os.path.join(REPO, "scripts", "render", "tapeecho", "vu_render.py"), "scripts/render/tapeecho/"),
               (ROOM, "hdri/")]
    for src, dest in uploads:
        run(["scp", "-q", "-o", "BatchMode=yes", local_path(src), f"{host}:{host_work}/{dest}"])
    hdri = f"{work}/hdri/{os.path.basename(ROOM)}"
    lines = ["set -e"]
    for name, scale, samples, env in todo:
        assigns = " ".join(f"{k}={shlex.quote(str(v))}" for k, v in env.items())
        lines.append(f"echo render {name}")
        lines.append(f"{assigns} LUMEN_GPU=1 NORTHLIGHT_HDRI={hdri} blender --background --factory-startup "
                     f"--python {work}/scripts/render/lumen/face_render.py -- {work}/out/{name} {scale} {samples} "
                     f"> {work}/out/{name}.log 2>&1")
    job = os.path.join(out, "remote-job.sh")
    with open(job, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    run(["scp", "-q", "-o", "BatchMode=yes", local_path(job), f"{host}:{host_work}/job.sh"])
    print("render on", host, flush=True)
    print(run(ssh + [f"cd {remote_dir} && docker compose run --rm --entrypoint sh blender {work}/job.sh"]).strip())
    print("download", flush=True)
    remote_glob = f"{host}:{host_work}/out/*.png"
    if via_wsl:
        remote_glob = f"'{remote_glob}'"  # WSL runs it through a shell: the far side expands it
    run(["scp", "-q", "-o", "BatchMode=yes", remote_glob, local_path(out) + "/"])


def main():
    p = argparse.ArgumentParser()
    p.add_argument("out")
    p.add_argument("--only", default="deck,lit,pointers,tines")
    p.add_argument("--samples", type=int, help="every render at this many samples (a quick look)")
    p.add_argument("--scale", type=float, help="every render at this scale (a quick look)")
    p.add_argument("--remote", nargs="?", const="",
                   help="render on user@host's GPU (alone: the configured remote_host)")
    p.add_argument("--remote-dir", help="the Blender compose project there (default: remote_dir)")
    a = p.parse_args()
    os.makedirs(a.out, exist_ok=True)
    todo = jobs(set(a.only.split(",")))
    if a.samples or a.scale:
        todo = [(n, a.scale or s, a.samples or c, e) for n, s, c, e in todo]
    if a.remote is not None:
        host = a.remote or setting("remote_host", "NORTHLIGHT_RENDER_HOST")
        remote_dir = a.remote_dir or setting("remote_dir", "NORTHLIGHT_RENDER_DIR")
        if not host or not remote_dir:
            sys.exit("--remote needs a host and the Blender compose project's folder there: give --remote "
                     "user@host and --remote-dir, or set remote_host and remote_dir (scripts/render/render_config.py)")
        render_remote(a.out, todo, host, remote_dir)
    else:
        render_local(a.out, todo)
    print("done", a.out)


if __name__ == "__main__":
    main()
