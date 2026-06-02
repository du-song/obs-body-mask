#!/usr/bin/env bash
# Downloads the ONNX models and a label font into data/. Run once before
# building/running. These files are large/binary and are intentionally NOT
# committed to git (see .gitignore).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
models="$here/data/models"
fonts="$here/data/fonts"
mkdir -p "$models" "$fonts"

echo "==> MediaPipe Selfie Segmentation"
curl -fSL -o "$models/selfie_segmentation.onnx" \
  "https://huggingface.co/onnx-community/mediapipe_selfie_segmentation/resolve/main/onnx/model.onnx"

echo "==> Multi-person pose model (YOLO-pose, [1,56,8400] output)"
# Prefer YOLO11n-pose; fall back to the format-compatible YOLOv8n-pose.
if ! curl -fSL -o "$models/yolo11n-pose.onnx" \
      "https://huggingface.co/onnx-community/yolo11n-pose/resolve/main/onnx/model.onnx" 2>/dev/null; then
  echo "    yolo11n-pose not found, using format-compatible yolov8n-pose"
  curl -fSL -o "$models/yolo11n-pose.onnx" \
    "https://huggingface.co/Xenova/yolov8n-pose/resolve/main/onnx/model.onnx"
fi

echo "==> Label font"
# Bundle any sans TTF you like; this grabs a permissively-licensed one.
if [ ! -f "$fonts/overlay.ttf" ]; then
  curl -fSL -o "$fonts/overlay.ttf" \
    "https://github.com/google/fonts/raw/main/ofl/notosans/NotoSans%5Bwdth%2Cwght%5D.ttf" \
    || cp /usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf "$fonts/overlay.ttf" 2>/dev/null \
    || echo "    WARNING: provide a .ttf at $fonts/overlay.ttf"
fi

echo "Done. Models in $models, font in $fonts"
