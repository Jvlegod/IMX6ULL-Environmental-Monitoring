#include "wifidialog.h"

#include <QCoreApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QProcess>
#include <QFileInfo>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

WifiDialog::WifiDialog(QWidget *parent)
    : QDialog(parent), portCombo_(new QComboBox),
      scanButton_(new QPushButton(QStringLiteral("重新扫描 WiFi"))),
      ssidEdit_(new QLineEdit), passwordEdit_(new QLineEdit),
      connectButton_(new QPushButton(QStringLiteral("连接网络"))),
      networkTable_(new QTableWidget(0, 3)), statusLabel_(new QLabel(QStringLiteral("未连接 ESP8266"))),
      otaHostEdit_(new QLineEdit(QStringLiteral("192.168.1.100"))),
      otaPortEdit_(new QLineEdit(QStringLiteral("8080"))),
      otaManifestEdit_(new QLineEdit(QStringLiteral("/manifest.json"))),
      otaButton_(new QPushButton(QStringLiteral("检查并升级应用"))), otaProgress_(new QProgressBar)
{
    setWindowTitle(QStringLiteral("ESP8266 WiFi 配置"));
    resize(620, 480);

    for (const QString &port : Esp8266Controller::availablePorts()) portCombo_->addItem(port, port);
    portCombo_->addItem(QStringLiteral("模拟模式"), QString());
    ssidEdit_->setPlaceholderText(QStringLiteral("WiFi 名称"));
    passwordEdit_->setPlaceholderText(QStringLiteral("WiFi 密码"));
    passwordEdit_->setEchoMode(QLineEdit::Password);

    auto *portLayout = new QHBoxLayout;
    portLayout->addWidget(new QLabel(QStringLiteral("串口")));
    portLayout->addWidget(portCombo_, 1);
    portLayout->addWidget(new QLabel(QStringLiteral("默认配置: 115200 8N1")));

    auto *networkForm = new QFormLayout;
    networkForm->addRow(QStringLiteral("SSID"), ssidEdit_);
    networkForm->addRow(QStringLiteral("密码"), passwordEdit_);
    networkForm->addRow(QString(), connectButton_);

    networkTable_->setHorizontalHeaderLabels({QStringLiteral("SSID"), QStringLiteral("信号"), QStringLiteral("加密")});
    networkTable_->horizontalHeader()->setStretchLastSection(true);
    networkTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    networkTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    networkTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto *otaForm = new QFormLayout;
    otaForm->addRow(QStringLiteral("OTA 服务器"), otaHostEdit_);
    otaForm->addRow(QStringLiteral("HTTP 端口"), otaPortEdit_);
    otaForm->addRow(QStringLiteral("Manifest 路径"), otaManifestEdit_);
    otaProgress_->setRange(0, 100);
    otaProgress_->setValue(0);
    otaProgress_->setTextVisible(true);
    otaForm->addRow(otaButton_, otaProgress_);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(portLayout);
    layout->addWidget(statusLabel_);
    layout->addWidget(scanButton_, 0, Qt::AlignLeft);
    layout->addWidget(networkTable_, 1);
    layout->addLayout(networkForm);
    layout->addLayout(otaForm);

    connect(scanButton_, &QPushButton::clicked, this, &WifiDialog::scanNetworks);
    connect(otaButton_, &QPushButton::clicked, this, &WifiDialog::startOta);
    connect(connectButton_, &QPushButton::clicked, this, &WifiDialog::connectNetwork);
    connect(networkTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { ssidEdit_->setText(networkTable_->item(row, 0)->text()); });
    connect(&controller_, &Esp8266Controller::scanFinished, this, &WifiDialog::showScanResults);
    connect(&controller_, &Esp8266Controller::portStateChanged, this, &WifiDialog::showPortState);
    connect(&controller_, &Esp8266Controller::connectionStateChanged, this, &WifiDialog::showConnectionState);
    connect(&controller_, &Esp8266Controller::operationFailed, this, &WifiDialog::showError);
    connect(&controller_, &Esp8266Controller::otaProgress, this, &WifiDialog::showOtaProgress);
    connect(&controller_, &Esp8266Controller::otaPackageReady, this, &WifiDialog::applyOtaPackage);

    QTimer::singleShot(0, this, [this] {
        controller_.openPort(portCombo_->currentData().toString(), 115200);
        QTimer::singleShot(100, this, &WifiDialog::scanNetworks);
    });
}

void WifiDialog::scanNetworks()
{
    controller_.scanNetworks();
    setBusy(true);
}

void WifiDialog::connectNetwork()
{
    if (ssidEdit_->text().isEmpty()) { showError(QStringLiteral("请输入 WiFi 名称")); return; }
    controller_.connectNetwork(ssidEdit_->text(), passwordEdit_->text());
    setBusy(true);
}

void WifiDialog::showScanResults(const QVector<WifiNetwork> &networks)
{
    networkTable_->setRowCount(0);
    for (const WifiNetwork &network : networks) {
        const int row = networkTable_->rowCount();
        networkTable_->insertRow(row);
        networkTable_->setItem(row, 0, new QTableWidgetItem(network.ssid));
        networkTable_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("%1 dBm").arg(network.rssi)));
        networkTable_->setItem(row, 2, new QTableWidgetItem(network.encryption == 0 ? QStringLiteral("开放") : QStringLiteral("加密")));
    }
    statusLabel_->setText(QStringLiteral("扫描完成, 找到 %1 个网络").arg(networks.size()));
    setBusy(false);
}

void WifiDialog::showPortState(bool open, const QString &detail)
{
    statusLabel_->setText(detail);
    if (!open) emit wifiStateChanged(false, detail);
    setBusy(false);
}

void WifiDialog::showConnectionState(bool connected, const QString &detail)
{
    const QString stateDetail = connected ? QStringLiteral("WiFi 已连接 · %1").arg(detail) : detail;
    statusLabel_->setText(stateDetail);
    emit wifiStateChanged(connected, stateDetail);
    setBusy(false);
}

void WifiDialog::showError(const QString &message)
{
    statusLabel_->setText(message);
    emit wifiStateChanged(false, message);
    otaButton_->setEnabled(true);
    setBusy(false);
}

void WifiDialog::startOta()
{
    bool ok = false;
    const quint16 port = otaPortEdit_->text().toUShort(&ok);
    if (!ok || port == 0) { showError(QStringLiteral("HTTP 端口无效")); return; }
    if (!controller_.isOpen()) { showError(QStringLiteral("请先连接 ESP8266 WiFi")); return; }
    otaButton_->setEnabled(false);
    otaProgress_->setValue(0);
    statusLabel_->setText(QStringLiteral("正在通过 WiFi 检查 OTA 更新..."));
    controller_.startOta(otaHostEdit_->text(), port, otaManifestEdit_->text());
}

void WifiDialog::showOtaProgress(qint64 received, qint64 total)
{
    const int percent = total > 0 ? static_cast<int>((received * 100) / total) : 0;
    otaProgress_->setValue(qBound(0, percent, 100));
    statusLabel_->setText(QStringLiteral("OTA 下载中: %1 / %2 字节").arg(received).arg(total));
}

void WifiDialog::applyOtaPackage(const QString &version, const QString &path)
{
    if (path != QStringLiteral("/tmp/environment_monitor.new")) { showError(QStringLiteral("OTA 临时文件路径异常")); return; }
    const QString script = QStringLiteral("/usr/bin/environment_monitor_ota_apply.sh");
    if (!QFileInfo::exists(script)) { showError(QStringLiteral("缺少 OTA 替换脚本: %1").arg(script)); return; }
    if (!QProcess::startDetached(QStringLiteral("/bin/sh"), QStringList() << script << path << QStringLiteral("/usr/bin/environment_monitor"))) {
        showError(QStringLiteral("无法启动 OTA 替换脚本")); return;
    }
    statusLabel_->setText(QStringLiteral("已校验版本 %1, 正在替换并重启应用").arg(version));
    otaButton_->setEnabled(false);
    QTimer::singleShot(500, qApp, &QCoreApplication::quit);
}

void WifiDialog::setBusy(bool busy)
{
    scanButton_->setEnabled(!busy && controller_.isOpen());
    connectButton_->setEnabled(!busy && controller_.isOpen());
    otaButton_->setEnabled(!busy && controller_.isOpen());
}
