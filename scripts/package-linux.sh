#!/bin/sh
# Build Northlight for release on Linux and package it as an AppImage.
#
# Builds the linux-package preset (Release, static C++ runtime) into
# build/linux-package, runs the tests, stages an AppDir and writes to
# build/linux-package/dist:
#   Northlight-<version>-x86_64.AppImage   the app: make it executable and run it
#   SHA256SUMS.txt                          its checksum
# See docs/building.md, "Packaging".
#
# An AppImage runs on systems with the same or a newer glibc than the one it was
# built on, so release builds come from an old base (Ubuntu 22.04 in release.yml).
#
# Usage: scripts/package-linux.sh [--label <label>] [--skip-build] [--skip-tests]
#                                 [--jobs <n>] [--low-priority]
#   --label         'dev' (the default), a pre-release such as 'beta.1', or '' for
#                   a final release. Anything but 'dev' needs a clean working tree.
#   --skip-build    package what is already in build/linux-package
#   --skip-tests    build without running the tests
#   --jobs          compile at most this many files at once
#   --low-priority  build and test at the lowest CPU priority (nice 19)
#
# Tools: appimagetool and the AppImage type-2 runtime, from NORTHLIGHT_APPIMAGETOOL
# and NORTHLIGHT_APPIMAGE_RUNTIME (paths), else appimagetool on PATH and the
# runtime it downloads itself. release.yml pins both by checksum.
set -eu

label=dev
skip_build=0
skip_tests=0
jobs=
nice_cmd=
while [ $# -gt 0 ]; do
    case "$1" in
    --label) label="$2"; shift 2 ;;
    --skip-build) skip_build=1; shift ;;
    --skip-tests) skip_tests=1; shift ;;
    --jobs) jobs="$2"; shift 2 ;;
    --low-priority) nice_cmd="nice -n 19"; shift ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/build/linux-package"
app="$build/src/app/northlight_artefacts/Release"
appdir="$build/AppDir"
dist="$build/dist"
id=org.northlightaudio.northlight

# ---------------------------------------------------------------------------
# Checks before the long build
# ---------------------------------------------------------------------------
appimagetool="${NORTHLIGHT_APPIMAGETOOL:-$(command -v appimagetool || true)}"
if [ -z "$appimagetool" ] || [ ! -x "$appimagetool" ]; then
    echo "appimagetool was not found: set NORTHLIGHT_APPIMAGETOOL or put it on PATH" >&2
    exit 1
fi
runtime="${NORTHLIGHT_APPIMAGE_RUNTIME:-}"
if [ -n "$runtime" ] && [ ! -f "$runtime" ]; then
    echo "NORTHLIGHT_APPIMAGE_RUNTIME names no file: $runtime" >&2
    exit 1
fi

cd "$root"
commit="$(git rev-parse --short=7 HEAD)"
if [ "$label" != dev ] && [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "A '$label' package must be built from a clean commit (it names the source it came from)." >&2
    echo "Commit or stash the changes first, or use --label dev." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Build and test
# ---------------------------------------------------------------------------
if [ "$skip_build" -eq 0 ]; then
    $nice_cmd cmake --preset linux-package "-DNORTHLIGHT_VERSION_LABEL=$label"
    $nice_cmd cmake --build --preset linux-package ${jobs:+--parallel "$jobs"}
    if [ "$skip_tests" -eq 0 ]; then
        # As package.ps1: the paint-time budgets (label perf) are timed against
        # one machine, the real-time tests (label paced) run one at a time after
        # the rest, and a failed test runs once more before the package fails.
        # The JUCE tests need a display; xvfb-run gives one when there is none.
        display=
        if [ -z "${DISPLAY:-}" ] && command -v xvfb-run > /dev/null; then
            display="xvfb-run -a"
        fi
        $nice_cmd $display ctest --preset linux-package -LE 'perf|paced' --repeat until-pass:2
        $nice_cmd $display ctest --preset linux-package -L paced --parallel 1 --repeat until-pass:2
    fi
fi

# ---------------------------------------------------------------------------
# Version, as version::full() writes it; only a dev build names its commit
# ---------------------------------------------------------------------------
cache="$build/CMakeCache.txt"
[ -f "$cache" ] || { echo "No build in $build; run without --skip-build first." >&2; exit 1; }
number="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$cache")"
built_label="$(sed -n 's/^NORTHLIGHT_VERSION_LABEL:STRING=//p' "$cache")"
if [ "$built_label" != "$label" ]; then
    echo "build/linux-package was built with label '$built_label', not '$label'; build again without --skip-build." >&2
    exit 1
fi
version="$number"
[ -n "$label" ] && version="$version-$label"
[ "$label" = dev ] && version="$version+$commit"
echo "Packaging Northlight $version"

# ---------------------------------------------------------------------------
# Stage the AppDir
# ---------------------------------------------------------------------------
[ -x "$app/Northlight" ] || { echo "Northlight not found in $app" >&2; exit 1; }
rm -rf "$appdir" "$dist"
mkdir -p "$appdir/usr/lib/northlight" "$appdir/usr/share/applications" "$appdir/usr/share/mime/packages" \
    "$appdir/usr/share/metainfo" "$dist"
# The app folder stays whole: the app finds its assets and the scanner beside itself.
cp -R "$app/." "$appdir/usr/lib/northlight/"
cp "$root/installer/linux/AppRun" "$appdir/AppRun"
chmod 755 "$appdir/AppRun"
cp "$root/installer/linux/$id.desktop" "$appdir/$id.desktop"
cp "$root/installer/linux/$id.desktop" "$appdir/usr/share/applications/$id.desktop"
cp "$root/installer/linux/$id.xml" "$appdir/usr/share/mime/packages/$id.xml"
cp "$root/installer/linux/$id.metainfo.xml" "$appdir/usr/share/metainfo/$id.metainfo.xml"
for size in 16 32 48 256; do
    dir="$appdir/usr/share/icons/hicolor/${size}x${size}/apps"
    mkdir -p "$dir"
    cp "$root/assets/brand/northlight-icon-$size.png" "$dir/$id.png"
done
cp "$root/assets/brand/northlight-icon-256.png" "$appdir/$id.png"
ln -sf "$id.png" "$appdir/.DirIcon"

# The newest glibc the executables need: the oldest system the AppImage runs on.
glibc="$(objdump -T "$appdir/usr/lib/northlight/Northlight" "$appdir/usr/lib/northlight/northlight_plugin_scan" |
    grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1)"
echo "Needs $glibc or newer"
if objdump -p "$appdir/usr/lib/northlight/Northlight" | grep -q 'NEEDED.*libstdc++'; then
    echo "Northlight links libstdc++ dynamically; build with the linux-package preset" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# AppImage
# ---------------------------------------------------------------------------
name="Northlight-$version-x86_64.AppImage"
# appimagetool is itself an AppImage; extracting it first means it runs without
# FUSE (containers, WSL).
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 "$appimagetool" --no-appstream \
    ${runtime:+--runtime-file "$runtime"} "$appdir" "$dist/$name"
echo "Wrote $name"

# ---------------------------------------------------------------------------
# Checksums, in the format `sha256sum -c` reads
# ---------------------------------------------------------------------------
(cd "$dist" && sha256sum "$name" > SHA256SUMS.txt)
echo
echo "Done: $dist"
ls -l "$dist"
