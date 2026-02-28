# Multi-Camera Rig Toolkit (Qt GUI) — Windows Package
目标环境：**Windows + Visual Studio 2019 (MSVC) + Qt 5.15**

本工程包含：
- 多相机棋盘格标定（内参 + rig->cam 外参）
- AprilTag 检测（OpenCV ArUco AprilTag 36h11）
- 多相机 6DoF 位姿估计（RANSAC + Ceres LM）
- Qt Widgets 产品化 GUI（动作面板、日志、状态栏、文件对话框、设置记忆）

---

## 1) 推荐依赖方案：vcpkg（最省事）
> 你只需要确保 Qt 版本是 5.15（可以固定 vcpkg baseline / 或用你本机 Qt 5.15 安装）。  
> 如果你已经有 Qt 5.15（官方安装包），可看“方案B”。

### A. 安装 vcpkg
```bat
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
cd /d C:\vcpkg
bootstrap-vcpkg.bat
```

### B. 安装依赖（x64）
```bat
C:\vcpkg\vcpkg.exe install opencv eigen3 ceres qt5-base --triplet x64-windows
```
> 说明：vcpkg 的 Qt5 包名在不同版本可能略有差异（如 `qt5-base` / `qt5`）。如报错，运行 `vcpkg search qt5` 查看实际名称。

### C. 生成与编译（VS2019）
在工程根目录执行：
```bat
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Release
```

运行：
```bat
build\Release\multicam_rig_toolkit_qt.exe --cam 0 --cam 1 --board 9 6 --square 0.025
```

### D. 发布运行时（Qt DLL）
用 Qt 的 `windeployqt`（来自你 Qt 5.15 安装目录）：
```bat
C:\Qt\5.15.2\msvc2019_64\bin\windeployqt.exe build\Release\multicam_rig_toolkit_qt.exe
```

如果 OpenCV/Ceres DLL 缺失，把 vcpkg bin 加入 PATH：
```bat
set PATH=%PATH%;C:\vcpkg\installed\x64-windows\bin
```

---

## 2) 方案B：你已安装 Qt 5.15 + OpenCV（高级）
如果你用官方 Qt 5.15 安装包（msvc2019_64）和你自己的 OpenCV：
- 设置 `Qt5_DIR` 指向 `.../lib/cmake/Qt5`
- 设置 `OpenCV_DIR` 指向包含 `OpenCVConfig.cmake` 的目录
- Ceres/Eigen 推荐仍用 vcpkg（最稳）

例：
```bat
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 ^
  -DQt5_DIR="C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5" ^
  -DOpenCV_DIR="C:\opencv\build" ^
  -DCeres_DIR="C:\vcpkg\installed\x64-windows\share\ceres"
cmake --build build --config Release
```

---

## 3) 运行所需文件
- `rig_calib.yaml`：在 Calibration 页点击 “Calibrate + Save” 生成/保存
- `tag_corners_world.txt`：在 Tracking 页点击 “Load Tag Map (TXT)” 加载

Tag Map 格式（每行）：
```
tag_id corner_idx X Y Z
```
corner 顺序：0左上 1右上 2右下 3左下

---

## 4) 程序参数
### 摄像头输入
```bat
multicam_rig_toolkit_qt.exe --cam 0 --cam 1 --cam 2 --cam 3 --board 9 6 --square 0.025
```

### 视频输入
```bat
multicam_rig_toolkit_qt.exe --video cam0.mp4 --video cam1.mp4 --board 9 6 --square 0.025
```

---

## 5) 常见坑
- **全部统一 x64**（VS、Qt、OpenCV、vcpkg triplet）
- 摄像头索引不一定从 0 开始，若打不开换 0/1/2...
- 若 Qt 报 “platform plugin windows”，通常是没正确 `windeployqt`

