#include "mainwindow.h"

#include "appsettings.h"
#include "trendchart.h"
#include "wifidialog.h"
#include <QApplication>
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
#include <QProgressBar>
#include <QLineEdit>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
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
#include <QStyle>
#include <QIcon>


namespace {
const int kMaxPoints = 60;

int sensorFlagForDevice(const QString &device)
{
    if (device == QStringLiteral("BMP580 / I2C")) return SensorBmp580;
    if (device == QStringLiteral("RS485 温湿度计")) return SensorRs485;
    if (device == QStringLiteral("VEML7700 / I2C")) return SensorVeml7700;
    return 0;
}
}

MainWindow::MainWindow(ISensorProvider *provider, QWidget *parent)
    : QMainWindow(parent), provider_(provider), samplingButton_(nullptr),
      acquisitionButton_(nullptr), thresholdButton_(nullptr), samplingIntervalCombo_(nullptr),
      lastUpdateLabel_(nullptr), wifiStatusIcon_(nullptr), alertLabel_(nullptr), otaProgressBar_(nullptr), temperatureValue_(nullptr),
      temperatureUnit_(nullptr), humidityValue_(nullptr), humidityUnit_(nullptr),
      pressureValue_(nullptr), pressureUnit_(nullptr), illuminanceValue_(nullptr),
      illuminanceUnit_(nullptr), statusTable_(nullptr), chartView_(nullptr), wifiDialog_(nullptr),
      temperatureMinimum_(10.0), temperatureMaximum_(35.0),
      humidityMinimum_(20.0), humidityMaximum_(80.0),
      pressureMinimum_(95.0), pressureMaximum_(106.0),
      illuminanceMinimum_(0.0), illuminanceMaximum_(2000.0),
      acquisitionTimer_(new QTimer(this)), acquisitionDeviceMask_(SensorAll),
      acquisitionActive_(false), acquisitionScheduled_(false), samplingActive_(true),
      enabledDeviceMask_(SensorAll)
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
    wifiStatusIcon_ = new QLabel;
    QIcon wifiIcon = QIcon::fromTheme(QStringLiteral("network-wireless"));
    if (wifiIcon.isNull()) wifiIcon = style()->standardIcon(QStyle::SP_DriveNetIcon);
    wifiStatusIcon_->setPixmap(wifiIcon.pixmap(24, 24));
    wifiStatusIcon_->setEnabled(false);
    wifiStatusIcon_->setToolTip(QStringLiteral("WiFi 未连接"));
    header->addWidget(wifiStatusIcon_, 0, Qt::AlignVCenter);
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
    thresholdButton_ = new QPushButton(QStringLiteral("阈值设置"));
    samplingControls->addWidget(thresholdButton_);
    acquisitionButton_ = new QPushButton(QStringLiteral("采集时间"));
    connect(acquisitionButton_, &QPushButton::clicked, this, &MainWindow::showAcquisitionDialog);
    samplingControls->addWidget(acquisitionButton_);
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
    connect(statusTable_, &QTableWidget::itemChanged, this, &MainWindow::updateEnabledDevices);
    statusLayout->addWidget(statusTable_);
    content->addWidget(statusPanel, 2);
    root->addLayout(content, 1);

    alertLabel_ = new QLabel(QStringLiteral("状态正常 · 当前未发现超限数据"));
    alertLabel_->setObjectName(QStringLiteral("alert"));
    root->addWidget(alertLabel_);
    otaProgressBar_ = new QProgressBar;
    otaProgressBar_->setRange(0, 100);
    otaProgressBar_->setValue(0);
    otaProgressBar_->setFormat(QStringLiteral("OTA 未进行"));
    otaProgressBar_->setVisible(false);
    root->addWidget(otaProgressBar_);
    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("采集模式"));

    connect(provider_, &ISensorProvider::snapshotReady, this, &MainWindow::updateSnapshot);
    connect(provider_, &ISensorProvider::deviceStatusChanged, this, &MainWindow::updateDeviceStatus);
    connect(provider_, &ISensorProvider::providerError, this,
            [this](const QString &message) { setAlert(message, true); });
    connect(samplingIntervalCombo_, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &MainWindow::updateSamplingInterval);
    connect(thresholdButton_, &QPushButton::clicked, this, &MainWindow::showThresholdDialog);
    connect(acquisitionTimer_, &QTimer::timeout, this, &MainWindow::handleAcquisitionTimer);
    updateSamplingInterval(samplingIntervalCombo_->currentIndex());
    provider_->setEnabledDevices(enabledDeviceMask_);

    wifiDialog_ = new WifiDialog(this);
    wifiDialog_->hide();
    connect(provider_, &ISensorProvider::snapshotReady, wifiDialog_, &WifiDialog::publishTelemetry);
    connect(wifiDialog_->controller(), &Esp8266Controller::remoteSamplingInterval, this, [this](int seconds) {
        const int index = samplingIntervalCombo_->findData(seconds);
        if (index >= 0) samplingIntervalCombo_->setCurrentIndex(index);
    });
    connect(wifiDialog_->controller(), &Esp8266Controller::remoteThresholds, this,
            [this](double tmin, double tmax, double hmin, double hmax, double pmin, double pmax, double lmin, double lmax) {
        temperatureMinimum_ = tmin; temperatureMaximum_ = tmax; humidityMinimum_ = hmin; humidityMaximum_ = hmax;
        pressureMinimum_ = pmin; pressureMaximum_ = pmax; illuminanceMinimum_ = lmin; illuminanceMaximum_ = lmax;
        saveThresholds(); statusBar()->showMessage(QStringLiteral("网页阈值配置已同步"), 3000);
    });
    connect(wifiDialog_, &WifiDialog::wifiStateChanged, this, [this](bool connected, const QString &detail) {
        updateDeviceStatus(QStringLiteral("串口 WiFi"), connected, detail);
        wifiStatusIcon_->setEnabled(connected);
        wifiStatusIcon_->setToolTip(connected ? QStringLiteral("WiFi 已连接")
                                               : QStringLiteral("WiFi 未连接: %1").arg(detail));
    });
    connect(wifiDialog_, &WifiDialog::otaStatusChanged, this, [this](int progress, const QString &detail) {
        otaProgressBar_->setVisible(true);
        otaProgressBar_->setValue(progress);
        otaProgressBar_->setFormat(QStringLiteral("%1% · %2").arg(progress).arg(detail));
        if (progress >= 100) QTimer::singleShot(2500, otaProgressBar_, &QProgressBar::hide);
    });
}

void MainWindow::loadSettings()
{
    QSettings settings(environmentMonitorSettingsPath(), QSettings::IniFormat);
    const int intervalSeconds = settings.value(QStringLiteral("sampling/intervalSeconds"), 10).toInt();
    const int intervalIndex = samplingIntervalCombo_->findData(intervalSeconds);
    samplingIntervalCombo_->setCurrentIndex(intervalIndex >= 0 ? intervalIndex : 2);
    enabledDeviceMask_ = settings.value(QStringLiteral("acquisition/deviceMask"), SensorAll).toInt() & SensorAll;
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
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("采集设置"));
    dialog.setModal(true);
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    dialog.resize(qMin(560, screen.width() - 20), qMin(460, screen.height() - 20));

    auto *form = new QFormLayout;
    QSettings settings(environmentMonitorSettingsPath(), QSettings::IniFormat);
    QStringList enabledDevices;
    if (enabledDeviceMask_ & SensorBmp580) enabledDevices << QStringLiteral("BMP580");
    if (enabledDeviceMask_ & SensorRs485) enabledDevices << QStringLiteral("RS485");
    if (enabledDeviceMask_ & SensorVeml7700) enabledDevices << QStringLiteral("VEML7700");
    form->addRow(QStringLiteral("已使能设备"), new QLabel(enabledDevices.isEmpty()
                                                     ? QStringLiteral("无, 请在设备状态表勾选")
                                                     : enabledDevices.join(QStringLiteral(" + "))));

    auto *mode = new QComboBox;
    mode->addItem(QStringLiteral("手动开始和暂停"), 0);
    mode->addItem(QStringLiteral("采集指定时长"), 1);
    mode->addItem(QStringLiteral("指定开始和结束时间"), 2);
    mode->setCurrentIndex(settings.value(QStringLiteral("acquisition/mode"), 0).toInt());
    form->addRow(QStringLiteral("采集模式"), mode);

    auto *durationRow = new QWidget;
    auto *durationLayout = new QHBoxLayout(durationRow);
    durationLayout->setContentsMargins(0, 0, 0, 0);
    auto *duration = new QSpinBox;
    duration->setRange(1, 604800);
    duration->setValue(settings.value(QStringLiteral("acquisition/duration"), 60).toInt());
    auto *durationUnit = new QComboBox;
    durationUnit->addItem(QStringLiteral("秒"), 1);
    durationUnit->addItem(QStringLiteral("分钟"), 60);
    const int durationUnitIndex = durationUnit->findData(settings.value(QStringLiteral("acquisition/durationMultiplier"), 1).toInt());
    durationUnit->setCurrentIndex(durationUnitIndex >= 0 ? durationUnitIndex : 0);
    durationLayout->addWidget(duration, 1);
    durationLayout->addWidget(durationUnit);
    form->addRow(QStringLiteral("采集时长"), durationRow);

    const QDateTime now = QDateTime::currentDateTime();
    auto *startTime = new QDateTimeEdit(settings.value(QStringLiteral("acquisition/startTime"), now).toDateTime(), &dialog);
    auto *endTime = new QDateTimeEdit(settings.value(QStringLiteral("acquisition/endTime"), now.addSecs(60)).toDateTime(), &dialog);
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

    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (defaultPath.isEmpty()) defaultPath = QDir::tempPath();
    defaultPath += QStringLiteral("/acquisition.csv");
    auto *filePath = new QLineEdit(settings.value(QStringLiteral("acquisition/filePath"), defaultPath).toString());
    form->addRow(QStringLiteral("保存文件"), filePath);

    auto updateModeRows = [mode, durationRow, startRow, endRow](int index) {
        const bool duration = index == 1;
        const bool absolute = index == 2;
        durationRow->setVisible(duration);
        startRow->setVisible(absolute);
        endRow->setVisible(absolute);
    };
    connect(mode, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), updateModeRows);
    updateModeRows(mode->currentIndex());

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto *saveButton = buttons->addButton(QStringLiteral("保存设置"), QDialogButtonBox::AcceptRole);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(saveButton, &QPushButton::clicked, &dialog, [&dialog, mode,
                                                            startTime, endTime, filePath] {
        if (mode->currentIndex() == 2 && endTime->dateTime() <= startTime->dateTime()) {
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
    settings.setValue(QStringLiteral("acquisition/mode"), mode->currentIndex());
    settings.setValue(QStringLiteral("acquisition/duration"), duration->value());
    settings.setValue(QStringLiteral("acquisition/durationMultiplier"), durationUnit->currentData().toInt());
    settings.setValue(QStringLiteral("acquisition/startTime"), startTime->dateTime());
    settings.setValue(QStringLiteral("acquisition/endTime"), endTime->dateTime());
    settings.setValue(QStringLiteral("acquisition/filePath"), filePath->text().trimmed());
    settings.sync();
    statusBar()->showMessage(QStringLiteral("采集时间设置已保存, 点击开始采集后生效"), 5000);
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
    samplingButton_->setText(QStringLiteral("暂停采集"));
    samplingActive_ = false;
    provider_->stop();
    updateDeviceStatus(QStringLiteral("采集服务"), false, QStringLiteral("等待开始"));
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
            stream << "timestamp,bmp580_temperature,bmp580_pressure,rs485_humidity,veml7700_illuminance\n";
            stream.flush();
        }
        provider_->start();
        samplingActive_ = true;
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
    acquisitionDeviceMask_ = enabledDeviceMask_;
    provider_->stop();
    provider_->setEnabledDevices(enabledDeviceMask_);
    samplingButton_->setText(QStringLiteral("开始采集"));
    samplingActive_ = false;
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
    writeValue(snapshot.temperature);
    writeValue(snapshot.pressure);
    writeValue(snapshot.humidity);
    writeValue(snapshot.illuminance);
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
    if (!wifiDialog_) wifiDialog_ = new WifiDialog(this);
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
            valueLabel->setText(QStringLiteral("-"));
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
    const int monitoredDevices = enabledDeviceMask_;
    if ((monitoredDevices & SensorBmp580) && (!qIsFinite(snapshot.temperature) || snapshot.temperature < temperatureMinimum_ || snapshot.temperature > temperatureMaximum_))
        alerts << QStringLiteral("温度超限");
    if ((monitoredDevices & SensorRs485) && (!qIsFinite(snapshot.humidity) || snapshot.humidity < humidityMinimum_ || snapshot.humidity > humidityMaximum_))
        alerts << QStringLiteral("湿度超限");
    if ((monitoredDevices & SensorBmp580) && (!qIsFinite(snapshot.pressure) || snapshot.pressure < pressureMinimum_ || snapshot.pressure > pressureMaximum_))
        alerts << QStringLiteral("气压超限");
    if ((monitoredDevices & SensorVeml7700) && (!qIsFinite(snapshot.illuminance) || snapshot.illuminance < illuminanceMinimum_ || snapshot.illuminance > illuminanceMaximum_))
        alerts << QStringLiteral("光照超限");
    setAlert(alerts.isEmpty() ? QStringLiteral("状态正常 · 当前未发现超限数据")
                             : QStringLiteral("告警: ") + alerts.join(QStringLiteral(" / ")), !alerts.isEmpty());
    if (snapshot.collisionWarning)
        statusBar()->showMessage(QStringLiteral("碰撞预警: 检测到剧烈晃动"), 3000);
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
    const QSignalBlocker blocker(statusTable_);
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
        auto *deviceItem = new QTableWidgetItem(device);
        const int flag = sensorFlagForDevice(device);
        if (flag != 0) {
            deviceItem->setFlags(deviceItem->flags() | Qt::ItemIsUserCheckable);
            deviceItem->setCheckState(enabledDeviceMask_ & flag ? Qt::Checked : Qt::Unchecked);
        }
        statusTable_->setItem(row, 0, deviceItem);
        statusTable_->setItem(row, 1, new QTableWidgetItem);
        statusTable_->setItem(row, 2, new QTableWidgetItem);
    }
    auto *state = statusTable_->item(row, 1);
    if (detail == QStringLiteral("已禁用")) {
        state->setText(QStringLiteral("禁用"));
        state->setForeground(QColor(QStringLiteral("#87939b")));
    } else if (connected) {
        state->setText(QStringLiteral("在线"));
        state->setForeground(QColor(QStringLiteral("#168a5b")));
    } else if (detail == QStringLiteral("等待采集") || detail == QStringLiteral("等待开始")) {
        state->setText(QStringLiteral("待机"));
        state->setForeground(QColor(QStringLiteral("#b7791f")));
    } else {
        state->setText(QStringLiteral("离线"));
        state->setForeground(QColor(QStringLiteral("#b54747")));
    }
    statusTable_->item(row, 2)->setText(detail);
}

void MainWindow::updateEnabledDevices(QTableWidgetItem *item)
{
    if (!item || item->column() != 0) return;
    const int flag = sensorFlagForDevice(item->text());
    if (flag == 0) return;

    if (item->checkState() == Qt::Checked)
        enabledDeviceMask_ |= flag;
    else
        enabledDeviceMask_ &= ~flag;
    acquisitionDeviceMask_ = enabledDeviceMask_;
    provider_->setEnabledDevices(enabledDeviceMask_);

    QSettings settings(environmentMonitorSettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("acquisition/deviceMask"), enabledDeviceMask_);
    settings.sync();
}

void MainWindow::startConfiguredSampling()
{
    if (enabledDeviceMask_ == 0) {
        QMessageBox::warning(this, QStringLiteral("没有使能设备"), QStringLiteral("请先在设备状态表勾选至少一个传感器"));
        return;
    }

    QSettings settings(environmentMonitorSettingsPath(), QSettings::IniFormat);
    const int mode = settings.value(QStringLiteral("acquisition/mode"), 0).toInt();
    const QString filePath = settings.value(QStringLiteral("acquisition/filePath"),
                                             QStringLiteral("/tmp/environment_monitor/acquisition.csv")).toString();
    if (mode == 1) {
        const int duration = settings.value(QStringLiteral("acquisition/duration"), 60).toInt();
        const int multiplier = settings.value(QStringLiteral("acquisition/durationMultiplier"), 1).toInt();
        const QDateTime start = QDateTime::currentDateTime();
        startAcquisition(start, start.addSecs(duration * multiplier), enabledDeviceMask_, filePath);
        return;
    }
    if (mode == 2) {
        const QDateTime start = settings.value(QStringLiteral("acquisition/startTime")).toDateTime();
        const QDateTime end = settings.value(QStringLiteral("acquisition/endTime")).toDateTime();
        if (!start.isValid() || !end.isValid() || end <= start || end <= QDateTime::currentDateTime()) {
            QMessageBox::warning(this, QStringLiteral("采集时间无效"), QStringLiteral("请重新设置有效的开始和结束时间"));
            return;
        }
        startAcquisition(start, end, enabledDeviceMask_, filePath);
        return;
    }

    provider_->setEnabledDevices(enabledDeviceMask_);
    provider_->start();
    samplingActive_ = true;
    samplingButton_->setText(QStringLiteral("暂停采集"));
    statusBar()->showMessage(QStringLiteral("采集中"));
}

void MainWindow::toggleSampling()
{
    if (samplingActive_ || acquisitionScheduled_) {
        if (acquisitionActive_ || acquisitionScheduled_) {
            stopAcquisition(QStringLiteral("采集任务已停止"));
        } else {
            provider_->stop();
            samplingActive_ = false;
            samplingButton_->setText(QStringLiteral("开始采集"));
            statusBar()->showMessage(QStringLiteral("采集已暂停"));
        }
    } else {
        startConfiguredSampling();
    }
}

void MainWindow::setAlert(const QString &message, bool active)
{
    alertLabel_->setText(message);
    alertLabel_->setProperty("active", active);
    alertLabel_->style()->unpolish(alertLabel_);
    alertLabel_->style()->polish(alertLabel_);
}
