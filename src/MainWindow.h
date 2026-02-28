#pragma once
#include <QMainWindow>
#include <QThread>
#include <QMenuBar>
#include <QAction>
#include <QDockWidget>
#include <QTimer>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTabWidget>
#include <QListWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QStatusBar>
#include <QGroupBox>
#include <QGridLayout>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>

#include <opencv2/opencv.hpp>
#include "Core.h"
#include "SolveWorker.h"
#include "Types.h"
#include <QMutex>

struct InputSource {
  bool is_cam=false;
  int cam_id=-1;
  QString video_path;
  cv::VideoCapture cap;
};

class CaptureWorker;
class SolveWorker;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  MainWindow(const std::vector<InputSource>& sources,
             int board_w, int board_h, double square_m,
             QWidget* parent=nullptr);
  ~MainWindow();

private slots:
  void onTick();

  // Sources actions
  void onAddCamera();
  void onAddVideo();
  void onRemoveSource();
  void onPauseResumeSelected();
  void onPlayAll();
  void onStopAll();

  // Calibration actions
  void onGrabFrame();
  void onResetFrames();
  void onCalibrateAndSave();

  // Tracking actions
  void onLoadTagMap();
  void onLoadCalibYaml();
  void onTogglePose(bool on);
  void onPrintPose();
  void onExportTrajectory();
  void onSaveProject();
  void onLoadProject();
  void onSaveLayout();
  void onRestoreLayout();
  void onToggleDocks(bool on);

  void onFramesFromWorker(FramePack frames, qint64 capture_ts_ms);
  void onPoseFromWorker(const PoseResult& res);

  // UI mode
  void onModeCalibration();
  void onModeTracking();

private:
  void buildUI();
  void logLine(const QString& s);
  bool openAllSources();
  void closeAllSources();
  void rebuildCalibratorFromUI(bool reset=true);
  void refreshSourceList();
  void rebuildSourceDocks();
  void updateSourceDocks(const std::vector<cv::Mat>& frames);
  void stopCaptureBlocking();
  void updatePlaybackParams();
  int videoSourceCount() const;
  void setSourceEnabled(int idx, bool enabled);
  bool readFrames(std::vector<cv::Mat>& frames);
  void updateFpsStats(double dt_ms);
  QJsonObject toProjectJson() const;
  bool fromProjectJson(const QJsonObject& o);

  static QImage matToQImage(const cv::Mat& bgr);
  static cv::Mat makeMosaic(const std::vector<cv::Mat>& imgs, int cols=2);

  void overlayCalibration(std::vector<cv::Mat>& vis, const std::vector<cv::Mat>& frames);
  void overlayTracking(std::vector<cv::Mat>& vis, const std::vector<cv::Mat>& frames);

  void updateStatus();

private:
  // Inputs
  std::vector<InputSource> sources_;
  std::vector<bool> source_enabled_;
  std::vector<cv::Mat> last_frames_;
  int num_cams_=0;

  // Calib params
  int board_w_=0, board_h_=0;
  double square_=0.0;

  // Core
  std::unique_ptr<MultiCamCalibrator> calibrator_;
  std::vector<CameraModel> cams_;
  bool calib_loaded_=false;

  std::unordered_map<uint64_t, Eigen::Vector3d> tag_corner_map_;
  bool tagmap_loaded_=false;
  QString tagmap_path_;
  QString calib_path_;

  // Pose state
  bool pose_on_=false;
  Eigen::Matrix3d R_wr_ = Eigen::Matrix3d::Identity();
  Eigen::Vector3d t_wr_ = Eigen::Vector3d::Zero();
  int last_inliers_=0;
  struct TrajRow { qint64 t_ms; Eigen::Vector3d t; Eigen::Vector3d aa; int inliers; };
  std::vector<TrajRow> traj_;
  int ransac_iters_=280;
  double inlier_thresh_px_=3.0;
  int tag_dict_id_=cv::aruco::DICT_APRILTAG_36h11;
  double fps_=0.0;
  qint64 last_tick_ms_=0;

  // Mode
  enum Mode { CALIB=0, TRACK=1 } mode_=CALIB;

  // UI widgets
  QLabel* viewLabel_=nullptr;
  QTextEdit* log_=nullptr;

  // Per-source dock views
  bool show_docks_ = false; // default OFF to avoid duplicate display
  std::vector<QDockWidget*> camDocks_;
  std::vector<QLabel*> camLabels_;

  // Menu actions
  QAction* actSaveProject_=nullptr;
  QAction* actLoadProject_=nullptr;
  QAction* actSaveLayout_=nullptr;
  QAction* actRestoreLayout_=nullptr;
  QAction* actExportTraj_=nullptr;
  QAction* actToggleDocks_=nullptr;


  // Sources panel
  QListWidget* sourceList_=nullptr;
  QPushButton* btnAddCam_=nullptr;
  QPushButton* btnAddVideo_=nullptr;
  QPushButton* btnRemoveSource_=nullptr;
  QPushButton* btnPauseResume_=nullptr;
  QPushButton* btnPlayAll_=nullptr;
  QPushButton* btnStopAll_=nullptr;
  //QCheckBox* chkSyncPlay_=nullptr;
  //QLabel* lblPlayState_=nullptr;
  QPushButton* btnSaveProject_=nullptr;
  QPushButton* btnLoadProject_=nullptr;
  QLabel* lblSources_=nullptr;


  QPushButton* btnModeCalib_=nullptr;
  QPushButton* btnModeTrack_=nullptr;

  // Calibration tab
  QSpinBox* spBoardW_=nullptr;
  QSpinBox* spBoardH_=nullptr;
  QDoubleSpinBox* spSquare_=nullptr;
  QPushButton* btnGrab_=nullptr;
  QPushButton* btnReset_=nullptr;
  QPushButton* btnCalibrate_=nullptr;
  QLabel* lblCaptured_=nullptr;

  // Tracking tab
  QPushButton* btnLoadTag_=nullptr;
  QPushButton* btnExportTraj_=nullptr;
  QSpinBox* spRansacIters_=nullptr;
  QDoubleSpinBox* spInlierThresh_=nullptr;
  QComboBox* cbTagDict_=nullptr;
  QLabel* lblFps_=nullptr;
  QLabel* lblLatency_=nullptr;
  QPushButton* btnLoadYaml_=nullptr;
  QLabel* lblTagPath_=nullptr;
  QLabel* lblYamlPath_=nullptr;
  QCheckBox* chkPose_=nullptr;
  QPushButton* btnPrintPose_=nullptr;
  QLabel* lblInliers_=nullptr;

  // Settings
  QSettings settings_;

  // Threads
  QThread captureThread_;
  QThread solveThread_;
  CaptureWorker* captureWorker_=nullptr;
  SolveWorker* solveWorker_=nullptr;
  QMutex sources_mutex_;
  QMutex frames_mutex_;
  qint64 last_capture_ts_ms_=0;
  bool playback_running_=false;
  bool sync_play_=true;
  int64_t play_frame_=0;
  int64_t play_end_frame_=0;
  double play_fps_=30.0;
  int ui_frame_skip_=0;
  int ui_overlay_div_=4; // run heavy overlay every N UI ticks

  // Timer (UI refresh)
  QTimer timer_;
};
