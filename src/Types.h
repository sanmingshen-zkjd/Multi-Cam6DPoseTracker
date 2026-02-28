#pragma once
#include <vector>
#include <opencv2/core.hpp>
#include <QMetaType>

// Global typedef used consistently in signals/slots
using FramePack = std::vector<cv::Mat>;

Q_DECLARE_METATYPE(cv::Mat)
Q_DECLARE_METATYPE(FramePack)
