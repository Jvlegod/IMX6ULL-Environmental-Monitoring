#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "sensorprovider.h"
#include <QMainWindow>
#include <QVector>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QTableWidget;
QT_END_NAMESPACE
class TrendChart;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(ISensorProvider *provider, QWidget *parent = nullptr);
private slots:
    void updateSnapshot(const SensorSnapshot &snapshot);
    void updateDeviceStatus(const QString &device, bool connected, const QString &detail);
    void toggleSampling();
private:
    QWidget *makeMetricCard(const QString &title, const QString &accent,
                            QLabel **valueLabel, QLabel **unitLabel);
    void setAlert(const QString &message, bool active);
    void appendSeries(QVector<double> *series, double value);
    ISensorProvider *provider_;
    QPushButton *samplingButton_;
    QLabel *lastUpdateLabel_;
    QLabel *alertLabel_;
    QLabel *temperatureValue_;
    QLabel *temperatureUnit_;
    QLabel *humidityValue_;
    QLabel *humidityUnit_;
    QLabel *pressureValue_;
    QLabel *pressureUnit_;
    QLabel *illuminanceValue_;
    QLabel *illuminanceUnit_;
    QTableWidget *statusTable_;
    TrendChart *chartView_;
    QVector<double> temperatureSeries_;
    QVector<double> humiditySeries_;
    QVector<double> pressureSeries_;
    QVector<double> illuminanceSeries_;
};

#endif
