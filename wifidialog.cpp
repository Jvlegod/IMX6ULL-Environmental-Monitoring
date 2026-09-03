#include "wifidialog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

WifiDialog::WifiDialog(QWidget *parent)
    : QDialog(parent), portCombo_(new QComboBox),
      scanButton_(new QPushButton(QStringLiteral("重新扫描 WiFi"))),
      ssidEdit_(new QLineEdit), passwordEdit_(new QLineEdit),
      connectButton_(new QPushButton(QStringLiteral("连接网络"))),
      networkTable_(new QTableWidget(0, 3)), statusLabel_(new QLabel(QStringLiteral("未连接 ESP8266")))
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

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(portLayout);
    layout->addWidget(statusLabel_);
    layout->addWidget(scanButton_, 0, Qt::AlignLeft);
    layout->addWidget(networkTable_, 1);
    layout->addLayout(networkForm);

    connect(scanButton_, &QPushButton::clicked, this, &WifiDialog::scanNetworks);
    connect(connectButton_, &QPushButton::clicked, this, &WifiDialog::connectNetwork);
    connect(networkTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { ssidEdit_->setText(networkTable_->item(row, 0)->text()); });
    connect(&controller_, &Esp8266Controller::scanFinished, this, &WifiDialog::showScanResults);
    connect(&controller_, &Esp8266Controller::portStateChanged, this, &WifiDialog::showPortState);
    connect(&controller_, &Esp8266Controller::connectionStateChanged, this, &WifiDialog::showConnectionState);
    connect(&controller_, &Esp8266Controller::operationFailed, this, &WifiDialog::showError);

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
    setBusy(false);
}

void WifiDialog::setBusy(bool busy)
{
    scanButton_->setEnabled(!busy && controller_.isOpen());
    connectButton_->setEnabled(!busy && controller_.isOpen());
}
