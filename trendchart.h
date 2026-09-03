#ifndef TRENDCHART_H
#define TRENDCHART_H
#include <QWidget>
#include <QVector>
class TrendChart final : public QWidget
{
    Q_OBJECT
public:
    explicit TrendChart(QWidget *parent = nullptr);
    void setSeries(const QVector<double> &temperature, const QVector<double> &humidity,
                   const QVector<double> &pressure, const QVector<double> &illuminance);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    QVector<double> temperature_, humidity_, pressure_, illuminance_;
};
#endif
