
#include "Core.h"

// ============================================================
// Keys + Tag map
// ============================================================
uint64_t tagCornerKey(int tag_id, int corner_idx) {
  return (uint64_t(uint32_t(tag_id)) << 32) | uint32_t(corner_idx);
}

bool loadTagCornersTxt(const std::string& path,
                       std::unordered_map<uint64_t, Eigen::Vector3d>& map_pts)
{
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    std::cerr << "[ERR] Failed to open tag corners: " << path << "\n";
    return false;
  }
  map_pts.clear();
  int tag_id, corner_idx;
  double x,y,z;
  while (ifs >> tag_id >> corner_idx >> x >> y >> z) {
    if (corner_idx < 0 || corner_idx > 3) continue;
    map_pts[tagCornerKey(tag_id, corner_idx)] = Eigen::Vector3d(x,y,z);
  }
  if (map_pts.empty()) {
    std::cerr << "[ERR] No tag corners loaded from: " << path << "\n";
    return false;
  }
  return true;
}

// ============================================================
// Rig calibration YAML
// ============================================================
bool saveRigCalibYaml(const std::string& path, const std::vector<CameraModel>& cams)
{
  cv::FileStorage fs(path, cv::FileStorage::WRITE);
  if (!fs.isOpened()) {
    std::cerr << "[ERR] Cannot write yaml: " << path << "\n";
    return false;
  }
  fs << "num_cams" << (int)cams.size();
  for (int i=0;i<(int)cams.size();++i) {
    std::string name = "cam" + std::to_string(i);
    fs << name << "{";
    fs << "K" << cams[i].K;
    fs << "dist" << cams[i].dist;

    Eigen::Matrix4d T = Eigen::Matrix4d::Identity();
    T.block<3,3>(0,0) = cams[i].R_cr;
    T.block<3,1>(0,3) = cams[i].t_cr;

    cv::Mat Tcv(4,4,CV_64F);
    for (int r=0;r<4;++r) for (int c=0;c<4;++c) Tcv.at<double>(r,c) = T(r,c);
    fs << "T_cr" << Tcv;
    fs << "}";
  }
  return true;
}

bool loadRigCalibYaml(const std::string& path, std::vector<CameraModel>& cams)
{
  cv::FileStorage fs(path, cv::FileStorage::READ);
  if (!fs.isOpened()) {
    std::cerr << "[ERR] Failed to open calib yaml: " << path << "\n";
    return false;
  }
  int num_cams=0;
  fs["num_cams"] >> num_cams;
  if (num_cams <= 0) {
    std::cerr << "[ERR] Invalid num_cams in yaml.\n";
    return false;
  }
  cams.resize(num_cams);
  for (int i=0;i<num_cams;++i) {
    std::string name = "cam" + std::to_string(i);
    cv::FileNode n = fs[name];
    if (n.empty()) {
      std::cerr << "[ERR] Missing node: " << name << "\n";
      return false;
    }
    n["K"] >> cams[i].K;
    n["dist"] >> cams[i].dist;
    cv::Mat Tcr;
    n["T_cr"] >> Tcr;
    if (cams[i].K.empty() || cams[i].dist.empty() || Tcr.empty()) return false;

    Eigen::Matrix4d T;
    for (int r=0;r<4;++r) for (int c=0;c<4;++c) T(r,c) = Tcr.at<double>(r,c);
    cams[i].R_cr = T.block<3,3>(0,0);
    cams[i].t_cr = T.block<3,1>(0,3);
  }
  return true;
}

// ============================================================
// Multi-camera calibrator
// ============================================================
MultiCamCalibrator::MultiCamCalibrator(int num_cams, cv::Size board_size, double square_size_m)
  : num_cams_(num_cams), board_size_(board_size), square_(square_size_m)
{
  objp_.clear();
  for (int y=0;y<board_size_.height;++y) {
    for (int x=0;x<board_size_.width;++x) {
      objp_.push_back(cv::Point3f(float(x*square_), float(y*square_), 0.0f));
    }
  }
}

void MultiCamCalibrator::reset() { frames_.clear(); }

int MultiCamCalibrator::captured() const { return (int)frames_.size(); }

bool MultiCamCalibrator::detectAndMaybeStore(
    const std::vector<cv::Mat>& images,
    bool store,
    std::vector<std::vector<cv::Point2f>>* corners_out,
    std::vector<bool>* ok_out)
{
  // Safety: avoid out-of-range when number of sources changes
  if ((int)images.size() != num_cams_) {
    return false;
  }
  ChessboardFrame f;
  f.corners.resize(num_cams_);
  f.ok.assign(num_cams_, false);

  bool any=false;
  for (int i=0;i<num_cams_;++i) {
    cv::Mat gray;
    if (images[i].channels()==3) cv::cvtColor(images[i], gray, cv::COLOR_BGR2GRAY);
    else gray = images[i];

    std::vector<cv::Point2f> corners;
    bool ok = cv::findChessboardCorners(
      gray, board_size_, corners,
      cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);

    if (ok) {
      cv::cornerSubPix(gray, corners, cv::Size(11,11), cv::Size(-1,-1),
                       cv::TermCriteria(cv::TermCriteria::EPS+cv::TermCriteria::COUNT, 30, 0.01));
      f.corners[i] = corners;
      f.ok[i] = true;
      any = true;
    }
  }

  if (corners_out) *corners_out = f.corners;
  if (ok_out) *ok_out = f.ok;

  if (store && any) frames_.push_back(std::move(f));
  return any;
}

bool MultiCamCalibrator::calibrate(const std::vector<cv::Size>& image_sizes,
                                   std::vector<CameraModel>& out_cams,
                                   double& out_mean_rms)
{
  if ((int)image_sizes.size()!=num_cams_) return false;
  out_cams.assign(num_cams_, CameraModel{});
  out_mean_rms = 0.0;

  // Intrinsics
  for (int cam=0; cam<num_cams_; ++cam) {
    std::vector<std::vector<cv::Point3f>> objpoints;
    std::vector<std::vector<cv::Point2f>> imgpoints;

    for (const auto& fr : frames_) {
      if (!fr.ok[cam]) continue;
      objpoints.push_back(objp_);
      imgpoints.push_back(fr.corners[cam]);
    }
    if (imgpoints.size() < 8) {
      std::cerr << "[ERR] Not enough detections for cam" << cam << "\n";
      return false;
    }

    out_cams[cam].K = cv::Mat::eye(3,3,CV_64F);
    out_cams[cam].dist = cv::Mat::zeros(1,5,CV_64F);
    std::vector<cv::Mat> rvecs, tvecs;

    double rms = cv::calibrateCamera(
      objpoints, imgpoints, image_sizes[cam],
      out_cams[cam].K, out_cams[cam].dist, rvecs, tvecs,
      cv::CALIB_RATIONAL_MODEL);

    if (out_cams[cam].dist.cols > 5) out_cams[cam].dist = out_cams[cam].dist.colRange(0,5).clone();
    out_mean_rms += rms;
  }
  out_mean_rms /= std::max(1, num_cams_);

  // Extrinsics relative to cam0
  out_cams[0].R_cr = Eigen::Matrix3d::Identity();
  out_cams[0].t_cr = Eigen::Vector3d::Zero();

  for (int cam=1; cam<num_cams_; ++cam) {
    std::vector<std::vector<cv::Point3f>> objpoints;
    std::vector<std::vector<cv::Point2f>> img0, img1;

    for (const auto& fr : frames_) {
      if (fr.ok[0] && fr.ok[cam]) {
        objpoints.push_back(objp_);
        img0.push_back(fr.corners[0]);
        img1.push_back(fr.corners[cam]);
      }
    }
    if (img0.size() < 8) {
      std::cerr << "[ERR] Not enough paired frames cam0-cam" << cam << "\n";
      return false;
    }

    cv::Mat R, T, E, F;
    cv::stereoCalibrate(
      objpoints, img0, img1,
      out_cams[0].K, out_cams[0].dist,
      out_cams[cam].K, out_cams[cam].dist,
      image_sizes[0],
      R, T, E, F,
      cv::CALIB_FIX_INTRINSIC,
      cv::TermCriteria(cv::TermCriteria::COUNT+cv::TermCriteria::EPS, 100, 1e-6));

    for (int r=0;r<3;++r) for (int c=0;c<3;++c) out_cams[cam].R_cr(r,c) = R.at<double>(r,c);
    out_cams[cam].t_cr << T.at<double>(0), T.at<double>(1), T.at<double>(2);
  }

  return true;
}

// ============================================================
// AprilTag detection -> observations
// ============================================================
bool buildObservationsFromFrames(
    const std::vector<cv::Mat>& frames,
    const std::unordered_map<uint64_t, Eigen::Vector3d>& tag_corner_map,
    std::vector<Observation>& obs_out,
    AprilTagDetections* det_out,
    int dict_id)
{
  obs_out.clear();
  if (det_out) {
    det_out->corners_per_cam.clear();
    det_out->ids_per_cam.clear();
  }

  auto dict = cv::aruco::getPredefinedDictionary(dict_id);
  cv::aruco::DetectorParameters params;
  cv::aruco::ArucoDetector detector(dict, params);

  for (int cam_id=0; cam_id<(int)frames.size(); ++cam_id) {
    cv::Mat gray;
    if (frames[cam_id].channels()==3) cv::cvtColor(frames[cam_id], gray, cv::COLOR_BGR2GRAY);
    else gray = frames[cam_id];

    std::vector<std::vector<cv::Point2f>> corners;
    std::vector<int> ids;
    detector.detectMarkers(gray, corners, ids);

    if (det_out) {
      det_out->corners_per_cam.push_back(corners);
      det_out->ids_per_cam.push_back(ids);
    }

    for (int k=0;k<(int)ids.size();++k) {
      int tag_id = ids[k];
      if (corners[k].size()!=4) continue;
      for (int c=0;c<4;++c) {
        auto it = tag_corner_map.find(tagCornerKey(tag_id, c));
        if (it == tag_corner_map.end()) continue;
        Observation o;
        o.cam_id = cam_id;
        o.Pw = it->second;
        o.uv = Eigen::Vector2d(corners[k][c].x, corners[k][c].y);
        obs_out.push_back(o);
      }
    }
  }
  return !obs_out.empty();
}

// ============================================================
// Pose estimation (distorted projection + Ceres)
// ============================================================
static inline bool projectDistorted(
    const CameraModel& cam,
    const Eigen::Matrix3d& R_wr,
    const Eigen::Vector3d& t_wr,
    const Eigen::Vector3d& Pw,
    Eigen::Vector2d& uv_out)
{
  Eigen::Matrix3d R_rw = R_wr.transpose();
  Eigen::Vector3d Pr = R_rw * (Pw - t_wr);
  Eigen::Vector3d Pc = cam.R_cr * Pr + cam.t_cr;

  const double X = Pc.x(), Y = Pc.y(), Z = Pc.z();
  if (Z <= 1e-8) return false;
  double x = X / Z;
  double y = Y / Z;

  const double k1 = cam.dist.at<double>(0,0);
  const double k2 = cam.dist.at<double>(0,1);
  const double p1 = cam.dist.at<double>(0,2);
  const double p2 = cam.dist.at<double>(0,3);
  const double k3 = cam.dist.at<double>(0,4);

  double r2 = x*x + y*y;
  double r4 = r2*r2;
  double r6 = r4*r2;
  double radial = 1.0 + k1*r2 + k2*r4 + k3*r6;

  double x_dist = x*radial + 2.0*p1*x*y + p2*(r2 + 2.0*x*x);
  double y_dist = y*radial + p1*(r2 + 2.0*y*y) + 2.0*p2*x*y;

  const double fx = cam.K.at<double>(0,0);
  const double fy = cam.K.at<double>(1,1);
  const double cx = cam.K.at<double>(0,2);
  const double cy = cam.K.at<double>(1,2);

  uv_out.x() = fx*x_dist + cx;
  uv_out.y() = fy*y_dist + cy;
  return true;
}

struct ReprojCostDist {
  ReprojCostDist(const CameraModel& cam, const Eigen::Vector3d& Pw, const Eigen::Vector2d& uv)
      : cam_(cam), Pw_(Pw), uv_(uv) {}

  template <typename T>
  bool operator()(const T* const aa_wr, const T* const t_wr, T* residuals) const {
    T Rwr[9];
    ceres::AngleAxisToRotationMatrix(aa_wr, Rwr);

    T Pw[3] = {T(Pw_.x()), T(Pw_.y()), T(Pw_.z())};
    T d[3]  = {Pw[0] - t_wr[0], Pw[1] - t_wr[1], Pw[2] - t_wr[2]};

    // Pr = Rwr^T * d
    T Pr[3];
    Pr[0] = Rwr[0]*d[0] + Rwr[3]*d[1] + Rwr[6]*d[2];
    Pr[1] = Rwr[1]*d[0] + Rwr[4]*d[1] + Rwr[7]*d[2];
    Pr[2] = Rwr[2]*d[0] + Rwr[5]*d[1] + Rwr[8]*d[2];

    Eigen::Matrix<T,3,3> Rcr;
    Rcr << T(cam_.R_cr(0,0)), T(cam_.R_cr(0,1)), T(cam_.R_cr(0,2)),
           T(cam_.R_cr(1,0)), T(cam_.R_cr(1,1)), T(cam_.R_cr(1,2)),
           T(cam_.R_cr(2,0)), T(cam_.R_cr(2,1)), T(cam_.R_cr(2,2));
    Eigen::Matrix<T,3,1> tcr(T(cam_.t_cr.x()), T(cam_.t_cr.y()), T(cam_.t_cr.z()));
    Eigen::Matrix<T,3,1> Pc = Rcr * Eigen::Matrix<T,3,1>(Pr[0],Pr[1],Pr[2]) + tcr;

    const T X = Pc.x(), Y = Pc.y(), Z = Pc.z();
    if (Z <= T(1e-8)) { residuals[0]=T(1e3); residuals[1]=T(1e3); return true; }

    T x = X / Z;
    T y = Y / Z;

    const T k1 = T(cam_.dist.at<double>(0,0));
    const T k2 = T(cam_.dist.at<double>(0,1));
    const T p1 = T(cam_.dist.at<double>(0,2));
    const T p2 = T(cam_.dist.at<double>(0,3));
    const T k3 = T(cam_.dist.at<double>(0,4));

    T r2 = x*x + y*y;
    T r4 = r2*r2;
    T r6 = r4*r2;
    T radial = T(1.0) + k1*r2 + k2*r4 + k3*r6;

    T x_dist = x*radial + T(2.0)*p1*x*y + p2*(r2 + T(2.0)*x*x);
    T y_dist = y*radial + p1*(r2 + T(2.0)*y*y) + T(2.0)*p2*x*y;

    const T fx = T(cam_.K.at<double>(0,0));
    const T fy = T(cam_.K.at<double>(1,1));
    const T cx = T(cam_.K.at<double>(0,2));
    const T cy = T(cam_.K.at<double>(1,2));

    T u = fx * x_dist + cx;
    T v = fy * y_dist + cy;

    residuals[0] = u - T(uv_.x());
    residuals[1] = v - T(uv_.y());
    return true;
  }

  static ceres::CostFunction* Create(const CameraModel& cam,
                                     const Eigen::Vector3d& Pw,
                                     const Eigen::Vector2d& uv) {
    return new ceres::AutoDiffCostFunction<ReprojCostDist, 2, 3, 3>(
        new ReprojCostDist(cam, Pw, uv));
  }

  const CameraModel& cam_;
  Eigen::Vector3d Pw_;
  Eigen::Vector2d uv_;
};

static bool solvePoseLM(const std::vector<CameraModel>& cams,
                        const std::vector<Observation>& obs,
                        const std::vector<int>& indices,
                        Eigen::Matrix3d& R_wr,
                        Eigen::Vector3d& t_wr,
                        int max_iters)
{
  Eigen::AngleAxisd aa_init(R_wr);
  Eigen::Vector3d aavec = aa_init.axis() * aa_init.angle();
  double aa_wr[3] = {aavec.x(), aavec.y(), aavec.z()};
  double t[3] = {t_wr.x(), t_wr.y(), t_wr.z()};

  ceres::Problem problem;
  for (int idx : indices) {
    const auto& o = obs[idx];
    ceres::CostFunction* cost = ReprojCostDist::Create(cams[o.cam_id], o.Pw, o.uv);
    problem.AddResidualBlock(cost, new ceres::HuberLoss(3.0), aa_wr, t);
  }

  ceres::Solver::Options options;
  options.linear_solver_type = ceres::DENSE_QR;
  options.max_num_iterations = max_iters;
  options.minimizer_progress_to_stdout = false;

  ceres::Solver::Summary summary;
  ceres::Solve(options, &problem, &summary);
  if (!summary.IsSolutionUsable()) return false;

  double Rm[9];
  ceres::AngleAxisToRotationMatrix(aa_wr, Rm);
  R_wr << Rm[0],Rm[1],Rm[2],
          Rm[3],Rm[4],Rm[5],
          Rm[6],Rm[7],Rm[8];
  t_wr = Eigen::Vector3d(t[0],t[1],t[2]);
  return true;
}

static double reprojErrPx(const std::vector<CameraModel>& cams,
                          const Observation& o,
                          const Eigen::Matrix3d& R_wr,
                          const Eigen::Vector3d& t_wr)
{
  Eigen::Vector2d uv_hat;
  if (!projectDistorted(cams[o.cam_id], R_wr, t_wr, o.Pw, uv_hat)) return 1e9;
  return (uv_hat - o.uv).norm();
}

RansacResult estimatePoseRansac(const std::vector<CameraModel>& cams,
                               const std::vector<Observation>& obs,
                               const Eigen::Matrix3d& R_init,
                               const Eigen::Vector3d& t_init,
                               int iters,
                               double inlier_thresh_px)
{
  RansacResult best;
  if (obs.size() < 3) return best;

  std::mt19937 rng(7);
  std::uniform_int_distribution<int> uni(0, (int)obs.size()-1);

  int best_cnt = -1;

  for (int k=0;k<iters;++k) {
    std::vector<int> sample;
    while ((int)sample.size() < 3) {
      int r = uni(rng);
      if (std::find(sample.begin(), sample.end(), r) == sample.end())
        sample.push_back(r);
    }

    Eigen::Matrix3d R = R_init;
    Eigen::Vector3d t = t_init;
    if (!solvePoseLM(cams, obs, sample, R, t, 60)) continue;

    std::vector<int> inl;
    inl.reserve(obs.size());
    for (int i=0;i<(int)obs.size();++i) {
      if (reprojErrPx(cams, obs[i], R, t) < inlier_thresh_px)
        inl.push_back(i);
    }

    if ((int)inl.size() > best_cnt) {
      best_cnt = (int)inl.size();
      best.ok = true;
      best.R_wr = R;
      best.t_wr = t;
      best.inliers = std::move(inl);
    }
  }

  if (!best.ok || best.inliers.size() < 3) return best;

  // Final refine on inliers
  Eigen::Matrix3d R = best.R_wr;
  Eigen::Vector3d t = best.t_wr;
  solvePoseLM(cams, obs, best.inliers, R, t, 120);
  best.R_wr = R;
  best.t_wr = t;
  return best;
}
