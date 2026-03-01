#pragma once
#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <opencv2/opencv.hpp>
#include "MainWindow.h" // for InputSource
#include "Types.h"
#include <QMutex>


class CaptureWorker : public QObject {
  Q_OBJECT

public:
  CaptureWorker(std::vector<InputSource>* sources,
                std::vector<bool>* enabled,
                std::vector<cv::Mat>* last_frames,
                QMutex* mutex,
                int interval_ms=15)
    : sources_(sources), enabled_(enabled), last_frames_(last_frames), mutex_(mutex), interval_ms_(interval_ms) {}

public slots:
  void setSyncModeSlot(bool sync) { sync_mode_ = sync; }
  void setPlaybackRangeSlot(qint64 start, qint64 end_exclusive, double fps) {
    cur_frame_ = start;
    end_frame_ = end_exclusive;
    fps_ = (fps>0?fps:30.0);
    interval_ms_ = std::max(1, (int)std::lround(1000.0 / fps_));
    if (timer_ && timer_->isActive()) timer_->start(interval_ms_);
  }
  void setRunningSlot(bool running) {
    running_ = running;
    if (!timer_) return;
    if (!running_) timer_->stop();
    else timer_->start(interval_ms_);
  }

public:
  // legacy helpers (not thread-safe to call cross-thread)
  void setSyncMode(bool sync) { sync_mode_ = sync; }
  void setPlaybackRange(int64_t start, int64_t end_exclusive, double fps) {
    cur_frame_ = start;
    end_frame_ = end_exclusive;
    fps_ = fps;
    if (fps_ <= 0) fps_ = 30.0;
    interval_ms_ = std::max(1, (int)std::lround(1000.0 / fps_));
    if (timer_ && timer_->isActive()) { timer_->start(interval_ms_); }
  }
  void resetToStart() { cur_frame_ = 0; }

public slots:
  void start() {
    running_ = true;
    if (!timer_) {
      timer_ = new QTimer(this);
      connect(timer_, &QTimer::timeout, this, &CaptureWorker::tick);
    }
    timer_->start(interval_ms_);
  }
  void stop() {
    running_ = false;
    if (timer_) timer_->stop();
  }

signals:
  void framesReady(FramePack frames, qint64 capture_ts_ms);
private slots:
  void tick() {
    if (!running_) return;
    if (!sources_ || !enabled_) return;

    FramePack frames;
    {
      QMutexLocker locker(mutex_);
      if (sources_->empty()) return;

      if ((int)enabled_->size() != (int)sources_->size()) enabled_->assign(sources_->size(), true);
      if ((int)cache_frames_.size() != (int)sources_->size()) cache_frames_.resize(sources_->size());

      frames.reserve(sources_->size());

      for (int i=0;i<(int)sources_->size();++i) {
        auto& s = (*sources_)[i];

        if (!(*enabled_)[i]) {
          frames.push_back(cache_frames_[i]);
          continue;
        }

        cv::Mat f;
        if (s.is_image_seq) {
          if (s.seq_files.isEmpty()) {
            frames.push_back(cache_frames_[i]);
            continue;
          }
          int idx = sync_mode_ ? (int)cur_frame_ : s.seq_idx;
          idx = std::max(0, std::min(idx, (int)s.seq_files.size() - 1));
          f = cv::imread(s.seq_files[idx].toStdString(), cv::IMREAD_COLOR);
          s.seq_idx = idx;
          if (!sync_mode_ && s.seq_idx < (int)s.seq_files.size() - 1) s.seq_idx++;
        } else {
          if (!s.cap.isOpened()) {
            frames.push_back(cv::Mat());
            continue;
          }
          if (!s.cap.read(f) && !s.is_cam && sync_mode_) {
            // In sync mode we avoid per-frame random seeks (causes decode stalls/jitter).
            // If read fails at end, fallback logic below keeps last good frame.
          }
        }

        if (f.empty()) {
          // fallback to last good frame (may still be empty)
          frames.push_back(cache_frames_[i]);
        } else {
          cache_frames_[i] = f;
          frames.push_back(f);
        }
      }

      // Advance frame for sync playback (videos only)
      if (sync_mode_) {
        cur_frame_++;
        if (end_frame_ > 0 && cur_frame_ >= end_frame_) {
          // stop at end; UI can restart
          running_ = false;
          if (timer_) timer_->stop();
        }
      }
    }

    qint64 ts = QDateTime::currentMSecsSinceEpoch();
    emit framesReady(std::move(frames), ts);
  }

private:
  std::vector<InputSource>* sources_ = nullptr;
  std::vector<bool>* enabled_ = nullptr;
  std::vector<cv::Mat>* last_frames_ = nullptr; // legacy, no cross-thread writes
  std::vector<cv::Mat> cache_frames_;
  QMutex* mutex_ = nullptr;
  int interval_ms_=15;
  QTimer* timer_=nullptr;
  bool running_ = false;
  bool sync_mode_ = false;
  int64_t cur_frame_ = 0;
  int64_t end_frame_ = 0;
  double fps_ = 0;
};
