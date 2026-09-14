#include "trendchart.h"
#include <QPainter>
#include <QPainterPath>
TrendChart::TrendChart(QWidget *parent) : QWidget(parent) { setMinimumHeight(260); }
void TrendChart::setSeries(const QVector<double> &temperature, const QVector<double> &humidity,
                           const QVector<double> &pressure, const QVector<double> &illuminance)
{ temperature_ = temperature; humidity_ = humidity; pressure_ = pressure; illuminance_ = illuminance; update(); }
void TrendChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::white);

    const QRectF plot = rect().adjusted(54, 18, -24, -42);
    if (plot.width() < 20 || plot.height() < 20) return;

    const QVector<QColor> colors{QColor("#ef8354"), QColor("#4ea5d9"), QColor("#6c8ead"), QColor("#e5b94c")};
    const QVector<QVector<double>> values{temperature_, humidity_, pressure_, illuminance_};
    const QStringList names{QStringLiteral("温度"), QStringLiteral("湿度"), QStringLiteral("压力"), QStringLiteral("光照")};
    const QStringList units{QStringLiteral(" °C"), QStringLiteral(" %"), QStringLiteral(" kPa"), QStringLiteral(" lux")};

    double globalMin = 0.0, globalMax = 100.0;
    bool hasGlobal = false;
    for (int s = 0; s < values.size(); ++s) {
        for (double v : values[s]) {
            if (!qIsFinite(v)) continue;
            if (!hasGlobal) { globalMin = globalMax = v; hasGlobal = true; }
            else { globalMin = qMin(globalMin, v); globalMax = qMax(globalMax, v); }
        }
    }
    if (!hasGlobal) return;

    const double pad = qMax(0.001, (globalMax - globalMin) * 0.1);
    const double yMin = globalMin - pad;
    const double yMax = globalMax + pad;
    const double ySpan = qMax(0.001, yMax - yMin);

    QFont labelFont = p.font();
    labelFont.setPixelSize(10);
    p.setFont(labelFont);
    for (int i = 0; i <= 4; ++i) {
        const qreal y = plot.top() + plot.height() * i / 4.0;
        p.setPen(QPen(QColor("#e7edf0"), 1));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        const double val = yMax - (yMax - yMin) * i / 4.0;
        p.setPen(QColor("#73828b"));
        p.drawText(QRectF(0, y - 10, 48, 20), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(val, 'f', val >= 100 ? 0 : 1));
    }

    for (int s = 0; s < values.size(); ++s) {
        if (values[s].size() < 2) continue;
        QPainterPath path;
        bool first = true;
        double lx = 0, ly = 0;
        for (int i = 0; i < values[s].size(); ++i) {
            if (!qIsFinite(values[s][i])) continue;
            const qreal x = plot.left() + plot.width() * i / qMax(1, values[s].size() - 1);
            const qreal y = plot.bottom() - qBound(0.0, (values[s][i] - yMin) / ySpan, 1.0) * plot.height();
            if (first) { path.moveTo(x, y); first = false; }
            else path.lineTo(x, y);
            lx = x; ly = y;
        }
        if (!first) {
            p.setPen(QPen(colors[s], 3));
            p.drawPath(path);
            const double lastVal = values[s].last();
            if (qIsFinite(lastVal)) {
                labelFont.setPixelSize(10);
                p.setFont(labelFont);
                p.setPen(colors[s]);
                p.drawText(QRectF(lx + 4, ly - 10, 200, 20), Qt::AlignLeft | Qt::AlignVCenter,
                           QString::number(lastVal, 'f', 1));
            }
        }
    }

    int legendX = plot.left();
    labelFont.setPixelSize(11);
    p.setFont(labelFont);
    for (int i = 0; i < colors.size(); ++i) {
        p.setPen(QPen(colors[i], 3));
        p.drawLine(legendX, height() - 20, legendX + 18, height() - 20);
        p.setPen(QColor("#65737c"));
        p.drawText(legendX + 22, height() - 22, 60, 20, Qt::AlignLeft | Qt::AlignVCenter,
                   names[i] + units[i]);
        legendX += 100;
    }
}
