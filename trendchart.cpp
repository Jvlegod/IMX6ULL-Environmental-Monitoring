#include "trendchart.h"
#include <QPainter>
#include <QPainterPath>
TrendChart::TrendChart(QWidget *parent) : QWidget(parent) { setMinimumHeight(260); }
void TrendChart::setSeries(const QVector<double> &temperature, const QVector<double> &humidity,
                           const QVector<double> &pressure, const QVector<double> &illuminance)
{ temperature_ = temperature; humidity_ = humidity; pressure_ = pressure; illuminance_ = illuminance; update(); }
void TrendChart::paintEvent(QPaintEvent *)
{
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.fillRect(rect(), Qt::white);
    const QRectF plot = rect().adjusted(42, 18, -18, -42);
    p.setPen(QPen(QColor("#e7edf0"), 1));
    for (int i = 0; i <= 4; ++i) { const qreal y = plot.top() + plot.height() * i / 4.0; p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y)); }
    p.setPen(QColor("#73828b")); p.drawText(QRectF(0, plot.top() - 8, 38, 20), Qt::AlignRight, QStringLiteral("1100")); p.drawText(QRectF(0, plot.bottom() - 10, 38, 20), Qt::AlignRight, QStringLiteral("0"));
    const QVector<QColor> colors{QColor("#ef8354"), QColor("#4ea5d9"), QColor("#6c8ead"), QColor("#e5b94c")};
    const QVector<QVector<double>> values{temperature_, humidity_, pressure_, illuminance_};
    for (int s = 0; s < values.size(); ++s) {
        if (values[s].size() < 2) continue;
        double minValue = 0.0;
        double maxValue = 0.0;
        bool hasValue = false;
        for (double value : values[s]) {
            if (!qIsFinite(value)) continue;
            if (!hasValue) { minValue = maxValue = value; hasValue = true; }
            else { minValue = qMin(minValue, value); maxValue = qMax(maxValue, value); }
        }
        if (!hasValue) continue;
        const double span = qMax(0.001, maxValue - minValue);
        QPainterPath path;
        for (int i = 0; i < values[s].size(); ++i) {
            if (!qIsFinite(values[s][i])) continue;
            const qreal x = plot.left() + plot.width() * i / qMax(1, values[s].size() - 1);
            const qreal y = plot.bottom() - qBound(0.0, (values[s][i] - minValue) / span, 1.0) * plot.height();
            if (path.isEmpty()) path.moveTo(x, y); else path.lineTo(x, y);
        }
        p.setPen(QPen(colors[s], 2)); p.drawPath(path);
    }
    const QStringList labels{QStringLiteral("温度 °C"), QStringLiteral("湿度 %"), QStringLiteral("压力 kPa"), QStringLiteral("光照 lux")}; int x = plot.left();
    for (int i = 0; i < colors.size(); ++i) { p.setPen(QPen(colors[i], 2)); p.drawLine(x, height() - 20, x + 18, height() - 20); p.setPen(QColor("#65737c")); p.drawText(x + 24, height() - 14, labels[i]); x += 100; }
}
