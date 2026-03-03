#pragma once

#include <QWidget>
#include <QVector>
#include <QPen>
#include <QString>
#include <QPointF>

class QCPAxis;
class QCPGraph;
class QCPAbstractItem;
class QCPItemPosition;

namespace QCP {
enum Interaction {
  iRangeDrag = 0x001,
  iRangeZoom = 0x002
};
}

class QCPRange {
public:
  QCPRange() = default;
  QCPRange(double l, double u) : lower(l), upper(u) {}
  double lower = 0.0;
  double upper = 1.0;
};

class QCPLegend {
public:
  void setVisible(bool v) { visible_ = v; }
  bool visible() const { return visible_; }
private:
  bool visible_ = false;
};

class QCPAxis {
public:
  void setLabel(const QString& s) { label_ = s; }
  const QString& label() const { return label_; }
  void setRange(double lower, double upper) { range_ = QCPRange(lower, upper); }
  const QCPRange& range() const { return range_; }
private:
  QString label_;
  QCPRange range_{0.0, 1.0};
};

class QCPGraph {
public:
  void setData(const QVector<double>& x, const QVector<double>& y) { x_ = x; y_ = y; }
  void setPen(const QPen& p) { pen_ = p; }
  void setName(const QString& n) { name_ = n; }

  const QVector<double>& xs() const { return x_; }
  const QVector<double>& ys() const { return y_; }
  const QPen& pen() const { return pen_; }
  const QString& name() const { return name_; }

private:
  QVector<double> x_;
  QVector<double> y_;
  QPen pen_{Qt::white, 1.5};
  QString name_;
};

class QCPItemPosition {
public:
  void setCoords(double key, double value) { key_ = key; value_ = value; }
  double key() const { return key_; }
  double value() const { return value_; }
private:
  double key_ = 0.0;
  double value_ = 0.0;
};

class QCustomPlot;

class QCPAbstractItem {
public:
  virtual ~QCPAbstractItem() = default;
  virtual void draw(class QPainter& p, const QCustomPlot& plot, const QRect& plotRect) = 0;
};

class QCPItemStraightLine : public QCPAbstractItem {
public:
  explicit QCPItemStraightLine(QCustomPlot* plot);
  ~QCPItemStraightLine() override;

  QCPItemPosition* point1;
  QCPItemPosition* point2;
  void setPen(const QPen& p) { pen_ = p; }

  void draw(class QPainter& p, const QCustomPlot& plot, const QRect& plotRect) override;
private:
  QPen pen_{Qt::red, 1.2};
};

class QCPItemText : public QCPAbstractItem {
public:
  explicit QCPItemText(QCustomPlot* plot);
  ~QCPItemText() override;

  QCPItemPosition* position;
  void setText(const QString& t) { text_ = t; }
  void setColor(const QColor& c) { color_ = c; }

  void draw(class QPainter& p, const QCustomPlot& plot, const QRect& plotRect) override;
private:
  QString text_;
  QColor color_{Qt::white};
};

class QCustomPlot : public QWidget {
public:
  explicit QCustomPlot(QWidget* parent=nullptr);
  ~QCustomPlot() override;

  QCPAxis* xAxis;
  QCPAxis* yAxis;
  QCPLegend* legend;

  void setInteractions(int flags) { interactions_ = flags; Q_UNUSED(interactions_); }
  void addGraph();
  QCPGraph* graph(int index) const;
  int graphCount() const { return graphs_.size(); }
  void clearItems();
  void addItem(QCPAbstractItem* item);
  void replot() { update(); }

  QPointF mapToPixel(double key, double value, const QRect& plotRect) const;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  int interactions_ = 0;
  QVector<QCPGraph*> graphs_;
  QVector<QCPAbstractItem*> items_;
};
