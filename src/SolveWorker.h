#pragma once
#include <QObject>
#include <QDateTime>
#include <opencv2/opencv.hpp>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include "Core.h"
#include "Types.h"

struct PoseResult {
  bool ok=false;
  double t[3]{0,0,0};
  double aa[3]{0,0,0};
  int inliers=0;
  qint64 capture_ts_ms=0;
  qint64 solve_ts_ms=0;
  double latency_ms=0.0;
  int obs_count=0;
};

class SolveWorker : public QObject {
  Q_OBJECT
public:
  SolveWorker() = default;

  void setStaticData(const std::vector<CameraModel>* cams,
                     const std::unordered_map<uint64_t, Eigen::Vector3d>* tagmap)
  {
    cams_ = cams;
    tagmap_ = tagmap;
  }

  void setParams(int ransac_iters, double inlier_thresh_px, int dict_id, bool enabled) {
    ransac_iters_ = ransac_iters;
    inlier_thresh_px_ = inlier_thresh_px;
    dict_id_ = dict_id;
    pose_on_ = enabled;
  }

  void setInitPose(const Eigen::Matrix3d& R_wr, const Eigen::Vector3d& t_wr) {
    R_wr_ = R_wr;
    t_wr_ = t_wr;
  }

signals:
  void poseReady(const PoseResult& res);

public slots:
  void onFrames(FramePack frames, qint64 capture_ts_ms) {
    PoseResult out;
    out.capture_ts_ms = capture_ts_ms;
    if (!pose_on_ || !cams_ || !tagmap_ || !frames.size()) {
      out.solve_ts_ms = QDateTime::currentMSecsSinceEpoch();
      out.latency_ms = double(out.solve_ts_ms - out.capture_ts_ms);
      emit poseReady(out);
      return;
    }
    if ((int)cams_->size() != (int)frames.size()) {
      out.solve_ts_ms = QDateTime::currentMSecsSinceEpoch();
      out.latency_ms = double(out.solve_ts_ms - out.capture_ts_ms);
      emit poseReady(out);
      return;
    }

    std::vector<Observation> obs;
    AprilTagDetections det;
    buildObservationsFromFrames(frames, *tagmap_, obs, &det, dict_id_);
    out.obs_count = (int)obs.size();

    if (obs.size() >= 3) {
      auto res = estimatePoseRansac(*cams_, obs, R_wr_, t_wr_, ransac_iters_, inlier_thresh_px_);
      if (res.ok) {
        R_wr_ = res.R_wr;
        t_wr_ = res.t_wr;
        out.ok = true;
        out.inliers = (int)res.inliers.size();
        Eigen::AngleAxisd aa(res.R_wr);
        Eigen::Vector3d aavec = aa.axis() * aa.angle();
        out.t[0]=t_wr_.x(); out.t[1]=t_wr_.y(); out.t[2]=t_wr_.z();
        out.aa[0]=aavec.x(); out.aa[1]=aavec.y(); out.aa[2]=aavec.z();
      }
    }

    out.solve_ts_ms = QDateTime::currentMSecsSinceEpoch();
    out.latency_ms = double(out.solve_ts_ms - out.capture_ts_ms);
    emit poseReady(out);
  }

private:
  const std::vector<CameraModel>* cams_ = nullptr;
  const std::unordered_map<uint64_t, Eigen::Vector3d>* tagmap_ = nullptr;

  int ransac_iters_=280;
  double inlier_thresh_px_=3.0;
  int dict_id_=cv::aruco::DICT_APRILTAG_36h11;
  bool pose_on_=false;

  Eigen::Matrix3d R_wr_ = Eigen::Matrix3d::Identity();
  Eigen::Vector3d t_wr_ = Eigen::Vector3d::Zero();
};
