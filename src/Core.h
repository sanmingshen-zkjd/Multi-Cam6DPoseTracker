#pragma once
#include <opencv2/opencv.hpp>
#include <opencv2/aruco.hpp>
#include <ceres/ceres.h>
#include <ceres/rotation.h>
#include <Eigen/Core>
#include <Eigen/Geometry>

#include <unordered_map>
#include <random>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <algorithm>

// -----------------------------
// Data structures
// -----------------------------
struct CameraModel {
  cv::Mat K;      // 3x3 CV_64F
  cv::Mat dist;   // 1x5 CV_64F (k1 k2 p1 p2 k3)
  Eigen::Matrix3d R_cr = Eigen::Matrix3d::Identity(); // rig->cam
  Eigen::Vector3d t_cr = Eigen::Vector3d::Zero();     // rig->cam
};

struct Observation {
  int cam_id;
  Eigen::Vector3d Pw;
  Eigen::Vector2d uv;
};

// -----------------------------
// I/O
// -----------------------------
bool loadRigCalibYaml(const std::string& path, std::vector<CameraModel>& cams);
bool saveRigCalibYaml(const std::string& path, const std::vector<CameraModel>& cams);

bool loadTagCornersTxt(const std::string& path,
                       std::unordered_map<uint64_t, Eigen::Vector3d>& map_pts);

// -----------------------------
// Calibration (multi-cam chessboard)
// -----------------------------
struct ChessboardFrame {
  std::vector<std::vector<cv::Point2f>> corners; // per cam
  std::vector<bool> ok;                          // per cam
};

class MultiCamCalibrator {
public:
  MultiCamCalibrator(int num_cams, cv::Size board_size, double square_size_m);
  void reset();
  int captured() const;

  // Detect corners; if store=true, store this frame if any camera sees it
  bool detectAndMaybeStore(const std::vector<cv::Mat>& images,
                           bool store,
                           std::vector<std::vector<cv::Point2f>>* corners_out=nullptr,
                           std::vector<bool>* ok_out=nullptr) ;

  bool calibrate(const std::vector<cv::Size>& image_sizes,
                 std::vector<CameraModel>& out_cams,
                 double& out_mean_rms);

private:
  int num_cams_;
  cv::Size board_size_;
  double square_;
  std::vector<cv::Point3f> objp_;
  std::vector<ChessboardFrame> frames_;
};

// -----------------------------
// Tracking: AprilTag observations
// -----------------------------
struct AprilTagDetections {
  std::vector<std::vector<std::vector<cv::Point2f>>> corners_per_cam;
  std::vector<std::vector<int>> ids_per_cam;
};

bool buildObservationsFromFrames(
    const std::vector<cv::Mat>& frames,
    const std::unordered_map<uint64_t, Eigen::Vector3d>& tag_corner_map,
    std::vector<Observation>& obs_out,
    AprilTagDetections* det_out=nullptr,
    int dict_id=cv::aruco::DICT_APRILTAG_36h11);

// -----------------------------
// Pose estimation (RANSAC + Ceres LM)
// -----------------------------
struct RansacResult {
  bool ok=false;
  Eigen::Matrix3d R_wr = Eigen::Matrix3d::Identity();
  Eigen::Vector3d t_wr = Eigen::Vector3d::Zero();
  std::vector<int> inliers;
};

RansacResult estimatePoseRansac(const std::vector<CameraModel>& cams,
                               const std::vector<Observation>& obs,
                               const Eigen::Matrix3d& R_init,
                               const Eigen::Vector3d& t_init,
                               int iters=300,
                               double inlier_thresh_px=3.0);

// -----------------------------
// Helpers
// -----------------------------
uint64_t tagCornerKey(int tag_id, int corner_idx);
