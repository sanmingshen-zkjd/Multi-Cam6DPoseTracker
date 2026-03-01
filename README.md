# Multi-Camera Rig Toolkit (Qt GUI)

This is the **productized GUI** version (Qt Widgets) of:
- Multi-camera chessboard calibration (intrinsics + rig->cam extrinsics)
- AprilTag detection (OpenCV ArUco AprilTag)
- Multi-camera 6DoF rig pose estimation (RANSAC + Ceres LM)

Compared to the OpenCV-window version, this provides:
- A real app window with dockable panels
- Buttons for calibration and tracking actions
- Status bar, logs, and live multi-camera mosaic display
- Persistent settings (last used files, thresholds) via QSettings

---

## Dependencies
- Qt 6 Widgets (preferred) or Qt 5 Widgets
- OpenCV
- Eigen3
- Ceres Solver

Ubuntu example:
```bash
sudo apt-get install -y qt6-base-dev libopencv-dev libeigen3-dev libceres-dev
# If you're on Qt5:
# sudo apt-get install -y qtbase5-dev
```

---

## Build
```bash
mkdir -p build
cd build
cmake ..
cmake --build . -j
```

---

## Run

### Live cameras
```bash
./multicam_rig_toolkit_qt --cam 0 --cam 1 --cam 2 --cam 3 --board 9 6 --square 0.025
```

### Video files
```bash
./multicam_rig_toolkit_qt --video cam0.mp4 --video cam1.mp4 --video cam2.mp4 --video cam3.mp4 --board 9 6 --square 0.025
```

---

## Input files

### Tag 3D map (required for tracking pose)
Default path (can also load from UI):
- `tag_corners_world.txt`

Format each line:
```
tag_id corner_idx X Y Z
```
Corner order: 0 TL, 1 TR, 2 BR, 3 BL.

### Calibration YAML
Default path (can also load from UI):
- `rig_calib.yaml`

---

## UI overview

Left panel: **Sources**
- shows how many cameras/videos opened
- frame size and FPS estimate

Middle: **Live view**
- Mosaic display (2 columns by default)
- Overlays: chessboard corners / AprilTag outlines / text

Right panel: **Actions**
- Tab **Calibration**
  - Grab Frame (stores chessboard detections)
  - Reset
  - Calibrate + Save YAML
- Tab **Tracking**
  - Load Tag Map
  - Load Calibration YAML
  - Toggle Pose Estimation
  - Print pose/inliers

Status bar: current mode + capture count + inliers.

---

## Notes
- For best extrinsics: capture chessboard visible on cam0 and other cameras in the SAME frame (board static is OK).
- Pose estimation uses previous estimate as initialization for smoother tracking.


## New architecture option: Electron + Vue frontend + C++ backend

A migration scaffold has been added:
- Backend: `backend/` (C++ HTTP service)
- Frontend: `frontend/` (Electron + Vue app shell)

See `MIGRATION_ELECTRON_VUE_CPP.md` for build/run and migration phases.
