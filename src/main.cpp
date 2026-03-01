#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QFont>
#include <QtGlobal>
#include <QString>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <vector>

#include <opencv2/opencv.hpp>
#include "MainWindow.h"
#include "SolveWorker.h"
#include "Types.h"

static void printUsage() {
  std::cout <<
R"(Usage:
  multicam_rig_toolkit_qt [--cam N]* [--video path]* --board W H --square S

Examples:
  ./multicam_rig_toolkit_qt --cam 0 --cam 1 --cam 2 --cam 3 --board 9 6 --square 0.025
  ./multicam_rig_toolkit_qt --video cam0.mp4 --video cam1.mp4 --board 9 6 --square 0.025
)";
}

static bool openSources(std::vector<InputSource>& sources) {
  for (auto& s : sources) {
    if (s.is_cam) s.cap.open(s.cam_id);
    else s.cap.open(s.video_path.toStdString());
    if (!s.cap.isOpened()) {
      std::cerr << "[ERR] Failed to open source: "
                << (s.is_cam ? ("cam "+std::to_string(s.cam_id)) : s.video_path.toStdString()) << "\n";
      return false;
    }
  }
  return true;
}

int main(int argc, char** argv) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
  QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
      Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif

  QApplication app(argc, argv);


  // Increase global UI font and control metrics for better readability/proportion.
  qreal dpiScale = 1.0;
  if (QScreen* screen = QGuiApplication::primaryScreen()) {
    dpiScale = std::max<qreal>(1.0, screen->logicalDotsPerInch() / 96.0);
  }

  QFont font = app.font();
  if (font.pointSizeF() > 0.0) {
    const qreal boosted = font.pointSizeF() * std::max<qreal>(1.20, dpiScale * 1.20);
    const qreal targetPt = std::max<qreal>(13.5, boosted);
    font.setPointSizeF(targetPt);
    app.setFont(font);
  } else if (font.pixelSize() > 0) {
    const int boosted = (int)std::lround(font.pixelSize() * std::max<qreal>(1.20, dpiScale * 1.20));
    const int targetPx = std::max(18, boosted);
    font.setPixelSize(targetPx);
    app.setFont(font);
  }

  // Keep button/input text proportionate to larger layouts.
  const int minControlH = std::max(34, (int)std::lround(34.0 * dpiScale));
  app.setStyleSheet(QString(
      "QMainWindow,QWidget{background:#20242b;color:#e8edf2;}"
      "QLabel{color:#e8edf2;}"
      "QPushButton,QToolButton,QComboBox,QSpinBox,QDoubleSpinBox{"
      "min-height:%1px;padding:4px 8px;background:#2d333b;color:#f0f4f8;border:1px solid #4a5563;border-radius:4px;}"
      "QPushButton:hover,QToolButton:hover{background:#384150;}"
      "QPushButton:pressed,QToolButton:pressed{background:#222831;}"
      "QTabBar::tab{padding:6px 12px;background:#2d333b;color:#e8edf2;}"
      "QSlider::groove:horizontal{height:8px;background:#3a4250;border-radius:4px;}"
      "QSlider::handle:horizontal{width:16px;margin:-5px 0;background:#7aa2f7;border-radius:8px;}")
      .arg(minControlH));

  std::vector<InputSource> sources;
  int board_w=-1, board_h=-1;
  double square=0.0;

  for (int i=1;i<argc;++i) {
    std::string a = argv[i];
    if (a=="--cam" && i+1<argc) {
      InputSource s; s.is_cam=true; s.cam_id=std::stoi(argv[++i]);
      sources.push_back(s);
    } else if (a=="--video" && i+1<argc) {
      InputSource s; s.is_cam=false; s.video_path=QString::fromUtf8(argv[++i]);
      sources.push_back(s);
    } else if (a=="--board" && i+2<argc) {
      board_w = std::stoi(argv[++i]);
      board_h = std::stoi(argv[++i]);
    } else if (a=="--square" && i+1<argc) {
      square = std::stod(argv[++i]);
    } else if (a=="--help" || a=="-h") {
      printUsage();
      return 0;
    }
  }

  // All parameters are OPTIONAL now; you can configure everything in the GUI.
  if (board_w<=0) board_w = 9;
  if (board_h<=0) board_h = 6;
  if (square<=0.0) square = 0.025;


  if (!sources.empty()) {
    if (!openSources(sources)) return 1;
  }

  MainWindow w(sources, board_w, board_h, square);

  w.show();
  return app.exec();
}
