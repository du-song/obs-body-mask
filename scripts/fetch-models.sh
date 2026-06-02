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

echo "==> PP-HumanSeg (OpenCV Zoo export; NCHW [1,3,192,192], 2-class softmax)"
# Selectable in the filter's "Segmentation Model" dropdown. Works with the
# core's plain [0,1] RGB normalization (verified to produce clean masks).
curl -fSL -o "$models/pphumanseg.onnx" \
  "https://github.com/opencv/opencv_zoo/raw/main/models/human_segmentation_pphumanseg/human_segmentation_pphumanseg_2023mar.onnx"

echo "==> Multi-person pose model (YOLO26n-pose, NMS-free [1,300,57] output)"
# YOLO26 ships .pt only, so export to ONNX via ultralytics. The PoseDetector
# also accepts the older [1,56,8400] YOLOv8/11 layout, so if export is not
# possible we fall back to the format-compatible YOLOv8n-pose ONNX.
if python3 - "$models" <<'PY'
import sys
try:
    from ultralytics import YOLO
except Exception:
    sys.exit(3)
m = YOLO("yolo26n-pose.pt")  # auto-downloads weights
p = m.export(format="onnx", opset=17, imgsz=640, dynamic=False, simplify=True)
import shutil, os
shutil.move(p, os.path.join(sys.argv[1], "yolo26n-pose.onnx"))
print("exported", p)
PY
then
  echo "    exported yolo26n-pose.onnx"
else
  echo "    ultralytics not available; using format-compatible yolov8n-pose"
  curl -fSL -o "$models/yolo26n-pose.onnx" \
    "https://huggingface.co/Xenova/yolov8n-pose/resolve/main/onnx/model.onnx"
fi

echo "==> YOLO11s-pose (alternate pose model; [1,56,8400] YOLOv8/11 layout)"
# Selectable as "YOLO11s-pose" in the filter. Prebuilt ONNX export (the nano
# pose variant has no clean public ONNX, so the small variant is used).
curl -fSL -o "$models/yolo11s-pose.onnx" \
  "https://huggingface.co/AXERA-TECH/YOLO11-Pose/resolve/main/yolo11s-pose.onnx"

echo "==> Label font"
# Bundle any sans TTF you like; this grabs a permissively-licensed one.
if [ ! -f "$fonts/overlay.ttf" ]; then
  curl -fSL -o "$fonts/overlay.ttf" \
    "https://github.com/google/fonts/raw/main/ofl/notosans/NotoSans%5Bwdth%2Cwght%5D.ttf" \
    || cp /usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf "$fonts/overlay.ttf" 2>/dev/null \
    || echo "    WARNING: provide a .ttf at $fonts/overlay.ttf"
fi

echo "Done. Models in $models, font in $fonts"
