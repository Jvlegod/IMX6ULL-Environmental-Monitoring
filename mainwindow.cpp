#include "mainwindow.h"

#include "trendchart.h"
#include <QApplication>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QStatusBar>


namespace {
const int kMaxPoints = 60;
}

MainWindow::MainWindow(ISensorProvider *provider, QWidget *parent)
    : QMainWindow(parent), provider_(provider), samplingButton_(nullptr),
      lastUpdateLabel_(nullptr), alertLabel_(nullptr), temperatureValue_(nullptr),
      temperatureUnit_(nullptr), humidityValue_(nullptr), humidityUnit_(nullptr),
      pressureValue_(nullptr), pressureUnit_(nullptr), illuminanceValue_(nullptr),
      illuminanceUnit_(nullptr), statusTable_(nullptr), chartView_(nullptr)
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
    root->addLayout(header);

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
    temperatureValue_->setText(QString::number(snapshot.temperature, 'f', 1));
    temperatureUnit_->setText(QStringLiteral("°C"));
    humidityValue_->setText(QString::number(snapshot.humidity, 'f', 1));
    humidityUnit_->setText(QStringLiteral("% RH"));
    pressureValue_->setText(QString::number(snapshot.pressure, 'f', 2));
    pressureUnit_->setText(QStringLiteral("kPa"));
    illuminanceValue_->setText(QString::number(snapshot.illuminance, 'f', 0));
    illuminanceUnit_->setText(QStringLiteral("lux"));
    lastUpdateLabel_->setText(snapshot.timestamp.toString(QStringLiteral("HH:mm:ss")));

    appendSeries(&temperatureSeries_, snapshot.temperature);
    appendSeries(&humiditySeries_, snapshot.humidity);
    appendSeries(&pressureSeries_, snapshot.pressure);
    appendSeries(&illuminanceSeries_, snapshot.illuminance);

    chartView_->setSeries(temperatureSeries_, humiditySeries_, pressureSeries_, illuminanceSeries_);

    QStringList alerts;
    if (snapshot.temperature < 10.0 || snapshot.temperature > 35.0)
        alerts << QStringLiteral("温度超限");
    if (snapshot.humidity < 20.0 || snapshot.humidity > 80.0)
        alerts << QStringLiteral("湿度超限");
    if (snapshot.pressure < 95.0 || snapshot.pressure > 106.0)
        alerts << QStringLiteral("气压超限");
    setAlert(alerts.isEmpty() ? QStringLiteral("状态正常 · 当前未发现超限数据")
                             : QStringLiteral("告警: ") + alerts.join(QStringLiteral(" / ")), !alerts.isEmpty());
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
