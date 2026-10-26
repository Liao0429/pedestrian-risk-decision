[简体中文](README.md) | **English**

# Lightweight Edge Pedestrian Perception & Risk Decision System (C++)

> **Final-year project**: Real-time pedestrian detection, crowd-gathering analysis and risk-level warning on Luckfox Pico Max (RV1106, 1 TOPS NPU, 256MB).
> Uses a hybrid pipeline: **lightweight perception + geometric correction + graph-topology analysis + temporal risk decision**.

## ✨ Features

- **Lightweight detection**: YOLOv5n + INT8 quantized, real-time pedestrian detection on edge NPU (C++/RKNN)
- **Geometric correction**: Inverse Perspective Mapping (IPM) maps pixel foot-points to ground metric coordinates, removing perspective distortion
- **Complex network topology**: adjacency matrix + Graph Laplacian network features (density / largest connected component ratio / spectral) drive risk scoring; DBSCAN clustering module is standalone with unit tests (optional extension, not in the main loop)
- **Temporal decision**: multi-object Kalman tracking (nearest-neighbor association) + FSM finite-state machine (hysteresis / debounce / DANGER hold); ByteTrack/EMA modules are standalone with unit tests (optional extension)
- **Risk grading**: real-time **SAFE / WARNING / DANGER** three-level output
- **Configurable**: rule thresholds loaded from JSON (`config/default_config.json`)
- **Edge performance**: NEON-optimized preprocessing (CPU preprocess 136ms→33ms), **7.46 FPS** measured, ~11MB RSS, 1h+ long-run with zero restart (watchdog + bounded timeouts)
- **Extensible hardware**: dual-servo gimbal, voice alert, color touchscreen, physical switch (`docs/hardware/`)

## 🏗️ System Architecture

```mermaid
graph TD
 A[Camera<br/>720×480] --> B["YOLOv5n Pedestrian Detection<br/>RKNN · INT8 · NPU"]
 B --> C["IPM Inverse Perspective Mapping<br/>pixel foot-point → ground metric"]
 C --> D["Complex Network Topology<br/>Adjacency + Graph Laplacian features<br/>n / density / r_gcc / spectral"]
 D --> E["EnhancedDecision Module<br/>Kalman smoothing + FSM temporal decision"]
 E --> F["Risk output<br/>SAFE / WARNING / DANGER"]
 F --> G["Overlay + RTSP streaming"]
 G --> H[Monitoring client]
```

**Pipeline**: lightweight perception (YOLOv5n+INT8) → geometric correction (IPM) → graph topology analysis (Graph Laplacian features) → temporal risk decision (Kalman/FSM) → three-level risk output with RTSP streaming. Detailed architecture: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## 📁 Directory Structure

```
pedestrian-risk-decision/
├── dataset/ # Real datasets
│ ├── images500/ # 500 real COCO images with pedestrians (1–19 people)
│ └── videos50/ # 111 real pedestrian/crowd videos (Mixkit, validated)
├── scripts/ # Test scripts
│ ├── crowd_test.py # image mode (baseline)
│ ├── crowd_test_enhanced.py # video mode (Kalman + FSM)
├── src/
│ ├── algorithms/ # Algorithm modules (Graph Laplacian/Kalman/ByteTrack/DBSCAN/FSM/IPM/EMA, Python + C++)
│ ├── hardware/ # Hardware drivers (servo/pan_tilt/voice_alert)
│ └── luckfox_rtsp_yolov5/ # On-board C++ project (RKNN, NEON-optimized)
├── config/ # Threshold config
├── results/ # Experimental results (image baseline + video enhanced + ablation + edge performance)
├── tests/ # Algorithm unit tests (all pass)
├── docs/ # System principle / architecture / algorithms / hardware / demo / work log
└── config/default_config.json
```

## 📊 Real Datasets

- **Images**: 500 real COCO 2017 images with pedestrians, covering 1–19 people (dispersed 200 / gathered 150 / fairly crowded 138 / crowded 12), with ground-truth person counts
- **Videos**: 111 real Mixkit pedestrian/crowd videos (360p, 3.9–50 s), dual-validated by ffprobe + HOG (111/111 passed)
- All real data, no synthetic/fabricated content; download & validation scripts in `dataset/_work/` and `dataset/videos50/`

## 🧪 Experimental Results (real)

### Image baseline (500 images)
| Metric | Result |
|---|---|
| Overall detection rate | **82.5%** |
| High-density (14+ people) detection rate | **79.4%** |
| Level distribution | SAFE 414 / WARNING 61 / DANGER 25 |

### Video enhancement (111 real videos, Kalman + FSM, full rerun)
- **All 111 real videos (45021 frames)**: level distribution **SAFE 25669 / WARNING 11545 / DANGER 7807**, average **4.00 level switches per video** (FSM hysteresis stabilizes), 40 videos reached DANGER, **31 videos stayed SAFE throughout** (no alert; without ground truth we cannot judge whether these are correct no-alerts or missed alerts)
- Dense crowd (`mixkit_13192`): **DANGER 179 / WARNING 120 / SAFE 6** (305 frames), only 4 switches (fair baseline AND-rule: 48 switches)
- Sparse/empty scene (`mixkit_50134`, normal lighting): SAFE throughout, no alert (no person present)
- **Before/after (fair baseline, same rule as real engine)**: average switches **23.2 → 4.0 (↓82.8%)**; 24 baseline videos reached DANGER vs 40 enhanced; DANGER frames 2190 → 7807 (FSM score includes density weight and hold mechanism — more sensitive and more stable than per-frame rule). Details: `results/rerun/_comparison_summary.json`

See `docs/figures/` for performance comparison charts.

### Ablation study (component contributions)

![Ablation](docs/figures/fig_ablation.png)

- **A baseline (no enhancement)**: 35.2 average switches
- **B +Kalman smoothing**: → 11.7 switches (↓67%)
- **C +Kalman+spectral**: identical to B (11.7) — spectral features are computed/displayed but NOT used in risk scoring; an integration experiment (M1/M2, offline re-run on 111 videos) showed no independent semantic gain, **rejected by decision**; kept as display-only
- **D full (+FSM temporal decision)**: → 4.0 switches (hysteresis / confirm frames / DANGER hold)

Details: `results/rerun/_ablation.json` and `docs/figures/fig_ablation.png`.

### ⚡ Edge performance (Luckfox Pico Max / RV1106)

| Metric | Result |
|---|---|
| Frame rate (after NEON) | **6.30 → 7.46 FPS (+18%)** |
| CPU preprocessing (letterbox+RGB) | **136ms → 33ms (-76%, NEON)** |
| Memory (RSS) | **~10.5-10.9MB** |
| Long-run (systemd watchdog + bounded timeouts) | **1h+ zero restart, zero error** (t0~t4 snapshots) |
| On-board golden tests | **174/174 passed** (NEON vs scalar) |
| Main-loop protection | VI/VENC 1s bounded timeout; 30 consecutive failures → exit, watchdog restarts |

Performance profiling logs & long-run snapshots: `results/edge_board/`.

## 🚀 Deployment

### PC-side testing (GPU, for experiments)
```bash
# Image baseline
python scripts/crowd_test.py --input dataset/images500 --output results/images500_baseline --device 0
# Video enhancement (Kalman + FSM)
python scripts/crowd_test_enhanced.py --input <video.mp4> --output results/video_enhanced --spectral --device 0
```

### On-board deployment (Luckfox RV1106, C++/RKNN)
1. Cross-compile `src/luckfox_rtsp_yolov5/` on an Ubuntu host (see `docs/ARCHITECTURE.md` and `docs/hardware/`)
2. scp the binary to the board, run `sudo ./luckfox_pico_rtsp_yolov5` (systemd-managed + watchdog auto-restart)
3. RTSP stream: `rtsp://<board-ip>:554/live/0`
4. Algorithm modules (graph/Kalman/FSM) C++ headers in `src/algorithms/*.h`, cross-compilable

## 📚 Documentation Index

| Doc | Content |
|---|---|
| `docs/工作日志.md` | Development work log (time / change / commit / progress) |
| `docs/SYSTEM_PRINCIPLE.md` | System principle & algorithm details |
| `docs/ARCHITECTURE.md` | System architecture & module overview |
| `docs/algorithms.md` | Algorithm design (C++/Python dual impl, golden regression) |
| `docs/WEB_VIEWER.md` | Browser-based live monitoring (go2rtc) |
| `docs/DEMO_VIDEO.md` | Real detection demo videos |
| `docs/LUCKFOX_VOICE_ASSISTANT_PLAN.md` | Voice assistant LLM plan (cloud/local) |
| `docs/hardware/` | Hardware extensions (servo/voice/display/switch/PCB) |

## 🧩 Extensible Modules

- **Voice assistant LLM** (cloud/local): `docs/LUCKFOX_VOICE_ASSISTANT_PLAN.md`
- **PCB backplane** (JLCPCB): `docs/hardware/PCB_BOARD_DESIGN.md`
- **DBSCAN crowd clustering / ByteTrack tracking / EMA smoothing**: standalone implementations with unit tests (`src/algorithms/`), ready to be integrated into the main loop

## 📌 Notes

- This repository is **private**; the author will make it public manually after the project is complete.
- Thesis/defense/review internal documents are kept **locally only** (`H:\毕设论文\`) and NOT uploaded to this repository.
- All experimental data comes from real datasets and is reproducible.