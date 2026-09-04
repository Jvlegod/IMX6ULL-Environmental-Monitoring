#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "sensorprovider.h"
#include <QDateTime>
#include <QFile>
#include <QMainWindow>
#include <QVector>

QT_BEGIN_NAMESPACE
class QLabel;
class QComboBox;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QTimer;
QT_END_NAMESPACE
class TrendChart;
class WifiDialog;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(ISensorProvider *provider, QWidget *parent = nullptr);
private slots:
    void updateSnapshot(const SensorSnapshot &snapshot);
    void updateDeviceStatus(const QString &device, bool connected, const QString &detail);
    void toggleSampling();
    void showWifiDialog();
    void showThresholdDialog();
    void showAcquisitionDialog();
    void updateSamplingInterval(int index);
    void handleAcquisitionTimer();
    void updateEnabledDevices(QTableWidgetItem *item);
private:
    QWidget *makeMetricCard(const QString &title, const QString &accent,
                            QLabel **valueLabel, QLabel **unitLabel);
    void setAlert(const QString &message, bool active);
    void appendSeries(QVector<double> *series, double value);
    void loadSettings();
    void saveThresholds();
    void startAcquisition(const QDateTime &startTime, const QDateTime &endTime,
                          int deviceMask, const QString &filePath);
    void stopAcquisition(const QString &message);
    void scheduleAcquisitionTimer(const QDateTime &target);
    void recordSnapshot(const SensorSnapshot &snapshot);
    void startConfiguredSampling();
    ISensorProvider *provider_;
    QPushButton *samplingButton_;
    QPushButton *acquisitionButton_;
    QPushButton *thresholdButton_;
    QComboBox *samplingIntervalCombo_;
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
    WifiDialog *wifiDialog_;
    QVector<double> temperatureSeries_;
    QVector<double> humiditySeries_;
    QVector<double> pressureSeries_;
    QVector<double> illuminanceSeries_;
    double temperatureMinimum_;
    double temperatureMaximum_;
    double humidityMinimum_;
    double humidityMaximum_;
    double pressureMinimum_;
    double pressureMaximum_;
    double illuminanceMinimum_;
    double illuminanceMaximum_;
    QTimer *acquisitionTimer_;
    QFile acquisitionFile_;
    QDateTime acquisitionStartTime_;
    QDateTime acquisitionEndTime_;
    QString acquisitionFilePath_;
    int acquisitionDeviceMask_;
    bool acquisitionActive_;
    bool acquisitionScheduled_;
    bool samplingActive_;
    int enabledDeviceMask_;
};

#endif
