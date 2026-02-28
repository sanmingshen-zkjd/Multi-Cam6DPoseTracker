#include <QApplication>
#include <QString>
#include <iostream>
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
  QApplication app(argc, argv);  std::vector<InputSource> sources;
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
