#include "QCustomPlot.h"

#include <QPainter>
#include <QPaintEvent>
#include <algorithm>

QCPItemStraightLine::QCPItemStraightLine(QCustomPlot*) {
  point1 = new QCPItemPosition();
  point2 = new QCPItemPosition();
}

QCPItemStraightLine::~QCPItemStraightLine() {
  delete point1;
  delete point2;
}

void QCPItemStraightLine::draw(QPainter& p, const QCustomPlot& plot, const QRect& plotRect) {
  p.setPen(pen_);
  QPointF a = plot.mapToPixel(point1->key(), point1->value(), plotRect);
  QPointF b = plot.mapToPixel(point2->key(), point2->value(), plotRect);
  p.drawLine(a, b);
}

QCPItemText::QCPItemText(QCustomPlot*) {
  position = new QCPItemPosition();
}

QCPItemText::~QCPItemText() {
  delete position;
}

void QCPItemText::draw(QPainter& p, const QCustomPlot& plot, const QRect& plotRect) {
  p.setPen(color_);
  QPointF pt = plot.mapToPixel(position->key(), position->value(), plotRect);
  p.drawText(pt + QPointF(6, -6), text_);
}

QCustomPlot::QCustomPlot(QWidget* parent) : QWidget(parent) {
  xAxis = new QCPAxis();
  yAxis = new QCPAxis();
  legend = new QCPLegend();
  setMinimumHeight(160);
}

QCustomPlot::~QCustomPlot() {
  clearItems();
  qDeleteAll(graphs_);
  delete xAxis;
  delete yAxis;
  delete legend;
}

void QCustomPlot::addGraph() {
  graphs_.push_back(new QCPGraph());
}

QCPGraph* QCustomPlot::graph(int index) const {
  if (index < 0 || index >= graphs_.size()) return nullptr;
  return graphs_[index];
}

void QCustomPlot::clearItems() {
  qDeleteAll(items_);
  items_.clear();
}

void QCustomPlot::addItem(QCPAbstractItem* item) {
  if (item) items_.push_back(item);
}

QPointF QCustomPlot::mapToPixel(double key, double value, const QRect& plotRect) const {
  const auto xr = xAxis->range();
  const auto yr = yAxis->range();
  const double xSpan = std::max(1e-12, xr.upper - xr.lower);
  const double ySpan = std::max(1e-12, yr.upper - yr.lower);
  const double xNorm = (key - xr.lower) / xSpan;
  const double yNorm = (value - yr.lower) / ySpan;
  const double x = plotRect.left() + xNorm * plotRect.width();
  const double y = plotRect.bottom() - yNorm * plotRect.height();
  return QPointF(x, y);
}

void QCustomPlot::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor(29, 35, 43));

  QRect plotRect = rect().adjusted(44, 18, -16, -26);
  if (plotRect.width() < 20 || plotRect.height() < 20) return;

  p.setPen(QPen(QColor(70, 80, 94), 1));
  p.drawRect(plotRect);

  for (QCPGraph* g : graphs_) {
    if (!g) continue;
    const auto& xs = g->xs();
    const auto& ys = g->ys();
    if (xs.size() < 2 || ys.size() != xs.size()) continue;

    QPainterPath path;
    path.moveTo(mapToPixel(xs[0], ys[0], plotRect));
    for (int i = 1; i < xs.size(); ++i) {
      path.lineTo(mapToPixel(xs[i], ys[i], plotRect));
    }
    p.setPen(g->pen());
    p.drawPath(path);
  }

  for (QCPAbstractItem* item : items_) {
    if (item) item->draw(p, *this, plotRect);
  }

  p.setPen(QColor(170, 182, 198));
  p.drawText(8, height() - 8, yAxis->label());
  p.drawText(width() - 24, height() - 8, xAxis->label());
}
