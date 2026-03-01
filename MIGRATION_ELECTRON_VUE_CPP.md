# Electron + Vue + C++ Backend Migration (Phase 1)

This repository now contains a **frontend/backend split scaffold**:

- `backend/` — C++ HTTP service (`multicam_pose_backend`)
- `frontend/` — Electron + Vue desktop UI shell

## Why phase-1
A full replacement of the Qt UI with Electron+Vue while preserving all existing multi-camera calibration/tracking features is a significant migration. This phase establishes the architecture and API boundary first.

## Backend
### Build
```bash
cmake -S backend -B build_backend
cmake --build build_backend -j
```

### Run
```bash
./build_backend/multicam_pose_backend 18080
```

### API (initial)
- `GET /api/health`
- `GET /api/state`
- `POST /api/sources/add?path=...`
- `POST /api/sources/remove_last`
- `POST /api/play?sync=true`
- `POST /api/pause`
- `POST /api/step?delta=1`

## Frontend
### Install & run
```bash
cd frontend
npm install
npm run dev
# in another terminal
npm run dev:electron
```

## Next phases
1. Move OpenCV frame decode/processing pipeline into backend service modules.
2. Add streaming endpoint(s) (MJPEG/WebSocket/WebRTC) for real frames.
3. Implement per-source view components in Vue with pan/zoom/draw overlays.
4. Port calibration/tracking forms and project save/load endpoints.
