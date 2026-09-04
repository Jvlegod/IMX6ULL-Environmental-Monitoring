#include "mainwindow.h"

#include "appsettings.h"
#include "trendchart.h"
#include "wifidialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QLineEdit>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QTimer>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QtMath>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QStatusBar>


namespace {
const int kMaxPoints = 60;
}

MainWindow::MainWindow(ISensorProvider *provider, QWidget *parent)
    : QMainWindow(parent), provider_(provider), samplingButton_(nullptr),
      acquisitionButton_(nullptr), thresholdButton_(nullptr), samplingIntervalCombo_(nullptr),
      lastUpdateLabel_(nullptr), alertLabel_(nullptr), temperatureValue_(nullptr),
      temperatureUnit_(nullptr), humidityValue_(nullptr), humidityUnit_(nullptr),
      pressureValue_(nullptr), pressureUnit_(nullptr), illuminanceValue_(nullptr),
      illuminanceUnit_(nullptr), statusTable_(nullptr), chartView_(nullptr), wifiDialog_(nullptr),
      temperatureMinimum_(10.0), temperatureMaximum_(35.0),
      humidityMinimum_(20.0), humidityMaximum_(80.0),
      pressureMinimum_(95.0), pressureMaximum_(106.0),
      illuminanceMinimum_(0.0), illuminanceMaximum_(2000.0),
      acquisitionTimer_(new QTimer(this)), acquisitionDeviceMask_(SensorAll),
      acquisitionActive_(false), acquisitionScheduled_(false)
{
    setWindowTitle(QStringLiteral("IMX6ULL 环境监测系统"));
    resize(1100, 720);
    setMinimumSize(800, 560);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(24, 20, 24, 18);
    root->setSpacing(16);

    auto *header = new QHBoxLayout;
    auto *titleBox = new QVBoxLayout;
    auto *title = new QLabel(QStringLiteral("环境监测总览"));
    title->setObjectName(QStringLiteral("title"));
    auto *subtitle = new QLabel(QStringLiteral("IMX6ULL · 多传感器采集终端"));
    subtitle->setObjectName(QStringLiteral("subtitle"));
    titleBox->addWidget(title);
    titleBox->addWidget(subtitle);
    header->addLayout(titleBox);
    header->addStretch();
    lastUpdateLabel_ = new QLabel(QStringLiteral("等待数据"));
    lastUpdateLabel_->setObjectName(QStringLiteral("lastUpdate"));
    header->addWidget(lastUpdateLabel_, 0, Qt::AlignVCenter);
    samplingButton_ = new QPushButton(QStringLiteral("暂停采集"));
    samplingButton_->setObjectName(QStringLiteral("samplingButton"));
    samplingButton_->setMinimumWidth(120);
    connect(samplingButton_, &QPushButton::clicked, this, &MainWindow::toggleSampling);
    header->addWidget(samplingButton_);
    auto *wifiButton = new QPushButton(QStringLiteral("WiFi 配置"));
    wifiButton->setObjectName(QStringLiteral("samplingButton"));
    connect(wifiButton, &QPushButton::clicked, this, &MainWindow::showWifiDialog);
    header->addWidget(wifiButton);
    root->addLayout(header);

    auto *samplingControls = new QHBoxLayout;
    samplingControls->addWidget(new QLabel(QStringLiteral("采集周期")));
    samplingIntervalCombo_ = new QComboBox;
    samplingIntervalCombo_->addItem(QStringLiteral("1 秒"), 1);
    samplingIntervalCombo_->addItem(QStringLiteral("5 秒"), 5);
    samplingIntervalCombo_->addItem(QStringLiteral("10 秒"), 10);
    samplingIntervalCombo_->addItem(QStringLiteral("30 秒"), 30);
    samplingIntervalCombo_->addItem(QStringLiteral("1 分钟"), 60);
    samplingControls->addWidget(samplingIntervalCombo_);
    acquisitionButton_ = new QPushButton(QStringLiteral("采集任务"));
    samplingControls->addWidget(acquisitionButton_);
    thresholdButton_ = new QPushButton(QStringLiteral("阈值设置"));
    samplingControls->addWidget(thresholdButton_);
    samplingControls->addStretch();
    root->addLayout(samplingControls);
    loadSettings();

    auto *metrics = new QGridLayout;
    metrics->setSpacing(12);
    metrics->addWidget(makeMetricCard(QStringLiteral("环境温度"), QStringLiteral("#ef8354"),
                                      &temperatureValue_, &temperatureUnit_), 0, 0);
    metrics->addWidget(makeMetricCard(QStringLiteral("相对湿度"), QStringLiteral("#4ea5d9"),
                                      &humidityValue_, &humidityUnit_), 0, 1);
    metrics->addWidget(makeMetricCard(QStringLiteral("大气压力"), QStringLiteral("#6c8ead"),
                                      &pressureValue_, &pressureUnit_), 0, 2);
    metrics->addWidget(makeMetricCard(QStringLiteral("环境光照"), QStringLiteral("#e5b94c"),
                                      &illuminanceValue_, &illuminanceUnit_), 0, 3);
    for (int i = 0; i < 4; ++i)
        metrics->setColumnStretch(i, 1);
    root->addLayout(metrics);

    auto *content = new QHBoxLayout;
    content->setSpacing(14);
    auto *chartPanel = new QFrame;
    chartPanel->setObjectName(QStringLiteral("panel"));
    auto *chartLayout = new QVBoxLayout(chartPanel);
    auto *chartTitle = new QLabel(QStringLiteral("实时趋势"));
    chartTitle->setObjectName(QStringLiteral("panelTitle"));
    chartLayout->addWidget(chartTitle);

    chartView_ = new TrendChart;
    chartLayout->addWidget(chartView_);
    content->addWidget(chartPanel, 3);

    auto *statusPanel = new QFrame;
    statusPanel->setObjectName(QStringLiteral("panel"));
    auto *statusLayout = new QVBoxLayout(statusPanel);
    auto *statusTitle = new QLabel(QStringLiteral("设备状态"));
    statusTitle->setObjectName(QStringLiteral("panelTitle"));
    statusLayout->addWidget(statusTitle);
    statusTable_ = new QTableWidget(0, 3);
    statusTable_->setHorizontalHeaderLabels({QStringLiteral("设备"), QStringLiteral("状态"), QStringLiteral("说明")});
    statusTable_->horizontalHeader()->setStretchLastSection(true);
    statusTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    statusTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    statusTable_->verticalHeader()->setVisible(false);
    statusTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    statusTable_->setSelectionMode(QAbstractItemView::NoSelection);
    statusLayout->addWidget(statusTable_);
    content->addWidget(statusPanel, 2);
    root->addLayout(content, 1);

    alertLabel_ = new QLabel(QStringLiteral("状态正常 · 当前未发现超限数据"));
    alertLabel_->setObjectName(QStringLiteral("alert"));
    root->addWidget(alertLabel_);
    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("模拟采集模式"));

    connect(provider_, &ISensorProvider::snapshotReady, this, &MainWindow::updateSnapshot);
    connect(provider_, &ISensorProvider::deviceStatusChanged, this, &MainWindow::updateDeviceStatus);
    connect(provider_, &ISensorProvider::providerError, this,
            [this](const QString &message) { setAlert(message, true); });
    connect(samplingIntervalCombo_, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &MainWindow::updateSamplingInterval);
    connect(acquisitionButton_, &QPushButton::clicked, this, &MainWindow::showAcquisitionDialog);
    connect(thresholdButton_, &QPushButton::clicked, this, &MainWindow::showThresholdDialog);
    connect(acquisitionTimer_, &QTimer::timeout, this, &MainWindow::handleAcquisitionTimer);
    updateSamplingInterval(samplingIntervalCombo_->currentIndex());
}

void MainWindow::loadSettings()
{
    QSettings settings(environmentMonitorSettingsPath(), QSettings::IniFormat);
    const int intervalSeconds = settings.value(QStringLiteral("sampling/intervalSeconds"), 10).toInt();
    const int intervalIndex = samplingIntervalCombo_->findData(intervalSeconds);
    samplingIntervalCombo_->setCurrentIndex(intervalIndex >= 0 ? intervalIndex : 2);
    temperatureMinimum_ = settings.value(QStringLiteral("thresholds/temperatureMinimum"), temperatureMinimum_).toDouble();
    temperatureMaximum_ = settings.value(QStringLiteral("thresholds/temperatureMaximum"), temperatureMaximum_).toDouble();
    humidityMinimum_ = settings.value(QStringLiteral("thresholds/humidityMinimum"), humidityMinimum_).toDouble();
    humidityMaximum_ = settings.value(QStringLiteral("thresholds/humidityMaximum"), humidityMaximum_).toDouble();
    pressureMinimum_ = settings.value(QStringLiteral("thresholds/pressureMinimum"), pressureMinimum_).toDouble();
    pressureMaximum_ = settings.value(QStringLiteral("thresholds/pressureMaximum"), pressureMaximum_).toDouble();
    illuminanceMinimum_ = settings.value(QStringLiteral("thresholds/illuminanceMinimum"), illuminanceMinimum_).toDouble();
    illuminanceMaximum_ = settings.value(QStringLiteral("thresholds/illuminanceMaximum"), illuminanceMaximum_).toDouble();
}

void MainWindow::saveThresholds()
{
    QSettings settings(environmentMonitorSettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("thresholds/temperatureMinimum"), temperatureMinimum_);
    settings.setValue(QStringLiteral("thresholds/temperatureMaximum"), temperatureMaximum_);
    settings.setValue(QStringLiteral("thresholds/humidityMinimum"), humidityMinimum_);
    settings.setValue(QStringLiteral("thresholds/humidityMaximum"), humidityMaximum_);
    settings.setValue(QStringLiteral("thresholds/pressureMinimum"), pressureMinimum_);
    settings.setValue(QStringLiteral("thresholds/pressureMaximum"), pressureMaximum_);
    settings.setValue(QStringLiteral("thresholds/illuminanceMinimum"), illuminanceMinimum_);
    settings.setValue(QStringLiteral("thresholds/illuminanceMaximum"), illuminanceMaximum_);
    settings.sync();
}

void MainWindow::updateSamplingInterval(int index)
{
    if (index < 0) return;
    const int intervalSeconds = samplingIntervalCombo_->itemData(index).toInt();
    provider_->setSamplingInterval(intervalSeconds * 1000);
    QSettings settings(environmentMonitorSettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("sampling/intervalSeconds"), intervalSeconds);
    settings.sync();
}

void MainWindow::showAcquisitionDialog()
{
    if (acquisitionActive_ || acquisitionScheduled_) {
        stopAcquisition(QStringLiteral("采集任务已停止"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("采集任务"));
    dialog.setModal(true);
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    dialog.resize(qMin(560, screen.width() - 20), qMin(460, screen.height() - 20));

    auto *form = new QFormLayout;
    auto *devices = new QWidget;
    auto *deviceLayout = new QHBoxLayout(devices);
    deviceLayout->setContentsMargins(0, 0, 0, 0);
    auto *bmp280 = new QCheckBox(QStringLiteral("BMP280"));
    auto *rs485 = new QCheckBox(QStringLiteral("RS485 温湿度计"));
    auto *veml7700 = new QCheckBox(QStringLiteral("VEML7700"));
    bmp280->setChecked(true);
    rs485->setChecked(true);
    veml7700->setChecked(true);
    deviceLayout->addWidget(bmp280);
    deviceLayout->addWidget(rs485);
    deviceLayout->addWidget(veml7700);
    form->addRow(QStringLiteral("采集设备"), devices);

    auto *mode = new QComboBox;
    mode->addItem(QStringLiteral("采集指定时长"), 0);
    mode->addItem(QStringLiteral("指定开始和结束时间"), 1);
    form->addRow(QStringLiteral("采集模式"), mode);

    auto *durationRow = new QWidget;
    auto *durationLayout = new QHBoxLayout(durationRow);
    durationLayout->setContentsMargins(0, 0, 0, 0);
    auto *duration = new QSpinBox;
    duration->setRange(1, 604800);
    duration->setValue(60);
    auto *durationUnit = new QComboBox;
    durationUnit->addItem(QStringLiteral("秒"), 1);
    durationUnit->addItem(QStringLiteral("分钟"), 60);
    durationLayout->addWidget(duration, 1);
    durationLayout->addWidget(durationUnit);
    form->addRow(QStringLiteral("采集时长"), durationRow);

    const QDateTime now = QDateTime::currentDateTime();
    auto *startTime = new QDateTimeEdit(now, &dialog);
    auto *endTime = new QDateTimeEdit(now.addSecs(60), &dialog);
    for (auto *editor : {startTime, endTime}) {
        editor->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        editor->setCalendarPopup(true);
    }
    auto *startRow = new QWidget;
    auto *startLayout = new QHBoxLayout(startRow);
    startLayout->setContentsMargins(0, 0, 0, 0);
    startLayout->addWidget(startTime);
    form->addRow(QStringLiteral("开始时间"), startRow);
    auto *endRow = new QWidget;
    auto *endLayout = new QHBoxLayout(endRow);
    endLayout->setContentsMargins(0, 0, 0, 0);
    endLayout->addWidget(endTime);
    form->addRow(QStringLiteral("结束时间"), endRow);

    QSettings settings(environmentMonitorSettingsPath(), QSettings::IniFormat);
    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (defaultPath.isEmpty()) defaultPath = QDir::tempPath();
    defaultPath += QStringLiteral("/acquisition.csv");
    auto *filePath = new QLineEdit(settings.value(QStringLiteral("acquisition/filePath"), defaultPath).toString());
    form->addRow(QStringLiteral("保存文件"), filePath);

    auto updateModeRows = [mode, durationRow, startRow, endRow](int index) {
        const bool absolute = index == 1;
        durationRow->setVisible(!absolute);
        startRow->setVisible(absolute);
        endRow->setVisible(absolute);
    };
    connect(mode, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), updateModeRows);
    updateModeRows(mode->currentIndex());

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto *startButton = buttons->addButton(QStringLiteral("开始采集"), QDialogButtonBox::AcceptRole);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(startButton, &QPushButton::clicked, &dialog, [&dialog, bmp280, rs485, veml7700, mode,
                                                            duration, durationUnit, startTime, endTime, filePath] {
        if (!bmp280->isChecked() && !rs485->isChecked() && !veml7700->isChecked()) {
            QMessageBox::warning(&dialog, QStringLiteral("采集设备为空"), QStringLiteral("至少选择一个采集设备"));
            return;
        }
        if (mode->currentIndex() == 1 && endTime->dateTime() <= startTime->dateTime()) {
            QMessageBox::warning(&dialog, QStringLiteral("时间无效"), QStringLiteral("结束时间必须晚于开始时间"));
            return;
        }
        if (filePath->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("文件路径为空"), QStringLiteral("请输入保存文件路径"));
            return;
        }
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted) return;
    int deviceMask = 0;
    if (bmp280->isChecked()) deviceMask |= SensorBmp280;
    if (rs485->isChecked()) deviceMask |= SensorRs485;
    if (veml7700->isChecked()) deviceMask |= SensorVeml7700;
    const QDateTime start = mode->currentIndex() == 0 ? QDateTime::currentDateTime() : startTime->dateTime();
    const QDateTime end = mode->currentIndex() == 0
        ? start.addSecs(duration->value() * durationUnit->currentData().toInt())
        : endTime->dateTime();
    settings.setValue(QStringLiteral("acquisition/filePath"), filePath->text().trimmed());
    settings.sync();
    startAcquisition(start, end, deviceMask, filePath->text().trimmed());
}

void MainWindow::startAcquisition(const QDateTime &startTime, const QDateTime &endTime,
                                  int deviceMask, const QString &filePath)
{
    if (endTime <= startTime || deviceMask == 0) return;
    acquisitionTimer_->stop();
    acquisitionStartTime_ = startTime;
    acquisitionEndTime_ = endTime;
    acquisitionDeviceMask_ = deviceMask;
    acquisitionFilePath_ = filePath;
    acquisitionActive_ = false;
    acquisitionScheduled_ = true;
    acquisitionButton_->setText(QStringLiteral("停止采集"));
    samplingButton_->setEnabled(false);
    provider_->stop();
    statusBar()->showMessage(QStringLiteral("等待采集任务开始: %1").arg(startTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    scheduleAcquisitionTimer(startTime);
}

void MainWindow::scheduleAcquisitionTimer(const QDateTime &target)
{
    const qint64 milliseconds = QDateTime::currentDateTime().msecsTo(target);
    acquisitionTimer_->start(static_cast<int>(qBound<qint64>(1, milliseconds, 2147483647)));
}

void MainWindow::handleAcquisitionTimer()
{
    const QDateTime now = QDateTime::currentDateTime();
    if (acquisitionScheduled_) {
        if (now < acquisitionStartTime_) {
            scheduleAcquisitionTimer(acquisitionStartTime_);
            return;
        }
        acquisitionScheduled_ = false;
        acquisitionActive_ = true;
        provider_->setEnabledDevices(acquisitionDeviceMask_);
        acquisitionFile_.setFileName(acquisitionFilePath_);
        const QFileInfo fileInfo(acquisitionFilePath_);
        if (!fileInfo.absolutePath().isEmpty() && !QDir().mkpath(fileInfo.absolutePath())) {
            stopAcquisition(QStringLiteral("无法创建采集目录: %1").arg(fileInfo.absolutePath()));
            return;
        }
        const bool needsHeader = !acquisitionFile_.exists() || acquisitionFile_.size() == 0;
        if (!acquisitionFile_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            stopAcquisition(QStringLiteral("无法打开采集文件: %1").arg(acquisitionFilePath_));
            return;
        }
        if (needsHeader) {
            QTextStream stream(&acquisitionFile_);
            stream.setCodec("UTF-8");
            stream << "timestamp";
            if (acquisitionDeviceMask_ & SensorBmp280) stream << ",bmp280_temperature,bmp280_pressure";
            if (acquisitionDeviceMask_ & SensorRs485) stream << ",rs485_humidity";
            if (acquisitionDeviceMask_ & SensorVeml7700) stream << ",veml7700_illuminance";
            stream << '\n';
            stream.flush();
        }
        provider_->start();
        statusBar()->showMessage(QStringLiteral("正在采集, 保存到 %1").arg(acquisitionFilePath_));
        if (now >= acquisitionEndTime_) {
            stopAcquisition(QStringLiteral("采集任务已完成"));
            return;
        }
        scheduleAcquisitionTimer(acquisitionEndTime_);
        return;
    }
    if (acquisitionActive_ && now >= acquisitionEndTime_)
        stopAcquisition(QStringLiteral("采集任务已完成, 数据已保存"));
    else if (acquisitionActive_)
        scheduleAcquisitionTimer(acquisitionEndTime_);
}

void MainWindow::stopAcquisition(const QString &message)
{
    acquisitionTimer_->stop();
    if (acquisitionFile_.isOpen()) acquisitionFile_.close();
    acquisitionActive_ = false;
    acquisitionScheduled_ = false;
    acquisitionDeviceMask_ = SensorAll;
    provider_->stop();
    provider_->setEnabledDevices(SensorAll);
    acquisitionButton_->setText(QStringLiteral("采集任务"));
    samplingButton_->setEnabled(true);
    samplingButton_->setText(QStringLiteral("开始采集"));
    statusBar()->showMessage(message, 5000);
}

void MainWindow::recordSnapshot(const SensorSnapshot &snapshot)
{
    if (!acquisitionActive_ || !acquisitionFile_.isOpen()) return;
    QTextStream stream(&acquisitionFile_);
    stream.setCodec("UTF-8");
    stream << snapshot.timestamp.toString(Qt::ISODate);
    auto writeValue = [&stream](double value) {
        stream << ',';
        if (qIsFinite(value)) stream << QString::number(value, 'f', 3);
    };
    if (acquisitionDeviceMask_ & SensorBmp280) {
        writeValue(snapshot.temperature);
        writeValue(snapshot.pressure);
    }
    if (acquisitionDeviceMask_ & SensorRs485) writeValue(snapshot.humidity);
    if (acquisitionDeviceMask_ & SensorVeml7700) writeValue(snapshot.illuminance);
    stream << '\n';
    stream.flush();
}

void MainWindow::showThresholdDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("传感器异常阈值"));
    dialog.setModal(true);
    dialog.resize(qMin(520, width() - 24), 420);

    auto *form = new QFormLayout;
    auto addLimit = [form](const QString &label, double minimum, double maximum,
                           double lower, double upper, int decimals, double step) {
        auto *minimumBox = new QDoubleSpinBox;
        minimumBox->setRange(lower, upper);
        minimumBox->setDecimals(decimals);
        minimumBox->setSingleStep(step);
        minimumBox->setValue(minimum);
        auto *maximumBox = new QDoubleSpinBox;
        maximumBox->setRange(lower, upper);
        maximumBox->setDecimals(decimals);
        maximumBox->setSingleStep(step);
        maximumBox->setValue(maximum);
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(QStringLiteral("下限")));
        row->addWidget(minimumBox, 1);
        row->addWidget(new QLabel(QStringLiteral("上限")));
        row->addWidget(maximumBox, 1);
        form->addRow(new QLabel(label), row);
        return qMakePair(minimumBox, maximumBox);
    };

    const auto temperature = addLimit(QStringLiteral("温度"), temperatureMinimum_, temperatureMaximum_, -100.0, 100.0, 1, 0.1);
    const auto humidity = addLimit(QStringLiteral("湿度"), humidityMinimum_, humidityMaximum_, 0.0, 100.0, 1, 0.1);
    const auto pressure = addLimit(QStringLiteral("气压"), pressureMinimum_, pressureMaximum_, 0.0, 200.0, 2, 0.1);
    const auto illuminance = addLimit(QStringLiteral("光照"), illuminanceMinimum_, illuminanceMaximum_, 0.0, 100000.0, 0, 10.0);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;
    const QList<QPair<QDoubleSpinBox *, QDoubleSpinBox *>> limits{temperature, humidity, pressure, illuminance};
    for (const auto &limit : limits) {
        if (limit.first->value() > limit.second->value()) {
            QMessageBox::warning(this, QStringLiteral("阈值无效"), QStringLiteral("每个参数的下限不能大于上限"));
            return;
        }
    }
    temperatureMinimum_ = temperature.first->value();
    temperatureMaximum_ = temperature.second->value();
    humidityMinimum_ = humidity.first->value();
    humidityMaximum_ = humidity.second->value();
    pressureMinimum_ = pressure.first->value();
    pressureMaximum_ = pressure.second->value();
    illuminanceMinimum_ = illuminance.first->value();
    illuminanceMaximum_ = illuminance.second->value();
    saveThresholds();
    statusBar()->showMessage(QStringLiteral("异常阈值已更新"), 3000);
}

void MainWindow::showWifiDialog()
{
    if (!wifiDialog_) {
        wifiDialog_ = new WifiDialog(this);
        connect(wifiDialog_, &WifiDialog::wifiStateChanged, this, [this](bool connected, const QString &detail) {
            updateDeviceStatus(QStringLiteral("串口 WiFi"), connected, detail);
        });
    }
    wifiDialog_->show();
    wifiDialog_->raise();
    wifiDialog_->activateWindow();
}

QWidget *MainWindow::makeMetricCard(const QString &title, const QString &accent,
                                    QLabel **valueLabel, QLabel **unitLabel)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("metricCard"));
    card->setProperty("accent", accent);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    auto *label = new QLabel(title);
    label->setObjectName(QStringLiteral("metricTitle"));
    layout->addWidget(label);
    *valueLabel = new QLabel(QStringLiteral("--"));
    (*valueLabel)->setObjectName(QStringLiteral("metricNumber"));
    layout->addWidget(*valueLabel);
    *unitLabel = new QLabel;
    (*unitLabel)->setObjectName(QStringLiteral("metricUnit"));
    layout->addWidget(*unitLabel);
    return card;
}

void MainWindow::updateSnapshot(const SensorSnapshot &snapshot)
{
    const auto updateMetric = [](double value, QLabel *valueLabel, QLabel *unitLabel,
                                 QVector<double> *series, const QString &unit, int decimals) {
        if (!qIsFinite(value)) {
            valueLabel->setText(QStringLiteral("--"));
            unitLabel->setText(QStringLiteral("未采集"));
            return;
        }
        valueLabel->setText(QString::number(value, 'f', decimals));
        unitLabel->setText(unit);
        series->append(value);
        if (series->size() > kMaxPoints) series->remove(0);
    };
    updateMetric(snapshot.temperature, temperatureValue_, temperatureUnit_, &temperatureSeries_, QStringLiteral("°C"), 1);
    updateMetric(snapshot.humidity, humidityValue_, humidityUnit_, &humiditySeries_, QStringLiteral("% RH"), 1);
    updateMetric(snapshot.pressure, pressureValue_, pressureUnit_, &pressureSeries_, QStringLiteral("kPa"), 2);
    updateMetric(snapshot.illuminance, illuminanceValue_, illuminanceUnit_, &illuminanceSeries_, QStringLiteral("lux"), 0);
    lastUpdateLabel_->setText(snapshot.timestamp.toString(QStringLiteral("HH:mm:ss")));

    chartView_->setSeries(temperatureSeries_, humiditySeries_, pressureSeries_, illuminanceSeries_);

    QStringList alerts;
    const int monitoredDevices = acquisitionActive_ ? acquisitionDeviceMask_ : SensorAll;
    if ((monitoredDevices & SensorBmp280) && (!qIsFinite(snapshot.temperature) || snapshot.temperature < temperatureMinimum_ || snapshot.temperature > temperatureMaximum_))
        alerts << QStringLiteral("温度超限");
    if ((monitoredDevices & SensorRs485) && (!qIsFinite(snapshot.humidity) || snapshot.humidity < humidityMinimum_ || snapshot.humidity > humidityMaximum_))
        alerts << QStringLiteral("湿度超限");
    if ((monitoredDevices & SensorBmp280) && (!qIsFinite(snapshot.pressure) || snapshot.pressure < pressureMinimum_ || snapshot.pressure > pressureMaximum_))
        alerts << QStringLiteral("气压超限");
    if ((monitoredDevices & SensorVeml7700) && (!qIsFinite(snapshot.illuminance) || snapshot.illuminance < illuminanceMinimum_ || snapshot.illuminance > illuminanceMaximum_))
        alerts << QStringLiteral("光照超限");
    setAlert(alerts.isEmpty() ? QStringLiteral("状态正常 · 当前未发现超限数据")
                             : QStringLiteral("告警: ") + alerts.join(QStringLiteral(" / ")), !alerts.isEmpty());
    recordSnapshot(snapshot);
}

void MainWindow::appendSeries(QVector<double> *series, double value)
{
    series->append(value);
    if (series->size() > kMaxPoints)
        series->remove(0);
}

void MainWindow::updateDeviceStatus(const QString &device, bool connected, const QString &detail)
{
    int row = -1;
    for (int i = 0; i < statusTable_->rowCount(); ++i) {
        if (statusTable_->item(i, 0)->text() == device) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        row = statusTable_->rowCount();
        statusTable_->insertRow(row);
        statusTable_->setItem(row, 0, new QTableWidgetItem(device));
        statusTable_->setItem(row, 1, new QTableWidgetItem);
        statusTable_->setItem(row, 2, new QTableWidgetItem);
    }
    auto *state = statusTable_->item(row, 1);
    state->setText(connected ? QStringLiteral("在线") : QStringLiteral("离线"));
    state->setForeground(connected ? QColor(QStringLiteral("#168a5b"))
                                   : QColor(QStringLiteral("#b54747")));
    statusTable_->item(row, 2)->setText(detail);
}

void MainWindow::toggleSampling()
{
    if (samplingButton_->text() == QStringLiteral("暂停采集")) {
        if (acquisitionActive_ || acquisitionScheduled_)
            stopAcquisition(QStringLiteral("采集任务已停止"));
        provider_->stop();
        samplingButton_->setText(QStringLiteral("开始采集"));
        statusBar()->showMessage(QStringLiteral("采集已暂停"));
    } else {
        provider_->start();
        samplingButton_->setText(QStringLiteral("暂停采集"));
        statusBar()->showMessage(QStringLiteral("模拟采集模式"));
    }
}

void MainWindow::setAlert(const QString &message, bool active)
{
    alertLabel_->setText(message);
    alertLabel_->setProperty("active", active);
    alertLabel_->style()->unpolish(alertLabel_);
    alertLabel_->style()->polish(alertLabel_);
}
