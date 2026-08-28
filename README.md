# obs-body-mask

An OBS Studio video filter for **human body segmentation** on Apple Silicon
(macOS, M-series). Runs realtime ONNX inference (ONNX Runtime + CoreML) and
offers three output modes:

1. **Mask** — outputs a binary mask: **human body = white, everything else =
   black**. Use it directly, or chain it as a mask source downstream.
2. **Background Replace** — keeps people, replaces the non-human background with
   an **image** or **solid color**, with a slider for the **% of the original**
   to blend back through.
3. **Head Text Overlay** — draws an auto-numbered label (`Person 1`, `Person 2`,
   …) on each detected head, using a multi-person **pose** model (YOLO26-pose by
   default; the older YOLOv8/11 `[1,56,8400]` layout is also supported).

Designed for the hard cases: it works for **back-facing** people (segmentation
is silhouette-based and head detection uses pose keypoints, not faces) and for
**~20 people at once** (segmentation is count-agnostic; pose is single-shot
multi-person).

## Install

### macOS (Apple Silicon) — prebuilt plugin

Grab `obs-body-mask.plugin.zip` from the
[latest release](https://github.com/du-song/obs-body-mask/releases/latest) and
unpack it into the OBS user plugin directory:

```bash
cd ~/Downloads
unzip obs-body-mask.plugin.zip -d obs-body-mask-release

PLUGINS=~/Library/Application\ Support/obs-studio/plugins
mkdir -p "$PLUGINS"
rm -rf "$PLUGINS/obs-body-mask.plugin"
cp -R obs-body-mask-release/obs-body-mask.plugin "$PLUGINS/"

# The bundle is ad-hoc signed, so clear the download quarantine flag —
# otherwise Gatekeeper blocks the bundled ONNX Runtime dylib and OBS
# silently skips the plugin.
xattr -dr com.apple.quarantine "$PLUGINS/obs-body-mask.plugin"
```

Restart OBS, then select a source → **Filters → + → Body Segmentation Mask**.

The bundle is self-contained — ONNX Runtime, all four models
(`selfie_segmentation`, `pphumanseg`, `yolo26n-pose`, `yolo11s-pose`), the
overlay font and the effect all live inside the `.plugin`, so
`scripts/fetch-models.sh` is not needed for the release build.

Requires Apple Silicon and OBS Studio 31+ (tested on 32.2). If the filter does
not appear, check the load result in the OBS log:

```bash
grep -i body-mask ~/Library/Application\ Support/obs-studio/logs/*.txt | tail
```

## Architecture

A platform-agnostic, OBS-free **shared core** (`src/core`, `src/ort-utils`) does
all model work and is reused by two front-ends:

- **`body-mask-cli`** — a standalone CLI that runs the pipeline on a single
  image. Builds and runs on **Linux and macOS without OBS**, for fast model
  validation and tuning.
- The **OBS plugin** (`src/body-mask-filter.cpp`, macOS) — reads frames back
  from the GPU, runs the core, and composites the result via
  `data/effects/body-mask.effect`.

The core depends only on **ONNX Runtime** plus vendored header-only libraries
(`stb_image`, `stb_image_write`, `stb_truetype`) — no OpenCV.

## Build from source

### 1. Fetch models + font (once)

```bash
./scripts/fetch-models.sh
```

### 2a. CLI (Linux or macOS — no OBS needed)

```bash
cmake --preset cli-linux
cmake --build build
./build/body-mask-cli --mode mask       --input people.jpg --output mask.png       --model data/models/selfie_segmentation.onnx
./build/body-mask-cli --mode background  --input people.jpg --bg-color FF0000 --original-blend 20 --output bg.png --model data/models/selfie_segmentation.onnx
./build/body-mask-cli --mode overlay     --input people.jpg --output labels.png \
    --pose-model data/models/yolo26n-pose.onnx --font data/fonts/overlay.ttf
```

### 2b. OBS plugin (macOS, Apple Silicon)

Requires the OBS dev dependencies (the [obs-plugintemplate] toolchain). ONNX
Runtime is fetched automatically and bundled into the `.plugin` with its install
name fixed to `@rpath`.

```bash
cmake --preset macos
cmake --build --preset macos
cp -R build_macos/RelWithDebInfo/obs-body-mask.plugin \
   ~/Library/Application\ Support/obs-studio/plugins/
```

Then in OBS: select a source → **Filters → + → Body Segmentation Mask**.

> Code signing: for local use the bundled dylib is ad-hoc signed. For
> distribution, re-sign with your Developer ID (hardened runtime) and notarize.

## Status

- ✅ Shared core + CLI: built and verified on Linux against real models
  (segmentation mask, background replace + blend, multi-person head labels).
- ⏳ OBS plugin layer: written against the OBS 31 API; **must be built and tested
  on a Mac** (cannot be compiled in a Linux-only environment).

## License

GPLv2 (links libobs). See `LICENSE` and `NOTICE` for third-party attributions
and model licenses.

[obs-plugintemplate]: https://github.com/obsproject/obs-plugintemplate
