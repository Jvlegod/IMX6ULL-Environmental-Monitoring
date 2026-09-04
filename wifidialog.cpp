#include "wifidialog.h"

#include <QCoreApplication>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QProcess>
#include <QFileInfo>
#include <QSettings>
#include <QGuiApplication>
#include <QScreen>
#include <QResizeEvent>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QKeyEvent>
#include <QMouseEvent>

WifiDialog::WifiDialog(QWidget *parent)
    : QDialog(parent), portCombo_(new QComboBox),
      scanButton_(new QPushButton(QStringLiteral("重新扫描 WiFi"))),
      ssidEdit_(new QLineEdit), passwordEdit_(new QLineEdit),
      connectButton_(new QPushButton(QStringLiteral("连接网络"))),
      networkTable_(new QTableWidget(0, 3)), statusLabel_(new QLabel(QStringLiteral("未连接 ESP8266"))),
      otaHostEdit_(new QLineEdit(QStringLiteral("192.168.1.100"))),
      otaPortEdit_(new QLineEdit(QStringLiteral("8080"))),
      otaManifestEdit_(new QLineEdit(QStringLiteral("/manifest.json"))),
      otaButton_(new QPushButton(QStringLiteral("检查并升级应用"))), otaProgress_(new QProgressBar),
      keyboardPanel_(new QWidget(this)), keyboardEdit_(nullptr), closeAllowed_(false)
{
    setWindowTitle(QStringLiteral("ESP8266 WiFi 配置"));
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    resize(qMin(620, screen.width() - 20), qMin(460, screen.height() - 20));

    const QStringList ports = Esp8266Controller::availablePorts();
    for (const QString &port : ports) portCombo_->addItem(port, port);
    const int uart4Index = portCombo_->findData(QStringLiteral("/dev/ttymxc3"));
    if (uart4Index >= 0) portCombo_->setCurrentIndex(uart4Index);
    ssidEdit_->setPlaceholderText(QStringLiteral("WiFi 名称"));
    passwordEdit_->setPlaceholderText(QStringLiteral("WiFi 密码"));
    passwordEdit_->setEchoMode(QLineEdit::Password);

    auto *closeButton = new QPushButton(QStringLiteral("×"));
    closeButton->setFixedSize(34, 30);
    connect(closeButton, &QPushButton::clicked, this, [this] { if (closeAllowed_) reject(); });
    auto *titleLayout = new QHBoxLayout;
    titleLayout->addWidget(new QLabel(QStringLiteral("WiFi 配置")));
    titleLayout->addStretch();
    titleLayout->addWidget(closeButton);

    auto *portLayout = new QHBoxLayout;
    portLayout->addWidget(new QLabel(QStringLiteral("串口")));
    portLayout->addWidget(portCombo_, 1);
    portLayout->addWidget(new QLabel(QStringLiteral("默认配置: 115200 8N1")));

    auto *networkForm = new QFormLayout;
    networkForm->addRow(QStringLiteral("SSID"), ssidEdit_);
    auto *passwordLayout = new QHBoxLayout;
    passwordLayout->setContentsMargins(0, 0, 0, 0);
    passwordLayout->addWidget(passwordEdit_);
    auto *showPassword = new QPushButton(QStringLiteral("显示"));
    showPassword->setCheckable(true);
    showPassword->setFixedWidth(58);
    connect(showPassword, &QPushButton::toggled, this, [this](bool visible) {
        passwordEdit_->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
    });
    passwordLayout->addWidget(showPassword);
    networkForm->addRow(QStringLiteral("密码"), passwordLayout);
    networkForm->addRow(QString(), connectButton_);

    loadSettings();

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
    layout->addLayout(titleLayout);
    layout->addLayout(portLayout);
    layout->addWidget(statusLabel_);
    layout->addWidget(scanButton_, 0, Qt::AlignLeft);
    layout->addWidget(networkTable_, 1);
    layout->addLayout(networkForm);
    layout->addLayout(otaForm);

    keyboardPanel_->setVisible(false);
    auto *keyboardLayout = new QGridLayout(keyboardPanel_);
    auto *keyboardTitle = new QLabel(QStringLiteral("触摸键盘"));
    auto *collapseKeyboard = new QPushButton(QStringLiteral("收起键盘"));
    connect(collapseKeyboard, &QPushButton::clicked, this, &WifiDialog::hideKeyboard);
    keyboardLayout->addWidget(keyboardTitle, 0, 0, 1, 8);
    keyboardLayout->addWidget(collapseKeyboard, 0, 8, 1, 2);
    const QStringList keys = {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8"), QStringLiteral("9"), QStringLiteral("0"),
                              QStringLiteral("q"), QStringLiteral("w"), QStringLiteral("e"), QStringLiteral("r"), QStringLiteral("t"), QStringLiteral("y"), QStringLiteral("u"), QStringLiteral("i"), QStringLiteral("o"), QStringLiteral("p"),
                              QStringLiteral("a"), QStringLiteral("s"), QStringLiteral("d"), QStringLiteral("f"), QStringLiteral("g"), QStringLiteral("h"), QStringLiteral("j"), QStringLiteral("k"), QStringLiteral("l"),
                              QStringLiteral("z"), QStringLiteral("x"), QStringLiteral("c"), QStringLiteral("v"), QStringLiteral("b"), QStringLiteral("n"), QStringLiteral("m"), QStringLiteral("."), QStringLiteral("_"), QStringLiteral("-")};
    for (int i = 0; i < keys.size(); ++i) {
        auto *key = new QPushButton(keys.at(i));
        key->setMinimumHeight(27);
        connect(key, &QPushButton::clicked, this, [this, key] {
            if (keyboardEdit_) keyboardEdit_->insert(key->text());
        });
        keyboardLayout->addWidget(key, i / 10 + 1, i % 10);
    }
    auto *backspace = new QPushButton(QStringLiteral("退格"));
    auto *space = new QPushButton(QStringLiteral("空格"));
    auto *clear = new QPushButton(QStringLiteral("清空"));
    auto *done = new QPushButton(QStringLiteral("完成"));
    connect(backspace, &QPushButton::clicked, this, [this] {
        if (keyboardEdit_) keyboardEdit_->backspace();
    });
    connect(space, &QPushButton::clicked, this, [this] {
        if (keyboardEdit_) keyboardEdit_->insert(QStringLiteral(" "));
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        if (keyboardEdit_) keyboardEdit_->clear();
    });
    connect(done, &QPushButton::clicked, this, &WifiDialog::hideKeyboard);
    keyboardLayout->addWidget(backspace, 5, 0, 1, 3);
    keyboardLayout->addWidget(space, 5, 3, 1, 4);
    keyboardLayout->addWidget(clear, 5, 7, 1, 2);
    keyboardLayout->addWidget(done, 5, 9);

    ssidEdit_->installEventFilter(this);
    passwordEdit_->installEventFilter(this);
    otaHostEdit_->installEventFilter(this);
    otaPortEdit_->installEventFilter(this);
    otaManifestEdit_->installEventFilter(this);

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
        const QString port = portCombo_->currentData().toString();
        if (port.isEmpty()) {
            showError(QStringLiteral("未找到可用 UART, 请确认 UART4 对应的 /dev/ttymxc3 已启用"));
            return;
        }
        if (controller_.openPort(port, 115200))
            QTimer::singleShot(100, this, &WifiDialog::scanNetworks);
    });
}

WifiDialog::~WifiDialog()
{
    saveSettings();
}

void WifiDialog::loadSettings()
{
    QSettings settings(QStringLiteral("jvle"), QStringLiteral("environment_monitor"));
    const QString savedPort = settings.value(QStringLiteral("wifi/serialPort"), QStringLiteral("/dev/ttymxc3")).toString();
    const int portIndex = portCombo_->findData(savedPort);
    if (portIndex >= 0) portCombo_->setCurrentIndex(portIndex);
    ssidEdit_->setText(settings.value(QStringLiteral("wifi/ssid")).toString());
    passwordEdit_->setText(settings.value(QStringLiteral("wifi/password")).toString());
    otaHostEdit_->setText(settings.value(QStringLiteral("ota/host"), otaHostEdit_->text()).toString());
    otaPortEdit_->setText(settings.value(QStringLiteral("ota/port"), otaPortEdit_->text()).toString());
    otaManifestEdit_->setText(settings.value(QStringLiteral("ota/manifest"), otaManifestEdit_->text()).toString());
}

void WifiDialog::saveSettings() const
{
    QSettings settings(QStringLiteral("jvle"), QStringLiteral("environment_monitor"));
    settings.setValue(QStringLiteral("wifi/serialPort"), portCombo_->currentData().toString());
    settings.setValue(QStringLiteral("wifi/ssid"), ssidEdit_->text());
    settings.setValue(QStringLiteral("wifi/password"), passwordEdit_->text());
    settings.setValue(QStringLiteral("ota/host"), otaHostEdit_->text());
    settings.setValue(QStringLiteral("ota/port"), otaPortEdit_->text());
    settings.setValue(QStringLiteral("ota/manifest"), otaManifestEdit_->text());
    settings.sync();
}

void WifiDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    closeAllowed_ = false;
    QTimer::singleShot(500, this, [this] { closeAllowed_ = true; });
}


void WifiDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    if (keyboardPanel_->isVisible()) {
        const int panelHeight = qMin(205, height() - 16);
        keyboardPanel_->setGeometry(8, height() - panelHeight - 8, width() - 16, panelHeight);
        keyboardPanel_->raise();
    }
}

bool WifiDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        if (auto *edit = qobject_cast<QLineEdit *>(watched)) {
            showKeyboard(edit);
            return QDialog::eventFilter(watched, event);
        }
    }
    if (event->type() == QEvent::FocusIn) {
        if (auto *edit = qobject_cast<QLineEdit *>(watched)) {
            showKeyboard(edit);
            return QDialog::eventFilter(watched, event);
        }
    }
    return QDialog::eventFilter(watched, event);
}

void WifiDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (keyboardPanel_->isVisible()) hideKeyboard();
        else reject();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void WifiDialog::showKeyboard(QLineEdit *edit)
{
    keyboardEdit_ = edit;
    keyboardPanel_->setVisible(true);
    const int panelHeight = qMin(205, height() - 16);
    keyboardPanel_->setGeometry(8, height() - panelHeight - 8, width() - 16, panelHeight);
    keyboardPanel_->raise();
    edit->setFocus();
}

void WifiDialog::hideKeyboard()
{
    keyboardPanel_->setVisible(false);
    keyboardEdit_ = nullptr;
}

void WifiDialog::scanNetworks()
{
    controller_.scanNetworks();
    setBusy(true);
}

void WifiDialog::connectNetwork()
{
    if (ssidEdit_->text().isEmpty()) { showError(QStringLiteral("请输入 WiFi 名称")); return; }
    saveSettings();
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
    saveSettings();
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
