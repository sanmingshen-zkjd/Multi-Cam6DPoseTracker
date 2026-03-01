
#include "MainWindow.h"
#include "CaptureWorker.h"
#include "SolveWorker.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDockWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QDateTime>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QScrollArea>
#include <QApplication>
#include <QHeaderView>
#include <QTabBar>
#include <QStyle>
#include <QFrame>
#include <QIntValidator>

static QString nowStr() {
  return QDateTime::currentDateTime().toString("hh:mm:ss");
}

static QStringList kImageNameFilters() {
  return {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tif", "*.tiff"};
}


ImageViewer::ImageViewer(QWidget* parent) : QGraphicsView(parent) {
  setScene(&scene_);
  setRenderHint(QPainter::Antialiasing, true);
  setBackgroundBrush(QColor(17,17,17));
  setFrameShape(QFrame::StyledPanel);
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorViewCenter);
  setDragMode(QGraphicsView::ScrollHandDrag);
  pixmapItem_ = scene_.addPixmap(QPixmap());
}

void ImageViewer::setImage(const QImage& img) {
  if (img.isNull()) {
    pixmapItem_->setPixmap(QPixmap());
    return;
  }
  pixmapItem_->setPixmap(QPixmap::fromImage(img));
  scene_.setSceneRect(pixmapItem_->boundingRect());
  if (zoomFactor_ == 1.0) {
    resetTransform();
    fitInView(pixmapItem_, Qt::KeepAspectRatio);
  }
}

void ImageViewer::setToolMode(ToolMode mode) {
  toolMode_ = mode;
  if (toolMode_ == PanTool) {
    setCursor(Qt::OpenHandCursor);
    setDragMode(QGraphicsView::ScrollHandDrag);
  } else {
    setCursor(Qt::CrossCursor);
    setDragMode(QGraphicsView::NoDrag);
  }
  if (!lineDrawing_ && previewLine_) {
    scene_.removeItem(previewLine_);
    delete previewLine_;
    previewLine_ = nullptr;
  }
}

void ImageViewer::zoomIn() { applyZoom(1.15); }
void ImageViewer::zoomOut() { applyZoom(1.0/1.15); }

void ImageViewer::resetView() {
  resetTransform();
  zoomFactor_ = 1.0;
  if (!pixmapItem_->pixmap().isNull()) fitInView(pixmapItem_, Qt::KeepAspectRatio);
}

void ImageViewer::clearAnnotations() {
  const auto items = scene_.items();
  for (auto* it : items) {
    if (it == pixmapItem_) continue;
    scene_.removeItem(it);
    delete it;
  }
  previewLine_ = nullptr;
  lineDrawing_ = false;
}

void ImageViewer::applyZoom(double factor) {
  zoomFactor_ *= factor;
  zoomFactor_ = std::max(0.1, std::min(zoomFactor_, 20.0));
  scale(factor, factor);
}

void ImageViewer::wheelEvent(QWheelEvent* e) {
  if (e->angleDelta().y() > 0) zoomIn();
  else zoomOut();
}

void ImageViewer::mousePressEvent(QMouseEvent* e) {
  if (toolMode_ == PanTool) {
    QGraphicsView::mousePressEvent(e);
    return;
  }
  if (e->button() != Qt::LeftButton || pixmapItem_->pixmap().isNull()) return;

  QPointF p = mapToScene(e->pos());
  if (!sceneRect().contains(p)) return;

  if (toolMode_ == PointTool) {
    scene_.addEllipse(p.x()-3, p.y()-3, 6, 6, QPen(QColor(255,80,80), 2), QBrush(QColor(255,80,80)));
    setToolMode(PanTool);
    return;
  }

  if (toolMode_ == LineTool) {
    if (!lineDrawing_) {
      lineDrawing_ = true;
      lineStart_ = p;
      previewLine_ = scene_.addLine(QLineF(lineStart_, lineStart_), QPen(QColor(80,220,255), 2));
      emit linePreviewText("Line: start");
    } else {
      scene_.addLine(QLineF(lineStart_, p), QPen(QColor(80,220,255), 2));
      if (previewLine_) {
        scene_.removeItem(previewLine_);
        delete previewLine_;
        previewLine_ = nullptr;
      }
      lineDrawing_ = false;
      setToolMode(PanTool);
    }
  }
}

void ImageViewer::mouseMoveEvent(QMouseEvent* e) {
  if (toolMode_ == LineTool && lineDrawing_ && previewLine_) {
    QPointF p = mapToScene(e->pos());
    previewLine_->setLine(QLineF(lineStart_, p));
    return;
  }
  QGraphicsView::mouseMoveEvent(e);
}

void ImageViewer::resizeEvent(QResizeEvent* e) {
  QGraphicsView::resizeEvent(e);
  if (zoomFactor_ == 1.0 && pixmapItem_ && !pixmapItem_->pixmap().isNull()) {
    resetTransform();
    fitInView(pixmapItem_, Qt::KeepAspectRatio);
  }
}

MainWindow::MainWindow(const std::vector<InputSource>& sources,
                       int board_w, int board_h, double square_m,
                       QWidget* parent)
  : QMainWindow(parent),
    sources_(sources),
    num_cams_((int)sources.size()),
    board_w_(board_w),
    board_h_(board_h),
    square_(square_m),
    settings_("YourCompany", "MultiCamRigToolkit")
{
    setWindowTitle("Multi-Camera Rig Toolkit (Qt)");
    resize(1280, 800);

    calibrator_.reset(new MultiCamCalibrator(std::max(1,num_cams_), cv::Size(board_w_, board_h_), square_));

    source_enabled_.assign(std::max(0,num_cams_), true);
    last_frames_.resize(std::max(0,num_cams_));
    buildUI();
    connect(&timer_, &QTimer::timeout, this, &MainWindow::onTick);
    timer_.start(33);

    // Start capture/solve threads
    captureWorker_ = new CaptureWorker(&sources_, &source_enabled_, &last_frames_, &sources_mutex_, 33);
    captureWorker_->moveToThread(&captureThread_);
    connect(&captureThread_, &QThread::started, captureWorker_, &CaptureWorker::start);
    connect(this, &MainWindow::destroyed, captureWorker_, &CaptureWorker::stop);

    solveWorker_ = new SolveWorker();
    solveWorker_->setStaticData(&cams_, &tag_corner_map_);
    solveWorker_->setParams(ransac_iters_, inlier_thresh_px_, tag_dict_id_, pose_on_);
    solveWorker_->setInitPose(R_wr_, t_wr_);
    solveWorker_->moveToThread(&solveThread_);

    // Wire signals
    connect(captureWorker_, &CaptureWorker::framesReady, this, &MainWindow::onFramesFromWorker, Qt::QueuedConnection);
    connect(captureWorker_, &CaptureWorker::framesReady, solveWorker_, &SolveWorker::onFrames, Qt::QueuedConnection);
    connect(solveWorker_, &SolveWorker::poseReady, this, &MainWindow::onPoseFromWorker, Qt::QueuedConnection);

    captureThread_.start();
    solveThread_.start();
}

MainWindow::~MainWindow() 
{
    timer_.stop();
    // Stop workers/threads
    if (captureWorker_) 
        captureWorker_->stop();
    captureThread_.quit();
    captureThread_.wait();
    solveThread_.quit();
    solveThread_.wait();
    delete solveWorker_;
    solveWorker_ = nullptr;
    delete captureWorker_;
    captureWorker_ = nullptr;

    for (auto& s : sources_) 
    {
        if (s.cap.isOpened()) 
            s.cap.release();
    }
}

void MainWindow::buildUI() {
    // Central view
    QWidget* central = new QWidget(this);
    QVBoxLayout* v = new QVBoxLayout(central);

    // Top mode tabs
    modeTabs_ = new QTabWidget(central);
    modeTabs_->addTab(new QWidget(modeTabs_), "Capture");
    modeTabs_->addTab(new QWidget(modeTabs_), "Calibration");
    modeTabs_->addTab(new QWidget(modeTabs_), "Tracking");
    modeTabs_->setCurrentIndex(0);
    v->addWidget(modeTabs_);
    connect(modeTabs_, &QTabWidget::currentChanged, this, &MainWindow::onModeTabChanged);

    QHBoxLayout* topSourceBar = new QHBoxLayout();
    btnAddCam_ = new QToolButton(central);
    btnAddVideo_ = new QToolButton(central);
    btnAddImgSeq_ = new QToolButton(central);
    btnRemoveSource_ = new QToolButton(central);
    btnAddCam_->setText("AddCamera");
    btnAddVideo_->setText("AddVideo");
    btnAddImgSeq_->setText("AddImageSeq");
    btnRemoveSource_->setText("Remove");
    btnAddCam_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btnAddVideo_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btnAddImgSeq_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btnRemoveSource_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btnAddCam_->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    btnAddVideo_->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    btnAddImgSeq_->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
    btnRemoveSource_->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    topSourceBar->addWidget(btnAddVideo_);
    topSourceBar->addWidget(btnAddImgSeq_);
    topSourceBar->addWidget(btnRemoveSource_);
    topSourceBar->addStretch(1);
    v->addLayout(topSourceBar);

    QFrame* topSep = new QFrame(central);
    topSep->setFrameShape(QFrame::HLine);
    topSep->setFrameShadow(QFrame::Sunken);
    v->addWidget(topSep);

    btnPlayAll_ = new QToolButton(central);
    btnStopAll_ = new QToolButton(central);
    btnStepPrev_ = new QToolButton(central);
    btnStepNext_ = new QToolButton(central);

    btnPlayAll_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    btnPlayAll_->setCheckable(true);
    btnStopAll_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    btnStepPrev_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    btnStepNext_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));
    btnPlayAll_->setToolTip("Play/Pause");
    btnStopAll_->setToolTip("Stop");
    btnStepPrev_->setToolTip("Prev Frame");
    btnStepNext_->setToolTip("Next Frame");

    // Per-view toolbars are created in rebuildSourceViews(), one toolbar per player.

    viewsHost_ = new QWidget(this);
    viewsGrid_ = new QGridLayout(viewsHost_);
    viewsGrid_->setContentsMargins(0,0,0,0);
    viewsGrid_->setSpacing(8);
    viewsHost_->setMinimumSize(960, 540);
    v->addWidget(viewsHost_, 1);
    rebuildSourceViews();

    QHBoxLayout* playProgressRow = new QHBoxLayout();
    playProgressRow->addWidget(btnPlayAll_);
    playProgressRow->addWidget(btnStopAll_);
    playProgressRow->addWidget(btnStepPrev_);
    playProgressRow->addWidget(btnStepNext_);
    playProgressRow->addSpacing(10);
    progressSlider_ = new QSlider(Qt::Horizontal, central);
    progressSlider_->setRange(0, 0);
    progressSlider_->setSingleStep(1);
    progressSlider_->setPageStep(30);
    playProgressRow->addWidget(progressSlider_, 1);
    editCurFrame_ = new QLineEdit("0", central);
    editCurFrame_->setFixedWidth(70);
    editCurFrame_->setAlignment(Qt::AlignCenter);
    editCurFrame_->setValidator(new QIntValidator(0, 9999999, editCurFrame_));
    lblTotalFrame_ = new QLabel("/ 0", central);
    playProgressRow->addWidget(editCurFrame_);
    playProgressRow->addWidget(lblTotalFrame_);
    v->addLayout(playProgressRow);

    setCentralWidget(central);

    connect(btnAddCam_, &QToolButton::clicked, this, &MainWindow::onAddCamera);
    connect(btnAddVideo_, &QToolButton::clicked, this, &MainWindow::onAddVideo);
    connect(btnAddImgSeq_, &QToolButton::clicked, this, &MainWindow::onAddImageSequence);
    connect(btnRemoveSource_, &QToolButton::clicked, this, &MainWindow::onRemoveSource);
    connect(btnPlayAll_, &QToolButton::clicked, this, &MainWindow::onPlayAll);
    connect(btnStopAll_, &QToolButton::clicked, this, &MainWindow::onStopAll);
    connect(btnStepPrev_, &QToolButton::clicked, this, &MainWindow::onStepPrevFrame);
    connect(btnStepNext_, &QToolButton::clicked, this, &MainWindow::onStepNextFrame);
    connect(progressSlider_, &QSlider::sliderReleased, this, &MainWindow::onProgressSliderReleased);
    connect(editCurFrame_, &QLineEdit::returnPressed, this, &MainWindow::onFrameJumpReturnPressed);
    QMenu* fileMenu = menuBar()->addMenu("File");
    actSaveProject_ = fileMenu->addAction("Save Project...");
    actLoadProject_ = fileMenu->addAction("Load Project...");
    connect(actSaveProject_, &QAction::triggered, this, &MainWindow::onSaveProject);
    connect(actLoadProject_, &QAction::triggered, this, &MainWindow::onLoadProject);

    // Right dock: actions + log
    QDockWidget* dock = new QDockWidget("Actions", this);
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);

    QWidget* dockw = new QWidget(dock);
    QVBoxLayout* dv = new QVBoxLayout(dockw);

    actionTabs_ = new QTabWidget(dockw);

    // Capture tab
    QWidget* tabCap = new QWidget(actionTabs_);
    QVBoxLayout* capv = new QVBoxLayout(tabCap);
    capv->addWidget(new QLabel("Live capture source management", tabCap));
    btnCaptureNow_ = new QPushButton("Capture", tabCap);
    capv->addWidget(btnAddCam_);
    capv->addWidget(btnCaptureNow_);
    capv->addStretch(1);
    connect(btnCaptureNow_, &QPushButton::clicked, this, &MainWindow::onCaptureNow);

    // Calibration tab
    QWidget* tabCal = new QWidget(actionTabs_);
    QVBoxLayout* calv = new QVBoxLayout(tabCal);

    QGroupBox* gbMethod = new QGroupBox("Calibration Method", tabCal);
    QVBoxLayout* ml = new QVBoxLayout(gbMethod);
    cbCalibMethod_ = new QComboBox(gbMethod);
    cbCalibMethod_->addItem("Chessboard Calibration (available)");
    cbCalibMethod_->addItem("Total Station Calibration (reserved)");
    cbCalibMethod_->addItem("UAV Calibration (reserved)");
    cbCalibMethod_->setCurrentIndex(0);
    ml->addWidget(cbCalibMethod_);
    ml->addWidget(new QLabel("Reserved methods are placeholders for future implementation.", gbMethod));
    gbMethod->setLayout(ml);

    QGroupBox* gbBoard = new QGroupBox("Chessboard Parameters", tabCal);
    QGridLayout* gl = new QGridLayout(gbBoard);

    spBoardW_ = new QSpinBox(gbBoard);
    spBoardH_ = new QSpinBox(gbBoard);
    spSquare_ = new QDoubleSpinBox(gbBoard);
    spBoardW_->setRange(2, 50);
    spBoardH_->setRange(2, 50);
    spSquare_->setRange(1e-6, 10.0);
    spSquare_->setDecimals(6);
    spSquare_->setSingleStep(0.001);

    spBoardW_->setValue(board_w_);
    spBoardH_->setValue(board_h_);
    spSquare_->setValue(square_);

    gl->addWidget(new QLabel("Inner corners W:", gbBoard), 0, 0);
    gl->addWidget(spBoardW_, 0, 1);
    gl->addWidget(new QLabel("Inner corners H:", gbBoard), 1, 0);
    gl->addWidget(spBoardH_, 1, 1);
    gl->addWidget(new QLabel("Square size (m):", gbBoard), 2, 0);
    gl->addWidget(spSquare_, 2, 1);
    gbBoard->setLayout(gl);

    btnGrab_ = new QPushButton("Grab Frame (Chessboard)", tabCal);
    btnReset_ = new QPushButton("Reset Captures", tabCal);
    btnGrab_->setVisible(false);
    btnReset_->setVisible(false);
    btnComputeCalib_ = new QPushButton("Compute Calibration", tabCal);
    btnRecomputeCalib_ = new QPushButton("Recompute (Selected Frames)", tabCal);
    btnSaveCalib_ = new QPushButton("Save rig_calib.yaml", tabCal);
    btnSaveCalib_->setEnabled(false);
    calibProgressBar_ = new QProgressBar(tabCal);
    calibProgressBar_->setRange(0, 100);
    calibProgressBar_->setValue(0);
    lblCalibProgress_ = new QLabel("Progress: idle", tabCal);
    calibErrorTable_ = new QTableWidget(tabCal);
    calibErrorTable_->setColumnCount(3);
    calibErrorTable_->setHorizontalHeaderLabels(QStringList() << "Use" << "Frame" << "RMSE(px)");
    calibErrorTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    calibErrorTable_->verticalHeader()->setVisible(false);
    calibErrorTable_->setAlternatingRowColors(true);
    calibErrorTable_->setStyleSheet(
      "QTableWidget{alternate-background-color:#2b313a;background:#252b33;color:#e8edf2;gridline-color:#3a4350;}"
      "QHeaderView::section{background:#303744;color:#e8edf2;border:1px solid #3d4654;padding:4px;}"
      "QTableWidget::item{padding:4px;}");
    lblCaptured_ = new QLabel("Captured: 0", tabCal);

    calv->addWidget(gbMethod);
    calv->addWidget(gbBoard);
    calv->addWidget(btnComputeCalib_);
    calv->addWidget(btnRecomputeCalib_);
    calv->addWidget(btnSaveCalib_);
    calv->addWidget(calibProgressBar_);
    calv->addWidget(lblCalibProgress_);
    calv->addWidget(calibErrorTable_);
    calv->addWidget(lblCaptured_);
    calv->addStretch(1);

    connect(btnGrab_, &QPushButton::clicked, this, &MainWindow::onGrabFrame);
    connect(btnReset_, &QPushButton::clicked, this, &MainWindow::onResetFrames);
    connect(btnComputeCalib_, &QPushButton::clicked, this, &MainWindow::onComputeCalibration);
    connect(btnRecomputeCalib_, &QPushButton::clicked, this, &MainWindow::onRecomputeCalibrationSelected);
    connect(btnSaveCalib_, &QPushButton::clicked, this, &MainWindow::onSaveCalibrationYaml);

    // If board params change, rebuild calibrator and reset captures
    connect(spBoardW_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){ rebuildCalibratorFromUI(true); });
    connect(spBoardH_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){ rebuildCalibratorFromUI(true); });
    connect(spSquare_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double){ rebuildCalibratorFromUI(true); });

    // Tracking tab
    QWidget* tabTrk = new QWidget(actionTabs_);
    QVBoxLayout* trkv = new QVBoxLayout(tabTrk);

    btnLoadTag_ = new QPushButton("Load Tag Map (TXT)", tabTrk);
    btnLoadYaml_ = new QPushButton("Load Calibration (YAML)", tabTrk);
    chkPose_ = new QCheckBox("Pose Estimation ON", tabTrk);
    chkPose_->setChecked(false);

    QGroupBox* gbParams = new QGroupBox("Tracking Parameters", tabTrk);
    QGridLayout* tg = new QGridLayout(gbParams);
    spRansacIters_ = new QSpinBox(gbParams);
    spRansacIters_->setRange(10, 5000);
    spRansacIters_->setValue(ransac_iters_);
    spInlierThresh_ = new QDoubleSpinBox(gbParams);
    spInlierThresh_->setRange(0.1, 50.0);
    spInlierThresh_->setDecimals(2);
    spInlierThresh_->setValue(inlier_thresh_px_);
    cbTagDict_ = new QComboBox(gbParams);
    cbTagDict_->addItem("APRILTAG_36h11", (int)cv::aruco::DICT_APRILTAG_36h11);
    cbTagDict_->addItem("APRILTAG_25h9",  (int)cv::aruco::DICT_APRILTAG_25h9);
    cbTagDict_->addItem("APRILTAG_16h5",  (int)cv::aruco::DICT_APRILTAG_16h5);
    cbTagDict_->setCurrentIndex(0);

    tg->addWidget(new QLabel("RANSAC iters:"), 0, 0);
    tg->addWidget(spRansacIters_, 0, 1);
    tg->addWidget(new QLabel("Inlier thresh (px):"), 1, 0);
    tg->addWidget(spInlierThresh_, 1, 1);
    tg->addWidget(new QLabel("Tag dictionary:"), 2, 0);
    tg->addWidget(cbTagDict_, 2, 1);
    gbParams->setLayout(tg);

    lblFps_ = new QLabel("FPS: 0", tabTrk);
    btnPrintPose_ = new QPushButton("Print Pose to Log", tabTrk);
    lblInliers_ = new QLabel("Inliers: 0", tabTrk);

    trkv->addWidget(btnLoadTag_);
    lblTagPath_ = new QLabel("TagMap: (none)", tabTrk);
    trkv->addWidget(lblTagPath_);
    trkv->addWidget(btnLoadYaml_);
    lblYamlPath_ = new QLabel("Calib: (none)", tabTrk);
    trkv->addWidget(lblYamlPath_);
    trkv->addWidget(gbParams);
    trkv->addWidget(chkPose_);
    trkv->addWidget(btnPrintPose_);
    btnExportTraj_ = new QPushButton("Export Trajectory CSV", tabTrk);
    trkv->addWidget(btnExportTraj_);
    trkv->addWidget(lblInliers_);
    lblLatency_ = new QLabel("Latency: 0 ms", tabTrk);
    trkv->addWidget(lblFps_);
    trkv->addWidget(lblLatency_);
    trkv->addStretch(1);

    connect(btnLoadTag_, &QPushButton::clicked, this, &MainWindow::onLoadTagMap);
    connect(btnLoadYaml_, &QPushButton::clicked, this, &MainWindow::onLoadCalibYaml);
    connect(chkPose_, &QCheckBox::toggled, this, &MainWindow::onTogglePose);
    connect(btnPrintPose_, &QPushButton::clicked, this, &MainWindow::onPrintPose);
    connect(btnExportTraj_, &QPushButton::clicked, this, &MainWindow::onExportTrajectory);
    connect(spRansacIters_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){ ransac_iters_=v; if(solveWorker_) solveWorker_->setParams(ransac_iters_, inlier_thresh_px_, tag_dict_id_, pose_on_); });
    connect(spInlierThresh_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ inlier_thresh_px_=v; if(solveWorker_) solveWorker_->setParams(ransac_iters_, inlier_thresh_px_, tag_dict_id_, pose_on_); });
    connect(cbTagDict_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int){ tag_dict_id_=cbTagDict_->currentData().toInt(); if(solveWorker_) solveWorker_->setParams(ransac_iters_, inlier_thresh_px_, tag_dict_id_, pose_on_); });

    actionTabs_->addTab(tabCap, "Capture");
    actionTabs_->addTab(tabCal, "Calibration");
    actionTabs_->addTab(tabTrk, "Tracking");
    // Right-side parameters must follow left mode; hide tab labels completely.
    if (actionTabs_->tabBar()) actionTabs_->tabBar()->hide();

    dv->addWidget(actionTabs_);

    // Log window
    log_ = new QTextEdit(dockw);
    log_->setReadOnly(true);
    log_->setMinimumHeight(240);
    dv->addWidget(log_);

    dockw->setLayout(dv);
    dock->setWidget(dockw);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    lblResolution_ = new QLabel("Resolution: -", this);
    statusBar()->addPermanentWidget(lblResolution_);

    onModeCapture();
    statusBar()->showMessage("Ready");
    refreshSourceList();
    // Per-source docks are OFF by default to avoid duplicate display.
    logLine("App started. Configure sources and chessboard in the UI.");
}

void MainWindow::logLine(const QString& s) {
    log_->append(QString("[%1] %2").arg(nowStr(), s));
}

bool MainWindow::openAllSources() {
  closeAllSources();
  bool okAll = true;
  for (auto& s : sources_) {
    if (s.is_image_seq) {
      if (s.seq_files.isEmpty()) okAll = false;
      continue;
    }
    if (s.is_cam) s.cap.open(s.cam_id);
    else s.cap.open(s.video_path.toStdString());
    if (!s.cap.isOpened()) okAll = false;
  }
  return okAll;
}

void MainWindow::closeAllSources() {
  for (auto& s : sources_) {
    if (s.cap.isOpened()) s.cap.release();
  }
}

void MainWindow::refreshSourceList() {
  QStringList status;
  QMutexLocker locker(&sources_mutex_);
  for (int i=0;i<(int)sources_.size();++i) {
    const auto& s = sources_[i];
    QString label;
    if (s.is_cam) label = QString("Camera %1").arg(s.cam_id);
    else if (s.is_image_seq) label = QString("ImgSeq:%1").arg(QFileInfo(s.seq_dir).fileName());
    else label = QFileInfo(s.video_path).fileName();
    bool en = (i < (int)source_enabled_.size()) ? source_enabled_[i] : true;
    QString owner = "{Capture}";
    if (s.mode_owner == (int)CALIB) owner = "{Calib}";
    else if (s.mode_owner == (int)TRACK) owner = "{Track}";
    label += owner;
    label += en ? "[RUN]" : "[PAUSED]";
    status << label;
  }
  if (!status.isEmpty()) statusBar()->showMessage(QString("Sources: %1").arg(status.join(" | ")));
}

std::vector<int> MainWindow::activeSourceIndices() const {
  std::vector<int> idx;
  for (int i=0;i<(int)sources_.size();++i) {
    if (sources_[i].mode_owner == (int)mode_) idx.push_back(i);
  }
  return idx;
}

void MainWindow::rebuildSourceViews() {
  if (!viewsGrid_) return;

  while (QLayoutItem* item = viewsGrid_->takeAt(0)) {
    if (item->widget()) item->widget()->deleteLater();
    delete item;
  }
  sourceViews_.clear();

  active_view_source_indices_ = activeSourceIndices();
  const int n = std::min(2, (int)active_view_source_indices_.size());
  if (n <= 0) {
    QLabel* hint = new QLabel("No sources. Click AddVideo to create a viewer.", viewsHost_);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet("background-color:#111; border:1px solid #333; color:#ddd;");
    hint->setMinimumSize(960, 540);
    viewsGrid_->addWidget(hint, 0, 0);
    return;
  }

  int cols = (n <= 1) ? 1 : 2;
  auto themedIcon = [this](const QString& name, QStyle::StandardPixmap sp) {
    QIcon icon = QIcon::fromTheme(name);
    if (icon.isNull()) icon = style()->standardIcon(sp);
    return icon;
  };

  for (int i=0; i<n; ++i) {
    QWidget* panel = new QWidget(viewsHost_);
    panel->setStyleSheet("background:#1f2328; border:1px solid #3f4650;");
    QVBoxLayout* pv = new QVBoxLayout(panel);
    pv->setContentsMargins(4,4,4,4);
    pv->setSpacing(4);

    QToolButton* panBtn = new QToolButton(panel);
    QToolButton* pointBtn = new QToolButton(panel);
    QToolButton* lineBtn = new QToolButton(panel);
    QToolButton* zoomInBtn = new QToolButton(panel);
    QToolButton* zoomOutBtn = new QToolButton(panel);
    QToolButton* resetBtn = new QToolButton(panel);
    QToolButton* clearBtn = new QToolButton(panel);

    panBtn->setIcon(themedIcon("transform-move", QStyle::SP_ArrowUp));
    pointBtn->setIcon(themedIcon("draw-freehand", QStyle::SP_DialogYesButton));
    lineBtn->setIcon(themedIcon("draw-line", QStyle::SP_LineEditClearButton));
    zoomInBtn->setIcon(themedIcon("zoom-in", QStyle::SP_FileDialogDetailedView));
    zoomOutBtn->setIcon(themedIcon("zoom-out", QStyle::SP_FileDialogListView));
    resetBtn->setIcon(themedIcon("zoom-fit-best", QStyle::SP_BrowserReload));
    clearBtn->setIcon(themedIcon("edit-clear", QStyle::SP_TrashIcon));

    panBtn->setCheckable(true);
    pointBtn->setCheckable(true);
    lineBtn->setCheckable(true);
    panBtn->setChecked(true);

    panBtn->setText("Pan");
    pointBtn->setText("Point");
    lineBtn->setText("Line");
    zoomInBtn->setText("Zoom+");
    zoomOutBtn->setText("Zoom-");
    resetBtn->setText("Reset");
    clearBtn->setText("Clear");

    panBtn->setToolTip("Pan view");
    pointBtn->setToolTip("Draw point");
    lineBtn->setToolTip("Draw line");
    zoomInBtn->setToolTip("Zoom in");
    zoomOutBtn->setToolTip("Zoom out");
    resetBtn->setToolTip("Reset view");
    clearBtn->setToolTip("Clear drawings");

    QWidget* canvas = new QWidget(panel);
    QGridLayout* overlay = new QGridLayout(canvas);
    overlay->setContentsMargins(0,0,0,0);
    overlay->setSpacing(0);

    QWidget* topBar = new QWidget(canvas);
    topBar->setAttribute(Qt::WA_StyledBackground, false);
    QHBoxLayout* tb = new QHBoxLayout(topBar);
    tb->setContentsMargins(6,0,6,6);
    tb->setSpacing(2);
    for (QToolButton* b : {panBtn, pointBtn, lineBtn, zoomInBtn, zoomOutBtn, resetBtn, clearBtn}) {
      b->setAutoRaise(true);
      b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
      tb->addWidget(b);
    }
    tb->addStretch(1);

    ImageViewer* v = new ImageViewer(canvas);
    v->setMinimumSize(480, 270);
    v->setToolMode(ImageViewer::PanTool);
    connect(panBtn, &QToolButton::clicked, this, [v, panBtn, pointBtn, lineBtn]() {
      panBtn->setChecked(true); pointBtn->setChecked(false); lineBtn->setChecked(false);
      v->setToolMode(ImageViewer::PanTool);
    });
    connect(pointBtn, &QToolButton::clicked, this, [v, panBtn, pointBtn, lineBtn]() {
      panBtn->setChecked(false); pointBtn->setChecked(true); lineBtn->setChecked(false);
      v->setToolMode(ImageViewer::PointTool);
    });
    connect(lineBtn, &QToolButton::clicked, this, [v, panBtn, pointBtn, lineBtn]() {
      panBtn->setChecked(false); pointBtn->setChecked(false); lineBtn->setChecked(true);
      v->setToolMode(ImageViewer::LineTool);
    });
    connect(zoomInBtn, &QToolButton::clicked, this, [v, panBtn, pointBtn, lineBtn]() {
      panBtn->setChecked(true); pointBtn->setChecked(false); lineBtn->setChecked(false);
      v->setToolMode(ImageViewer::PanTool);
      v->zoomIn();
    });
    connect(zoomOutBtn, &QToolButton::clicked, this, [v, panBtn, pointBtn, lineBtn]() {
      panBtn->setChecked(true); pointBtn->setChecked(false); lineBtn->setChecked(false);
      v->setToolMode(ImageViewer::PanTool);
      v->zoomOut();
    });
    connect(resetBtn, &QToolButton::clicked, this, [v, panBtn, pointBtn, lineBtn]() {
      panBtn->setChecked(true); pointBtn->setChecked(false); lineBtn->setChecked(false);
      v->setToolMode(ImageViewer::PanTool);
      v->resetView();
    });
    connect(clearBtn, &QToolButton::clicked, this, [v, panBtn, pointBtn, lineBtn]() {
      panBtn->setChecked(true); pointBtn->setChecked(false); lineBtn->setChecked(false);
      v->setToolMode(ImageViewer::PanTool);
      v->clearAnnotations();
    });

    overlay->addWidget(v, 0, 0);
    overlay->addWidget(topBar, 0, 0, Qt::AlignBottom | Qt::AlignLeft);
    pv->addWidget(canvas, 1);

    sourceViews_.push_back(v);
    int r = i / cols;
    int c = i % cols;
    viewsGrid_->addWidget(panel, r, c);
  }
}

void MainWindow::updateSourceViews(const std::vector<cv::Mat>& frames) {
  int n = std::min((int)sourceViews_.size(), std::min(2, (int)active_view_source_indices_.size()));
  QStringList resText;
  for (int i=0; i<n; ++i) {
    int srcIdx = active_view_source_indices_[i];
    if (!sourceViews_[i] || srcIdx < 0 || srcIdx >= (int)frames.size()) continue;
    if (frames[srcIdx].empty()) {
      sourceViews_[i]->setImage(QImage());
      continue;
    }
    resText << QString("S%1: %2x%3").arg(i+1).arg(frames[srcIdx].cols).arg(frames[srcIdx].rows);
    sourceViews_[i]->setImage(matToQImage(frames[srcIdx]));
  }
  for (int i=n; i<(int)sourceViews_.size(); ++i) {
    if (sourceViews_[i]) sourceViews_[i]->setImage(QImage());
  }
  if (lblResolution_) {
    lblResolution_->setText(resText.isEmpty() ? "Resolution: -" : QString("Resolution: %1").arg(resText.join(" | ")));
  }
}

void MainWindow::rebuildCalibratorFromUI(bool reset) {
  board_w_ = spBoardW_ ? spBoardW_->value() : board_w_;
  board_h_ = spBoardH_ ? spBoardH_->value() : board_h_;
  square_  = spSquare_ ? spSquare_->value() : square_;

  if (reset) {
    calibrator_.reset(new MultiCamCalibrator(std::max(1, (int)sources_.size()),
                                             cv::Size(board_w_, board_h_), square_));
    calib_pairs_.clear();
    calib_pair_rmse_.clear();
    has_computed_calib_ = false;
    if (btnSaveCalib_) btnSaveCalib_->setEnabled(false);
    if (calibErrorTable_) calibErrorTable_->setRowCount(0);
    if (calibProgressBar_) calibProgressBar_->setValue(0);
    if (lblCalibProgress_) lblCalibProgress_->setText("Progress: idle");
    logLine(QString("Chessboard params updated: %1x%2 square=%3 m (captures reset)")
            .arg(board_w_).arg(board_h_).arg(square_,0,'f',6));
    last_inliers_ = 0;
  }
}

// ---------------- Sources actions ----------------
void MainWindow::onAddCamera() {
  bool ok=false;
  int camId = QInputDialog::getInt(this, "Add Camera", "Camera index:", 0, 0, 64, 1, &ok);
  if (!ok) return;
  if (mode_ != CAPTURE) {
    QMessageBox::information(this, "Add Camera", "Please switch to Capture tab to add camera sources.");
    return;
  }

  InputSource s;
  s.is_cam = true;
  s.cam_id = camId;
  s.mode_owner = (int)CAPTURE;

  // Try open immediately
  s.cap.open(camId);
  if (!s.cap.isOpened()) {
    QMessageBox::warning(this, "Camera", "Failed to open camera. Try another index or close other apps.");
    logLine(QString("Failed to open camera %1").arg(camId));
  } else {
    logLine(QString("Added camera %1").arg(camId));
  }

  timer_.stop();
  if (captureWorker_) QMetaObject::invokeMethod(captureWorker_, "stop", Qt::BlockingQueuedConnection);
  sources_.push_back(std::move(s));
  num_cams_ = (int)sources_.size();
  // Do NOT auto-play on import
  if ((int)source_enabled_.size() < num_cams_) source_enabled_.resize(num_cams_, true);
  source_enabled_[num_cams_-1] = true;
  if ((int)last_frames_.size() < num_cams_) last_frames_.resize(num_cams_);
  cv::Mat firstFrame;
  if (!sources_.empty() && sources_.back().cap.isOpened()) {
    sources_.back().cap.read(firstFrame);
  }
  last_frames_[num_cams_ - 1] = firstFrame.clone();
  source_enabled_.assign(std::max(0,num_cams_), true);
  // last_frames_ will be populated by CaptureWorker when frames arrive.
  last_frames_.resize(std::max(0,num_cams_));
  // Rebuild calibrator with new cam count; reset captures
  calibrator_.reset(new MultiCamCalibrator(std::max(1,num_cams_), cv::Size(board_w_, board_h_), square_));
  refreshSourceList();
  if (show_docks_) rebuildSourceDocks();
  rebuildSourceViews();
  updateSourceViews(last_frames_);
}

void MainWindow::onAddVideo() 
{
    QString last = settings_.value("lastVideoDir", "").toString();
    QString path = QFileDialog::getOpenFileName(this, "Add Video", last,"Video (*.mp4 *.avi *.mov *.mkv);;All (*.*)");
    if (path.isEmpty()) return;

    if ((int)activeSourceIndices().size() >= 2) {
      QMessageBox::information(this, "Add Video", "Current tab already has 2 sources.");
      return;
    }

    InputSource s;
    s.is_cam = false;
    s.mode_owner = (int)mode_;
    s.video_path = path;
    std::string p = QFile::encodeName(path).constData();
    s.cap.open(p);
    if (!s.cap.isOpened()) 
    {
        QMessageBox::warning(this, "Video", "Failed to open video file.");
        logLine(QString("Failed to open video: %1").arg(path));
        return;
    }
    settings_.setValue("lastVideoDir", QFileInfo(path).absolutePath());

    timer_.stop();
    if (captureWorker_) 
        QMetaObject::invokeMethod(captureWorker_, "stop", Qt::BlockingQueuedConnection);
    sources_.push_back(std::move(s));
    num_cams_ = (int)sources_.size();
    // Do NOT auto-play on import
    if ((int)source_enabled_.size() < num_cams_) 
        source_enabled_.resize(num_cams_, true);
    source_enabled_[num_cams_-1] = false;
    if ((int)last_frames_.size() < num_cams_) 
        last_frames_.resize(num_cams_);
 
    cv::Mat firstFrame;
    if (!sources_.empty() && sources_.back().cap.isOpened()) {
      sources_.back().cap.set(cv::CAP_PROP_POS_FRAMES, 0);
      sources_.back().cap.read(firstFrame);
      sources_.back().cap.set(cv::CAP_PROP_POS_FRAMES, 0);
    }
    last_frames_[num_cams_ - 1] = firstFrame.clone();

    source_enabled_.assign(std::max(0,num_cams_), true);
    calibrator_.reset(new MultiCamCalibrator(std::max(1,num_cams_), cv::Size(board_w_, board_h_), square_));
    refreshSourceList();
    if (show_docks_) rebuildSourceDocks();
    rebuildSourceViews();
    updateSourceViews(last_frames_);
    logLine(QString("Added video: %1").arg(path));
}


void MainWindow::onAddImageSequence()
{
    QString last = settings_.value("lastImageSeqDir", "").toString();
    QString dir = QFileDialog::getExistingDirectory(this, "Add Image Sequence Folder", last);
    if (dir.isEmpty()) return;

    QDir qdir(dir);
    QFileInfoList files = qdir.entryInfoList(kImageNameFilters(), QDir::Files, QDir::Name);
    if (files.isEmpty()) {
      QMessageBox::warning(this, "Image Sequence", "No image files found in selected folder.");
      return;
    }

    if ((int)activeSourceIndices().size() >= 2) {
      QMessageBox::information(this, "Add Image Sequence", "Current tab already has 2 sources.");
      return;
    }

    InputSource s;
    s.is_cam = false;
    s.is_image_seq = true;
    s.mode_owner = (int)mode_;
    s.seq_dir = dir;
    s.video_path = dir;
    s.seq_idx = 0;
    for (const auto& fi : files) s.seq_files.push_back(fi.absoluteFilePath());

    cv::Mat firstFrame = cv::imread(s.seq_files.front().toStdString(), cv::IMREAD_COLOR);
    if (firstFrame.empty()) {
      QMessageBox::warning(this, "Image Sequence", "Failed to read first image in selected folder.");
      return;
    }

    settings_.setValue("lastImageSeqDir", dir);

    timer_.stop();
    if (captureWorker_) QMetaObject::invokeMethod(captureWorker_, "stop", Qt::BlockingQueuedConnection);

    sources_.push_back(std::move(s));
    num_cams_ = (int)sources_.size();
    if ((int)source_enabled_.size() < num_cams_) source_enabled_.resize(num_cams_, true);
    source_enabled_[num_cams_ - 1] = true;
    if ((int)last_frames_.size() < num_cams_) last_frames_.resize(num_cams_);
    last_frames_[num_cams_ - 1] = firstFrame.clone();

    source_enabled_.assign(std::max(0, num_cams_), true);
    calibrator_.reset(new MultiCamCalibrator(std::max(1, num_cams_), cv::Size(board_w_, board_h_), square_));
    refreshSourceList();
    if (show_docks_) rebuildSourceDocks();
    rebuildSourceViews();
    updateSourceViews(last_frames_);
    logLine(QString("Added image sequence: %1 (%2 frames)").arg(dir).arg(files.size()));
}

void MainWindow::onRemoveSource() {
  int row = -1;
  for (int i=(int)sources_.size()-1; i>=0; --i) {
    if (sources_[i].mode_owner == (int)mode_) { row = i; break; }
  }
  if (row < 0 || row >= (int)sources_.size()) return;

  timer_.stop();
  if (sources_[row].cap.isOpened()) sources_[row].cap.release();
  sources_.erase(sources_.begin() + row);
  num_cams_ = (int)sources_.size();
  source_enabled_.assign(std::max(0,num_cams_), true);
  // last_frames_ will be populated by CaptureWorker when frames arrive.
  last_frames_.resize(std::max(0,num_cams_));

  calibrator_.reset(new MultiCamCalibrator(std::max(1,num_cams_), cv::Size(board_w_, board_h_), square_));
  refreshSourceList();
  if (show_docks_) rebuildSourceDocks();
  rebuildSourceViews();
  logLine("Removed source.");
}

void MainWindow::onModeCalibration() {
  mode_ = CALIB;
  if (btnAddCam_) btnAddCam_->setVisible(false);
  if (btnAddVideo_) btnAddVideo_->setVisible(true);
  if (btnAddImgSeq_) btnAddImgSeq_->setVisible(true);
  if (modeTabs_ && modeTabs_->currentIndex()!=1) modeTabs_->setCurrentIndex(1);
  if (actionTabs_) actionTabs_->setCurrentIndex(1);
  rebuildSourceViews();
  updateSourceViews(last_frames_);
  logLine("Switched to Calibration mode.");
}

void MainWindow::onModeTracking() {
  mode_ = TRACK;
  if (btnAddCam_) btnAddCam_->setVisible(false);
  if (btnAddVideo_) btnAddVideo_->setVisible(true);
  if (btnAddImgSeq_) btnAddImgSeq_->setVisible(true);
  if (modeTabs_ && modeTabs_->currentIndex()!=2) modeTabs_->setCurrentIndex(2);
  if (actionTabs_) actionTabs_->setCurrentIndex(2);
  rebuildSourceViews();
  updateSourceViews(last_frames_);
  logLine("Switched to Tracking mode.");
}

void MainWindow::onModeCapture() {
  mode_ = CAPTURE;
  if (btnAddCam_) btnAddCam_->setVisible(true);
  if (btnAddVideo_) btnAddVideo_->setVisible(false);
  if (btnAddImgSeq_) btnAddImgSeq_->setVisible(false);
  if (modeTabs_ && modeTabs_->currentIndex()!=0) modeTabs_->setCurrentIndex(0);
  if (actionTabs_) actionTabs_->setCurrentIndex(0);
  rebuildSourceViews();
  updateSourceViews(last_frames_);
  logLine("Switched to Capture mode.");
}

void MainWindow::onCaptureNow() {
  stopCaptureBlocking();
  {
    QMutexLocker srcLock(&sources_mutex_);
    if ((int)last_frames_.size() < (int)sources_.size()) last_frames_.resize(sources_.size());
    for (int i=0;i<(int)sources_.size();++i) {
      auto& s = sources_[i];
      if (s.mode_owner != (int)CAPTURE) continue;
      cv::Mat f;
      if (s.is_image_seq) {
        if (!s.seq_files.isEmpty()) {
          int idx = std::max(0, std::min(s.seq_idx, s.seq_files.size()-1));
          f = cv::imread(s.seq_files[idx].toStdString(), cv::IMREAD_COLOR);
        }
      } else if (s.cap.isOpened()) {
        s.cap.read(f);
      }
      if (!f.empty()) last_frames_[i] = f;
    }
  }
  updateSourceViews(last_frames_);
  logLine("Capture snapshot updated.");
}

void MainWindow::onModeTabChanged(int idx) {
  if (idx == 0) onModeCapture();
  else if (idx == 1) onModeCalibration();
  else onModeTracking();
}

bool MainWindow::readFrames(std::vector<cv::Mat>& frames) {
  frames.clear();
  QMutexLocker locker(&frames_mutex_);
  if (last_frames_.empty()) return false;
  for (const auto& f : last_frames_) {
    if (f.empty()) return false;
    frames.push_back(f);
  }
  return !frames.empty();
}

QImage MainWindow::matToQImage(const cv::Mat& bgr) {
  cv::Mat rgb;
  if (bgr.channels()==3) cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  else if (bgr.channels()==4) cv::cvtColor(bgr, rgb, cv::COLOR_BGRA2RGBA);
  else cv::cvtColor(bgr, rgb, cv::COLOR_GRAY2RGB);

  return QImage((const uchar*)rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888).copy();
}

cv::Mat MainWindow::makeMosaic(const std::vector<cv::Mat>& imgs, int cols) {
  if (imgs.empty()) return cv::Mat();
  cols = std::max(1, cols);
  int rows = (int)std::ceil((double)imgs.size() / (double)cols);

  // Find first non-empty to determine cell size
  int cell_w = 0, cell_h = 0;
  for (const auto& im : imgs) {
    if (!im.empty()) { cell_w = im.cols; cell_h = im.rows; break; }
  }
  if (cell_w <= 0 || cell_h <= 0) return cv::Mat();

  cv::Mat mosaic(cell_h * rows, cell_w * cols, CV_8UC3, cv::Scalar(16,16,16));

  for (int i = 0; i < (int)imgs.size(); ++i) {
    int r = i / cols;
    int c = i % cols;
    cv::Rect roi(c * cell_w, r * cell_h, cell_w, cell_h);

    cv::Mat src = imgs[i];
    cv::Mat rgb;
    if (src.empty()) {
      rgb = cv::Mat(cell_h, cell_w, CV_8UC3, cv::Scalar(16,16,16));
    } else {
      if (src.channels() == 1) cv::cvtColor(src, rgb, cv::COLOR_GRAY2BGR);
      else if (src.channels() == 3) rgb = src;
      else cv::cvtColor(src, rgb, cv::COLOR_BGRA2BGR);

      if (rgb.cols != cell_w || rgb.rows != cell_h) {
        cv::resize(rgb, rgb, cv::Size(cell_w, cell_h), 0, 0, cv::INTER_AREA);
      }
    }

    rgb.copyTo(mosaic(roi));
  }
  return mosaic;
}

void MainWindow::overlayCalibration(std::vector<cv::Mat>& vis, const std::vector<cv::Mat>& frames) {
  std::vector<std::vector<cv::Point2f>> corners;
  std::vector<bool> ok;
  calibrator_->detectAndMaybeStore(frames, false, &corners, &ok);

  for (int i=0;i<num_cams_;++i) {
    if (ok[i]) {
      cv::drawChessboardCorners(vis[i], cv::Size(board_w_, board_h_), corners[i], true);
    }
    cv::putText(vis[i], "cam"+std::to_string(i), cv::Point(15,30),
                cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0,255,0), 2, cv::LINE_AA);
  }
}

void MainWindow::overlayTracking(std::vector<cv::Mat>& vis, const std::vector<cv::Mat>& frames) {
  AprilTagDetections det;
  std::vector<Observation> obs;

  if (tagmap_loaded_) {
    buildObservationsFromFrames(frames, tag_corner_map_, obs, &det, tag_dict_id_);
    for (int i=0;i<num_cams_;++i) {
      if (i < (int)det.ids_per_cam.size())
        cv::aruco::drawDetectedMarkers(vis[i], det.corners_per_cam[i], det.ids_per_cam[i]);
      cv::putText(vis[i], "cam"+std::to_string(i), cv::Point(15,30),
                  cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0,255,0), 2, cv::LINE_AA);
    }

  } else {
    for (int i=0;i<num_cams_;++i) {
      cv::putText(vis[i], "Load tag map to start tracking", cv::Point(15,60),
                  cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0,255,255), 2, cv::LINE_AA);
    }
  }
}

void MainWindow::updateStatus() {
  lblCaptured_->setText(QString("Captured: %1").arg(calibrator_->captured()));
  lblInliers_->setText(QString("Inliers: %1").arg(last_inliers_));

  QString m = (mode_==CALIB) ? "Calibration" : "Tracking";
  statusBar()->showMessage(QString("Mode: %1 | Sources: %2 | Captured: %3 | TagMap: %4 | Calib: %5 | Pose: %6 | Inliers: %7 | FPS: %8")
    .arg(m)
    .arg((int)sources_.size())
    .arg(calibrator_->captured())
    .arg(tagmap_loaded_ ? "YES" : "NO")
    .arg(calib_loaded_ ? "YES" : "NO")
    .arg(pose_on_ ? "ON" : "OFF")
    .arg(last_inliers_)
    .arg(fps_, 0, 'f', 1));
}

void MainWindow::onTick() {
  // UI refresh at a steady rate; frames arrive from CaptureWorker
  if (sources_.empty()) {
    updateSourceViews(std::vector<cv::Mat>());
    updateStatus();
    return;
  }
  std::vector<cv::Mat> frames;
  {
    QMutexLocker locker(&frames_mutex_);
    if (last_frames_.empty()) return;
    frames = last_frames_;
  }

  // Note: some sources may not have frames yet; mosaic will show placeholders.

  std::vector<cv::Mat> vis = frames;
  // If sources count changed, rebuild calibrator to match to avoid crash
  if (mode_==CALIB) {
    int n = (int)frames.size();
    if (n <= 0) { updateStatus(); return; }
    if (num_cams_ != n) {
      num_cams_ = n;
      calibrator_.reset(new MultiCamCalibrator(num_cams_, cv::Size(board_w_, board_h_), square_));
      //logLine(QString(\"Sources changed -> rebuild calibrator (num=%1)\").arg(num_cams_));
    }
  }
    // Heavy overlay (chessboard / tag detection) throttled to keep UI responsive
  // Heavy overlay (AprilTag/chessboard detection) can block UI and cause stutter.
  // NOTE: playback_running_ is only true for the sync Play button path, but sources may
  // still be actively reading via Pause/Resume flow. Detect active sources directly.
  //bool anySourceRunning = false;
  //{
  //  QMutexLocker lock(&sources_mutex_);
  //  for (bool en : source_enabled_) {
  //    if (en) { anySourceRunning = true; break; }
  //  }
  //}
  //const int overlayDiv = anySourceRunning ? std::max(ui_overlay_div_, 12) : ui_overlay_div_;
  //ui_frame_skip_ = (ui_frame_skip_ + 1) % overlayDiv;
  //if (ui_frame_skip_ == 0) {
  //  if (mode_==CALIB) overlayCalibration(vis, frames);
  //  else overlayTracking(vis, frames);
  //}

  updateSourceViews(vis);

  if (show_docks_) updateSourceDocks(frames);
  updateStatus();
}

void MainWindow::updateFpsStats(double dt_ms) {
  if (dt_ms <= 0.0) return;
  double inst = 1000.0 / dt_ms;
  // simple low-pass filter
  fps_ = (fps_<=0.0) ? inst : (0.9*fps_ + 0.1*inst);
  if (lblFps_) lblFps_->setText(QString("FPS: %1").arg(fps_, 0, 'f', 1));
}

void MainWindow::setSourceEnabled(int idx, bool enabled) {
  {
    QMutexLocker locker(&sources_mutex_);
    if (idx < 0 || idx >= (int)source_enabled_.size()) return;
    source_enabled_[idx] = enabled;
  }
  refreshSourceList();
}

QJsonObject MainWindow::toProjectJson() const {
  QJsonObject o;
  o["board_w"] = board_w_;
  o["board_h"] = board_h_;
  o["square"] = square_;
  o["ransac_iters"] = ransac_iters_;
  o["inlier_thresh_px"] = inlier_thresh_px_;
  o["tag_dict_id"] = tag_dict_id_;

  QJsonArray srcs;
  for (int i=0;i<(int)sources_.size();++i) {
    const auto& s = sources_[i];
    QJsonObject si;
    si["enabled"] = (i < (int)source_enabled_.size()) ? source_enabled_[i] : true;
    si["mode_owner"] = s.mode_owner;
    if (s.is_cam) {
      si["type"] = "cam";
      si["cam_id"] = s.cam_id;
    } else if (s.is_image_seq) {
      si["type"] = "imgseq";
      si["dir"] = s.seq_dir;
    } else {
      si["type"] = "video";
      si["path"] = s.video_path;
    }
    srcs.append(si);
  }
  o["sources"] = srcs;

  o["tagmap_path"] = tagmap_path_;
  o["calib_path"] = calib_path_;
  o["layout_geometry_b64"] = QString(saveGeometry().toBase64());
  o["layout_state_b64"] = QString(saveState().toBase64());
  return o;
}

bool MainWindow::fromProjectJson(const QJsonObject& o) {
  if (o.contains("board_w")) spBoardW_->setValue(o["board_w"].toInt(board_w_));
  if (o.contains("board_h")) spBoardH_->setValue(o["board_h"].toInt(board_h_));
  if (o.contains("square")) spSquare_->setValue(o["square"].toDouble(square_));
  if (o.contains("ransac_iters")) spRansacIters_->setValue(o["ransac_iters"].toInt(ransac_iters_));
  if (o.contains("inlier_thresh_px")) spInlierThresh_->setValue(o["inlier_thresh_px"].toDouble(inlier_thresh_px_));
  if (o.contains("tag_dict_id")) {
    int did = o["tag_dict_id"].toInt(tag_dict_id_);
    // set combo if present
    for (int i=0;i<cbTagDict_->count();++i) {
      if (cbTagDict_->itemData(i).toInt() == did) { cbTagDict_->setCurrentIndex(i); break; }
    }
  }

  // Rebuild sources
  timer_.stop();
  closeAllSources();
  sources_.clear();
  source_enabled_.clear();
  last_frames_.clear();

  if (o.contains("sources") && o["sources"].isArray()) {
    QJsonArray srcs = o["sources"].toArray();
    for (auto v : srcs) {
      if (!v.isObject()) continue;
      QJsonObject si = v.toObject();
      QString type = si["type"].toString();
      bool en = si["enabled"].toBool(true);
      int owner = si["mode_owner"].toInt(0);

      InputSource s;
      if (type == "cam") {
        s.is_cam = true;
        s.mode_owner = owner;
        s.cam_id = si["cam_id"].toInt(0);
        s.cap.open(s.cam_id);
      } else if (type == "video") {
        s.is_cam = false;
        s.mode_owner = owner;
        s.is_image_seq = false;
        s.video_path = si["path"].toString();
        s.cap.open(s.video_path.toStdString());
      } else if (type == "imgseq") {
        s.is_cam = false;
        s.mode_owner = owner;
        s.is_image_seq = true;
        s.seq_dir = si["dir"].toString();
        s.video_path = s.seq_dir;
        QDir qdir(s.seq_dir);
        QFileInfoList files = qdir.entryInfoList(kImageNameFilters(), QDir::Files, QDir::Name);
        for (const auto& fi : files) s.seq_files.push_back(fi.absoluteFilePath());
        s.seq_idx = 0;
      } else continue;

      sources_.push_back(std::move(s));
      source_enabled_.push_back(en);
      last_frames_.emplace_back();
    }
  }

  num_cams_ = (int)sources_.size();
  source_enabled_.assign(std::max(0,num_cams_), true);
  // last_frames_ will be populated by CaptureWorker when frames arrive.
  last_frames_.resize(std::max(0,num_cams_));
  calibrator_.reset(new MultiCamCalibrator(std::max(1,num_cams_), cv::Size(board_w_, board_h_), square_));
  refreshSourceList();
  rebuildSourceViews();

  // Bind file paths
  if (o.contains("tagmap_path")) {
    tagmap_path_ = o["tagmap_path"].toString();
    if (!tagmap_path_.isEmpty()) {
      if (QFile::exists(tagmap_path_)) {
        tagmap_loaded_ = loadTagCornersTxt(tagmap_path_.toStdString(), tag_corner_map_);
      } else {
        QMessageBox::warning(this, "Project", "Tag map file missing. Please locate it.");
        QString p = QFileDialog::getOpenFileName(this, "Locate Tag Map TXT", "", "Text (*.txt);;All (*.*)");
        if (!p.isEmpty()) { tagmap_path_=p; tagmap_loaded_=loadTagCornersTxt(p.toStdString(), tag_corner_map_); }
      }
      if (lblTagPath_) lblTagPath_->setText(QString("TagMap: %1").arg(tagmap_path_));
    }
  }
  if (o.contains("calib_path")) {
    calib_path_ = o["calib_path"].toString();
    if (!calib_path_.isEmpty()) {
      if (QFile::exists(calib_path_)) {
        calib_loaded_ = loadRigCalibYaml(calib_path_.toStdString(), cams_);
      } else {
        QMessageBox::warning(this, "Project", "Calibration YAML missing. Please locate it.");
        QString p = QFileDialog::getOpenFileName(this, "Locate Calibration YAML", "", "YAML (*.yaml *.yml);;All (*.*)");
        if (!p.isEmpty()) { calib_path_=p; calib_loaded_=loadRigCalibYaml(p.toStdString(), cams_); }
      }
      if (lblYamlPath_) lblYamlPath_->setText(QString("Calib: %1").arg(calib_path_));
    }
  }
  if (solveWorker_) solveWorker_->setStaticData(&cams_, &tag_corner_map_);

  // Restore layout if present
  if (o.contains("layout_geometry_b64")) {
    QByteArray g = QByteArray::fromBase64(o["layout_geometry_b64"].toString().toUtf8());
    if (!g.isEmpty()) restoreGeometry(g);
  }
  if (o.contains("layout_state_b64")) {
    QByteArray s = QByteArray::fromBase64(o["layout_state_b64"].toString().toUtf8());
    if (!s.isEmpty()) restoreState(s);
  }

  return true;
}

// ----------------- Calibration actions -----------------
void MainWindow::onGrabFrame() {
  std::vector<cv::Mat> frames;
  if (!readFrames(frames)) return;

  bool ok = calibrator_->detectAndMaybeStore(frames, true);
  logLine(ok ? "Grabbed chessboard frame." : "No chessboard detected.");
  updateStatus();
}

void MainWindow::onResetFrames() {
  calibrator_->reset();
  calib_pairs_.clear();
  calib_pair_rmse_.clear();
  has_computed_calib_ = false;
  if (btnSaveCalib_) btnSaveCalib_->setEnabled(false);
  if (calibErrorTable_) calibErrorTable_->setRowCount(0);
  if (calibProgressBar_) calibProgressBar_->setValue(0);
  if (lblCalibProgress_) lblCalibProgress_->setText("Progress: idle");
  logLine("Reset captured frames.");
  updateStatus();
}

void MainWindow::onComputeCalibration() {
  if (cbCalibMethod_ && cbCalibMethod_->currentIndex() != 0) {
    QMessageBox::information(this, "Calibration", "This calibration method is reserved and not implemented yet.");
    return;
  }
  // Use all frames from the two Calibration-tab sources (no Grab required).
  std::vector<int> idx;
  {
    QMutexLocker lock(&sources_mutex_);
    for (int i=0;i<(int)sources_.size();++i) {
      if (sources_[i].mode_owner == (int)CALIB && !sources_[i].is_cam) idx.push_back(i);
    }
  }
  if (idx.size() != 2) {
    QMessageBox::warning(this, "Calibration", "Calibration tab must have exactly 2 video/image-sequence sources.");
    return;
  }

  has_computed_calib_ = false;
  if (btnSaveCalib_) btnSaveCalib_->setEnabled(false);
  calib_pairs_.clear();
  calib_pair_rmse_.clear();
  if (calibErrorTable_) calibErrorTable_->setRowCount(0);
  if (calibProgressBar_) {
    calibProgressBar_->setRange(0, 100);
    calibProgressBar_->setValue(0);
  }
  if (lblCalibProgress_) lblCalibProgress_->setText("Progress: preparing...");

  stopCaptureBlocking();

  int totalFrames = 0;
  {
    QMutexLocker lock(&sources_mutex_);
    for (int k : idx) {
      if (k < 0 || k >= (int)sources_.size()) continue;
      if (sources_[k].is_image_seq) sources_[k].seq_idx = 0;
      else if (sources_[k].cap.isOpened()) {
        sources_[k].cap.set(cv::CAP_PROP_POS_FRAMES, 0);
      }
    }
    auto frameCount = [&](const InputSource& s)->int {
      if (s.is_image_seq) return s.seq_files.size();
      if (!s.cap.isOpened()) return 0;
      return std::max(0, (int)s.cap.get(cv::CAP_PROP_FRAME_COUNT));
    };
    int c0 = frameCount(sources_[idx[0]]);
    int c1 = frameCount(sources_[idx[1]]);
    totalFrames = (c0 > 0 && c1 > 0) ? std::min(c0, c1) : 0;
  }
  if (calibProgressBar_ && totalFrames > 0) calibProgressBar_->setRange(0, totalFrames);

  auto readNext = [](InputSource& s, cv::Mat& out)->bool {
    if (s.is_image_seq) {
      if (s.seq_files.isEmpty() || s.seq_idx >= s.seq_files.size()) return false;
      out = cv::imread(s.seq_files[s.seq_idx].toStdString(), cv::IMREAD_COLOR);
      s.seq_idx++;
      return !out.empty();
    }
    if (!s.cap.isOpened()) return false;
    return s.cap.read(out) && !out.empty();
  };

  int frameId = 0;
  MultiCamCalibrator previewCalib(2, cv::Size(board_w_, board_h_), square_);
  while (true) {
    cv::Mat a, b;
    {
      QMutexLocker lock(&sources_mutex_);
      if (!readNext(sources_[idx[0]], a) || !readNext(sources_[idx[1]], b)) break;
    }
    CalibrationPair p;
    p.frame_id = frameId;
    p.left = a;
    p.right = b;
    calib_pairs_.push_back(std::move(p));

    // Preview current frame and chessboard detection on the left viewer while scanning.
    std::vector<cv::Mat> pair = {a, b};
    std::vector<std::vector<cv::Point2f>> corners;
    std::vector<bool> ok;
    previewCalib.detectAndMaybeStore(pair, false, &corners, &ok);
    cv::Mat visA = a.clone();
    cv::Mat visB = b.clone();
    if (!corners.empty() && corners.size() >= 2) {
      cv::drawChessboardCorners(visA, cv::Size(board_w_, board_h_), corners[0], !ok.empty() && ok[0]);
      cv::drawChessboardCorners(visB, cv::Size(board_w_, board_h_), corners[1], ok.size() > 1 && ok[1]);
    }
    {
      QMutexLocker frameLock(&frames_mutex_);
      if (idx[0] >= 0 && idx[0] < (int)last_frames_.size()) last_frames_[idx[0]] = visA;
      if (idx[1] >= 0 && idx[1] < (int)last_frames_.size()) last_frames_[idx[1]] = visB;
      updateSourceViews(last_frames_);
    }

    frameId++;

    if (calibProgressBar_ && totalFrames > 0) calibProgressBar_->setValue(std::min(frameId, totalFrames));
    play_frame_ = std::max<int64_t>(0, frameId - 1);
    updateProgressUI(play_frame_, std::max<int64_t>(1, totalFrames));
    if (lblCalibProgress_) lblCalibProgress_->setText(QString("Progress: scanning %1 / %2").arg(frameId).arg(std::max(totalFrames, frameId)));
    QApplication::processEvents();
  }

  if (calib_pairs_.empty()) {
    QMessageBox::warning(this, "Calibration", "No frame pairs found from the selected sources.");
    return;
  }

  if (calibErrorTable_) {
    calibErrorTable_->setRowCount((int)calib_pairs_.size());
    for (int i = 0; i < (int)calib_pairs_.size(); ++i) {
      QTableWidgetItem* useItem = new QTableWidgetItem();
      useItem->setCheckState(Qt::Checked);
      useItem->setFlags(useItem->flags() | Qt::ItemIsUserCheckable);
      calibErrorTable_->setItem(i, 0, useItem);
      calibErrorTable_->setItem(i, 1, new QTableWidgetItem(QString::number(calib_pairs_[i].frame_id)));
      calibErrorTable_->setItem(i, 2, new QTableWidgetItem("-"));
    }
  }

  std::vector<int> selected;
  selected.reserve(calib_pairs_.size());
  for (int i = 0; i < (int)calib_pairs_.size(); ++i) selected.push_back(i);
  if (!runCalibrationOnPairs(selected, true)) return;
}

bool MainWindow::runCalibrationOnPairs(const std::vector<int>& pairIndices, bool updateTable) {
  if (pairIndices.empty()) {
    QMessageBox::warning(this, "Calibration", "No frame selected for calibration.");
    return false;
  }

  if (calibProgressBar_) {
    calibProgressBar_->setRange(0, (int)pairIndices.size());
    calibProgressBar_->setValue(0);
  }
  if (lblCalibProgress_) lblCalibProgress_->setText("Progress: detecting chessboard...");

  MultiCamCalibrator workCalib(2, cv::Size(board_w_, board_h_), square_);
  std::vector<cv::Size> sizes;
  bool sizesSet = false;
  int usedPairs = 0;
  std::vector<int> acceptedPairIds;
  for (int i = 0; i < (int)pairIndices.size(); ++i) {
    int id = pairIndices[i];
    if (id < 0 || id >= (int)calib_pairs_.size()) continue;
    const auto& p = calib_pairs_[id];
    std::vector<cv::Mat> pair = {p.left, p.right};
    if (!sizesSet) {
      sizes = {p.left.size(), p.right.size()};
      sizesSet = true;
    }
    if (workCalib.detectAndMaybeStore(pair, true)) {
      usedPairs++;
      acceptedPairIds.push_back(id);
    }
    if (calibProgressBar_) calibProgressBar_->setValue(i + 1);
    QApplication::processEvents();
  }

  if (usedPairs <= 0 || !sizesSet) {
    QMessageBox::warning(this, "Calibration", "No valid chessboard pairs found in selected frames.");
    return false;
  }

  if (lblCalibProgress_) lblCalibProgress_->setText("Progress: solving calibration...");
  double rms=0.0;
  std::vector<CameraModel> out;
  if (!workCalib.calibrate(sizes, out, rms)) {
    QMessageBox::warning(this, "Calibration", "Calibration failed. Ensure enough captures and paired frames with cam0.");
    logLine("Calibration failed.");
    return false;
  }

  std::vector<double> selectedRmse;
  workCalib.computeFrameReprojErrors(out, selectedRmse);
  calib_pair_rmse_.assign(calib_pairs_.size(), -1.0);
  for (int i = 0; i < (int)acceptedPairIds.size() && i < (int)selectedRmse.size(); ++i) {
    int id = acceptedPairIds[i];
    if (id >= 0 && id < (int)calib_pair_rmse_.size()) calib_pair_rmse_[id] = selectedRmse[i];
  }

  if (updateTable && calibErrorTable_) {
    for (int row = 0; row < calibErrorTable_->rowCount() && row < (int)calib_pair_rmse_.size(); ++row) {
      const double e = calib_pair_rmse_[row];
      calibErrorTable_->setItem(row, 2, new QTableWidgetItem(e >= 0.0 ? QString::number(e, 'f', 3) : "N/A"));
    }
  }

  cams_ = out;
  calib_loaded_ = true;
  has_computed_calib_ = true;
  if (btnSaveCalib_) btnSaveCalib_->setEnabled(true);
  if (solveWorker_) solveWorker_->setStaticData(&cams_, &tag_corner_map_);
  auto qualityText = [](double val)->QString {
    if (val < 0.5) return "Excellent";
    if (val < 1.0) return "Good";
    if (val < 2.0) return "Fair";
    return "Poor";
  };
  const QString quality = qualityText(rms);
  if (lblCalibProgress_) {
    lblCalibProgress_->setText(QString("Progress: done, mean RMS=%1 (%2)").arg(rms, 0, 'f', 4).arg(quality));
  }
  logLine(QString("Calibration OK. mean RMS=%1 (%2)").arg(rms, 0, 'f', 4).arg(quality));
  return true;
}

void MainWindow::onRecomputeCalibrationSelected() {
  if (calib_pairs_.empty() || !calibErrorTable_) {
    QMessageBox::information(this, "Calibration", "Please run Compute Calibration first.");
    return;
  }

  std::vector<int> selected;
  for (int row = 0; row < calibErrorTable_->rowCount(); ++row) {
    QTableWidgetItem* item = calibErrorTable_->item(row, 0);
    if (item && item->checkState() == Qt::Checked) selected.push_back(row);
  }
  runCalibrationOnPairs(selected, true);
}

void MainWindow::onSaveCalibrationYaml() {
  if (!has_computed_calib_ || cams_.empty()) {
    QMessageBox::information(this, "Save", "Please compute calibration first.");
    return;
  }

  QString savePath = QFileDialog::getSaveFileName(this, "Save rig_calib.yaml", "rig_calib.yaml", "YAML (*.yaml *.yml)");
  if (savePath.isEmpty()) return;

  calib_path_ = savePath;
  if (lblYamlPath_) lblYamlPath_->setText(QString("Calib: %1").arg(calib_path_));
  if (!saveRigCalibYaml(savePath.toStdString(), cams_)) {
    QMessageBox::warning(this, "Save", "Failed to save YAML.");
    return;
  }
  logLine(QString("Calibration YAML saved: %1").arg(savePath));
  settings_.setValue("lastCalibYaml", savePath);
}

// ----------------- Tracking actions -----------------
void MainWindow::onLoadTagMap() {
  QString last = settings_.value("lastTagMap", "tag_corners_world.txt").toString();
  QString path = QFileDialog::getOpenFileName(this, "Load tag map TXT", last, "Text (*.txt);;All (*.*)");
  if (path.isEmpty()) return;

  tagmap_path_ = path;
  tagmap_loaded_ = loadTagCornersTxt(path.toStdString(), tag_corner_map_);
  if (lblTagPath_) lblTagPath_->setText(QString("TagMap: %1").arg(tagmap_path_));
  if (!tagmap_loaded_) {
    QMessageBox::warning(this, "Tag map", "Failed to load tag map TXT.");
    logLine("Failed to load tag map.");
    return;
  }
  settings_.setValue("lastTagMap", path);
  logLine(QString("Loaded tag map: %1").arg(path));
}

void MainWindow::onLoadCalibYaml() {
  QString last = settings_.value("lastCalibYaml", "rig_calib.yaml").toString();
  QString path = QFileDialog::getOpenFileName(this, "Load calibration YAML", last, "YAML (*.yaml *.yml);;All (*.*)");
  if (path.isEmpty()) return;

  calib_path_ = path;
  calib_loaded_ = loadRigCalibYaml(path.toStdString(), cams_);
  if (lblYamlPath_) lblYamlPath_->setText(QString("Calib: %1").arg(calib_path_));
  if (solveWorker_) solveWorker_->setStaticData(&cams_, &tag_corner_map_);
  if (!calib_loaded_) {
    QMessageBox::warning(this, "Calibration", "Failed to load calibration YAML.");
    logLine("Failed to load calibration yaml.");
    return;
  }
  settings_.setValue("lastCalibYaml", path);
  logLine(QString("Loaded calib yaml: %1").arg(path));
}

void MainWindow::onTogglePose(bool on) {
  pose_on_ = on;
  if (solveWorker_) solveWorker_->setParams(ransac_iters_, inlier_thresh_px_, tag_dict_id_, pose_on_);
  logLine(QString("Pose estimation %1").arg(on ? "ON" : "OFF"));
}

void MainWindow::onPrintPose() {
  Eigen::AngleAxisd aa(R_wr_);
  QString s = QString("POSE t=[%1,%2,%3] ang=%4 axis=[%5,%6,%7] inliers=%8")
    .arg(t_wr_.x(),0,'f',6).arg(t_wr_.y(),0,'f',6).arg(t_wr_.z(),0,'f',6)
    .arg(aa.angle(),0,'f',6)
    .arg(aa.axis().x(),0,'f',6).arg(aa.axis().y(),0,'f',6).arg(aa.axis().z(),0,'f',6)
    .arg(last_inliers_);
  logLine(s);
}


// ----------------- Sources: pause/resume -----------------
void MainWindow::onPauseResumeSelected() {
  bool toEnable = true;
  {
    QMutexLocker locker(&sources_mutex_);
    if ((int)source_enabled_.size() != (int)sources_.size()) source_enabled_.assign(sources_.size(), true);
    bool anyPaused = false;
    for (bool en : source_enabled_) if (!en) { anyPaused = true; break; }
    toEnable = anyPaused;
    for (int i=0;i<(int)source_enabled_.size();++i) source_enabled_[i] = toEnable;
  }
  logLine(QString("All sources %1").arg(toEnable ? "RESUMED" : "PAUSED"));
  refreshSourceList();
}

// ----------------- Tracking: export trajectory -----------------
void MainWindow::onExportTrajectory() {
  if (traj_.empty()) {
    QMessageBox::information(this, "Export", "No trajectory collected yet. Turn Pose ON and ensure tags are detected.");
    return;
  }
  QString path = QFileDialog::getSaveFileName(this, "Export Trajectory CSV", "trajectory.csv", "CSV (*.csv)");
  if (path.isEmpty()) return;

  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::warning(this, "Export", "Failed to open file for writing.");
    return;
  }
  QByteArray header = "t_ms,tx,ty,tz,aax,aay,aaz,inliers\n";
  f.write(header);
  for (const auto& r : traj_) {
    QString line = QString("%1,%2,%3,%4,%5,%6,%7,%8\n")
      .arg(r.t_ms)
      .arg(r.t.x(),0,'f',9).arg(r.t.y(),0,'f',9).arg(r.t.z(),0,'f',9)
      .arg(r.aa.x(),0,'f',9).arg(r.aa.y(),0,'f',9).arg(r.aa.z(),0,'f',9)
      .arg(r.inliers);
    f.write(line.toUtf8());
  }
  f.close();
  logLine(QString("Trajectory exported: %1 (rows=%2)").arg(path).arg(traj_.size()));
}

// ----------------- Project config: save/load -----------------
void MainWindow::onSaveProject() {
  QString path = QFileDialog::getSaveFileName(this, "Save Project Config", "project.json", "JSON (*.json)");
  if (path.isEmpty()) return;

  QJsonObject o = toProjectJson();
  QJsonDocument doc(o);

  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::warning(this, "Save Project", "Failed to write project file.");
    return;
  }
  f.write(doc.toJson(QJsonDocument::Indented));
  f.close();
  logLine(QString("Project saved: %1").arg(path));
}

void MainWindow::onLoadProject() {
  QString path = QFileDialog::getOpenFileName(this, "Load Project Config", "", "JSON (*.json)");
  if (path.isEmpty()) return;

  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, "Load Project", "Failed to open project file.");
    return;
  }
  QByteArray data = f.readAll();
  f.close();

  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(data, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    QMessageBox::warning(this, "Load Project", "Invalid JSON.");
    return;
  }
  fromProjectJson(doc.object());
  logLine(QString("Project loaded: %1").arg(path));
}


void MainWindow::onFramesFromWorker(FramePack frames, qint64 capture_ts_ms) {
  last_capture_ts_ms_ = capture_ts_ms;
  {
    QMutexLocker locker(&frames_mutex_);
    last_frames_ = std::move(frames); // cv::Mat ref-counted
  }

  int64_t framePos = play_frame_;
  int64_t frameEnd = play_end_frame_;
  {
    QMutexLocker srcLock(&sources_mutex_);
    for (const auto& s : sources_) {
      if (s.is_cam || s.mode_owner!=(int)mode_) continue;
      if (s.is_image_seq) {
        framePos = std::max<int64_t>(0, s.seq_idx);
        if (frameEnd <= 0) frameEnd = (int64_t)s.seq_files.size();
        break;
      }
      if (!s.cap.isOpened()) continue;
      framePos = std::max<int64_t>(0, (int64_t)std::llround(s.cap.get(cv::CAP_PROP_POS_FRAMES)) - 1);
      double fc = s.cap.get(cv::CAP_PROP_FRAME_COUNT);
      if (frameEnd <= 0 && fc > 0) frameEnd = (int64_t)fc;
      break;
    }
  }
  play_frame_ = framePos;
  if (play_end_frame_ <= 0 && frameEnd > 0) play_end_frame_ = frameEnd;
  updateProgressUI(play_frame_, play_end_frame_);

  // forward to solver thread (queued)
  if (solveWorker_) {
    // handled via signal/slot wiring in constructor
  }
}

void MainWindow::onPoseFromWorker(const PoseResult& r) {
  if (r.ok) {
    t_wr_ = Eigen::Vector3d(r.t[0], r.t[1], r.t[2]);
    Eigen::Vector3d aavec(r.aa[0], r.aa[1], r.aa[2]);
    double angle = aavec.norm();
    Eigen::Vector3d axis = (angle < 1e-12) ? Eigen::Vector3d(1,0,0) : (aavec/angle);
    R_wr_ = Eigen::AngleAxisd(angle, axis).toRotationMatrix();
    last_inliers_ = r.inliers;
    traj_.push_back(TrajRow{ r.solve_ts_ms, t_wr_, aavec, r.inliers });
  }
  if (lblLatency_) lblLatency_->setText(QString("Latency: %1 ms (obs=%2)")
                                       .arg(r.latency_ms,0,'f',1).arg(r.obs_count));
}

void MainWindow::rebuildSourceDocks() {
  // Remove existing docks
  for (auto* d : camDocks_) {
    if (d) { removeDockWidget(d); d->deleteLater(); }
  }
  camDocks_.clear();
  camLabels_.clear();

  for (int i=0;i<(int)sources_.size();++i) {
    QDockWidget* d = new QDockWidget(QString("View %1").arg(i), this);
    d->setAllowedAreas(Qt::AllDockWidgetAreas);
    QLabel* lab = new QLabel(d);
    lab->setAlignment(Qt::AlignCenter);
    lab->setMinimumSize(320, 240);
    lab->setStyleSheet("background-color: #111; border: 1px solid #333;");
    d->setWidget(lab);
    addDockWidget(Qt::BottomDockWidgetArea, d);
    camDocks_.push_back(d);
    camLabels_.push_back(lab);
  }
}

void MainWindow::updateSourceDocks(const std::vector<cv::Mat>& frames) {
  if ((int)camLabels_.size() != (int)frames.size()) return;
  for (int i=0;i<(int)frames.size();++i) {
    if (!camLabels_[i]) continue;
    if (frames[i].empty()) continue;
    QImage img = matToQImage(frames[i]);
    camLabels_[i]->setPixmap(QPixmap::fromImage(img).scaled(
      camLabels_[i]->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }
}

void MainWindow::onSaveLayout() {
  settings_.setValue("mainWindow/geometry", saveGeometry());
  settings_.setValue("mainWindow/state", saveState());
  logLine("Layout saved to settings.");
}

void MainWindow::onRestoreLayout() {
  if (settings_.contains("mainWindow/geometry")) restoreGeometry(settings_.value("mainWindow/geometry").toByteArray());
  if (settings_.contains("mainWindow/state")) restoreState(settings_.value("mainWindow/state").toByteArray());
  logLine("Layout restored from settings.");
}

void MainWindow::onToggleDocks(bool on) {
  show_docks_ = on;
  if (show_docks_) {
    rebuildSourceDocks();
    logLine("Per-source docks: ON");
  } else {
    // remove existing docks
    for (auto* d : camDocks_) {
      if (d) { removeDockWidget(d); d->deleteLater(); }
    }
    camDocks_.clear();
    camLabels_.clear();
    logLine("Per-source docks: OFF");
  }
}


int MainWindow::videoSourceCount() const {
  int c=0;
  for (const auto& s : sources_) if (!s.is_cam && s.mode_owner==(int)mode_) c++;
  return c;
}

void MainWindow::stopCaptureBlocking() {
  if (!captureWorker_) return;
  // BlockingQueuedConnection ensures timer stops before returning
  QMetaObject::invokeMethod(captureWorker_, "stop", Qt::BlockingQueuedConnection);
}

void MainWindow::updatePlaybackParams() {
  if (!captureWorker_) return;
  int vidN = videoSourceCount();
  bool sync = sync_play_ && (vidN >= 2); // only meaningful with >=2 videos
  QMetaObject::invokeMethod(captureWorker_, "setSyncModeSlot", Qt::BlockingQueuedConnection, Q_ARG(bool, sync));

  // compute range and fps for sync video playback
  if (sync) {
    int64_t minFrames = (int64_t)9e18;
    double fps = 0.0;
    bool found=false;
    QMutexLocker srcLock(&sources_mutex_);
    for (auto& s : sources_) {
      if (s.is_cam || s.mode_owner!=(int)mode_) continue;
      if (s.is_image_seq) {
        int64_t fc = (int64_t)s.seq_files.size();
        if (fc > 0) { minFrames = std::min(minFrames, fc); found = true; }
        if (fps <= 0.0) fps = 30.0;
        continue;
      }
      if (!s.cap.isOpened()) continue;
      double fc = s.cap.get(cv::CAP_PROP_FRAME_COUNT);
      if (fc > 0) { minFrames = std::min(minFrames, (int64_t)fc); found=true; }
      if (fps <= 0.0) {
        double vf = s.cap.get(cv::CAP_PROP_FPS);
        if (vf > 0.0) fps = vf;
      }
    }
    if (!found || minFrames <= 0) {
      minFrames = 0;
    }
    play_end_frame_ = minFrames;
    play_fps_ = (fps > 0.0 ? fps : 30.0);
    QMetaObject::invokeMethod(captureWorker_, "setPlaybackRangeSlot", Qt::BlockingQueuedConnection,
                              Q_ARG(qint64, (qint64)0), Q_ARG(qint64, (qint64)play_end_frame_), Q_ARG(double, play_fps_));
  } else {
    int64_t maxFrames = 0;
    QMutexLocker srcLock(&sources_mutex_);
    for (const auto& s : sources_) {
      if (s.is_cam || s.mode_owner!=(int)mode_) continue;
      if (s.is_image_seq) {
        maxFrames = std::max<int64_t>(maxFrames, (int64_t)s.seq_files.size());
        continue;
      }
      if (!s.cap.isOpened()) continue;
      double fc = s.cap.get(cv::CAP_PROP_FRAME_COUNT);
      if (fc > 0) maxFrames = std::max<int64_t>(maxFrames, (int64_t)fc);
    }
    play_end_frame_ = maxFrames;
  }
  updateProgressUI(play_frame_, play_end_frame_);
}


void MainWindow::updateProgressUI(int64_t frame, int64_t endFrame) {
  if (!progressSlider_) return;
  int maxVal = (int)std::max<int64_t>(0, endFrame > 0 ? (endFrame - 1) : 0);
  int val = (int)std::max<int64_t>(0, std::min<int64_t>(frame, maxVal));
  progressSlider_->setRange(0, maxVal);
  progressSlider_->blockSignals(true);
  progressSlider_->setValue(val);
  progressSlider_->blockSignals(false);
  if (editCurFrame_) editCurFrame_->setText(QString::number(val));
  if (lblTotalFrame_) lblTotalFrame_->setText(QString("/ %1").arg(maxVal));
}


void MainWindow::stepAllVideos(int delta) {
  stopCaptureBlocking();
  playback_running_ = false;
  bool stepped = false;
  int64_t progressFrame = play_frame_;
  std::vector<cv::Mat> steppedFrames;

  {
    QMutexLocker srcLock(&sources_mutex_);
    steppedFrames.resize(sources_.size());
    for (int i=0;i<(int)sources_.size();++i) {
      auto& src = sources_[i];
      if (src.is_cam || src.mode_owner!=(int)mode_) continue;
      cv::Mat f;
      int64_t target = 0;
      if (src.is_image_seq) {
        int64_t cur = src.seq_idx;
        target = std::max<int64_t>(0, cur + delta);
        if (!src.seq_files.isEmpty()) target = std::min<int64_t>(target, (int64_t)src.seq_files.size()-1);
        src.seq_idx = (int)target;
        f = cv::imread(src.seq_files[(int)target].toStdString(), cv::IMREAD_COLOR);
      } else {
        if (!src.cap.isOpened()) continue;
        double cur = src.cap.get(cv::CAP_PROP_POS_FRAMES)-1;
        target = std::max<int64_t>(0, (int64_t)std::llround(cur) + delta);
        if (play_end_frame_ > 0) target = std::min<int64_t>(target, play_end_frame_-1);
        src.cap.set(cv::CAP_PROP_POS_FRAMES, (double)target);
        src.cap.read(f);
      }
      if (!f.empty()) {
        steppedFrames[i] = f;
        progressFrame = target;
        stepped = true;
      }
    }
  }

  if (stepped) {
    {
      QMutexLocker frameLock(&frames_mutex_);
      if ((int)last_frames_.size() != (int)steppedFrames.size()) last_frames_.resize(steppedFrames.size());
      for (int i=0;i<(int)steppedFrames.size();++i) {
        if (!steppedFrames[i].empty()) last_frames_[i] = steppedFrames[i];
      }
    }
    play_frame_ = progressFrame;
    updateProgressUI(play_frame_, play_end_frame_);
    logLine(QString("Step frame: %1").arg(delta > 0 ? "next" : "prev"));
  } else {
    logLine("Step frame ignored: no video source ready.");
  }
}

void MainWindow::onStepPrevFrame() { stepAllVideos(-1); }
void MainWindow::onStepNextFrame() { stepAllVideos(1); }

void MainWindow::onToolPan() {
  if (btnToolPan_) btnToolPan_->setChecked(true);
  if (btnToolPoint_) btnToolPoint_->setChecked(false);
  if (btnToolLine_) btnToolLine_->setChecked(false);
  for (auto* v : sourceViews_) if (v) v->setToolMode(ImageViewer::PanTool);
}

void MainWindow::onToolPoint() {
  if (btnToolPan_) btnToolPan_->setChecked(false);
  if (btnToolPoint_) btnToolPoint_->setChecked(true);
  if (btnToolLine_) btnToolLine_->setChecked(false);
  for (auto* v : sourceViews_) if (v) v->setToolMode(ImageViewer::PointTool);
}

void MainWindow::onToolLine() {
  if (btnToolPan_) btnToolPan_->setChecked(false);
  if (btnToolPoint_) btnToolPoint_->setChecked(false);
  if (btnToolLine_) btnToolLine_->setChecked(true);
  for (auto* v : sourceViews_) if (v) v->setToolMode(ImageViewer::LineTool);
}

void MainWindow::onZoomIn() { for (auto* v : sourceViews_) if (v) v->zoomIn(); }
void MainWindow::onZoomOut() { for (auto* v : sourceViews_) if (v) v->zoomOut(); }
void MainWindow::onResetView() { for (auto* v : sourceViews_) if (v) v->resetView(); }
void MainWindow::onClearAnnotations() { for (auto* v : sourceViews_) if (v) v->clearAnnotations(); }

void MainWindow::onProgressSliderReleased() {
  if (!progressSlider_) return;
  int target = progressSlider_->value();
  {
    QMutexLocker srcLock(&sources_mutex_);
    for (auto& src : sources_) {
      if (src.is_cam || src.mode_owner!=(int)mode_) continue;
      if (src.is_image_seq) {
        if (!src.seq_files.isEmpty()) src.seq_idx = std::max(0, std::min(target, (int)src.seq_files.size()-1));
        continue;
      }
      if (!src.cap.isOpened()) continue;
      src.cap.set(cv::CAP_PROP_POS_FRAMES, (double)target);
    }
  }
  play_frame_ = target;
  stepAllVideos(0);
}

void MainWindow::onFrameJumpReturnPressed() {
  if (!editCurFrame_ || !progressSlider_) return;
  bool ok = false;
  int target = editCurFrame_->text().toInt(&ok);
  if (!ok) return;
  target = std::max(progressSlider_->minimum(), std::min(progressSlider_->maximum(), target));
  progressSlider_->setValue(target);
  onProgressSliderReleased();
}

void MainWindow::onPlayAll() {
  if (playback_running_) {
    playback_running_ = false;
    stopCaptureBlocking();
    {
      QMutexLocker srcLock(&sources_mutex_);
      for (int i=0;i<(int)sources_.size();++i) {
        if (!sources_[i].is_cam && sources_[i].mode_owner==(int)mode_) source_enabled_[i] = false;
      }
    }
    if (btnPlayAll_) {
      btnPlayAll_->setChecked(false);
      btnPlayAll_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
      btnPlayAll_->setToolTip("Play");
    }
    logLine("Playback paused.");
    refreshSourceList();
    return;
  }

  // Start playback when user presses Play.
  int vidN = videoSourceCount();
  if (vidN < 2) {
    QMessageBox::information(this, "Play", "Need at least 2 video sources for synchronized playback.");
    return;
  }

  stopCaptureBlocking();

  {
    QMutexLocker srcLock(&sources_mutex_);
    // Enable all video sources for playback
    for (int i=0;i<(int)sources_.size();++i) if (!sources_[i].is_cam && sources_[i].mode_owner==(int)mode_) source_enabled_[i]=true;

  }
  playback_running_ = true;
  if (btnPlayAll_) {
    btnPlayAll_->setChecked(true);
    btnPlayAll_->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    btnPlayAll_->setToolTip("Pause");
  }
  updateProgressUI(play_frame_, play_end_frame_);
  //if (lblPlayState_) 
  //    lblPlayState_->setText("State: PLAY (SYNC)");

  updatePlaybackParams();
  timer_.start(33);
  QMetaObject::invokeMethod(captureWorker_, "start", Qt::QueuedConnection);
}

void MainWindow::onStopAll() {
  playback_running_ = false;
  if (btnPlayAll_) {
    btnPlayAll_->setChecked(false);
    btnPlayAll_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    btnPlayAll_->setToolTip("Play");
  }
  //if (lblPlayState_) lblPlayState_->setText("State: STOP");
  stopCaptureBlocking();
  updateProgressUI(play_frame_, play_end_frame_);
}
