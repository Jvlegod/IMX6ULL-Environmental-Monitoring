#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMetaType>
#include <QAbstractButton>
#include <QElapsedTimer>
#include <QEvent>
#include <QHash>
#include <QObject>
#include <QSet>

class TouchDebounceFilter final : public QObject
{
public:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *button = qobject_cast<QAbstractButton *>(watched);
        if (!button
            || (event->type() != QEvent::MouseButtonPress
                && event->type() != QEvent::MouseButtonRelease))
            return QObject::eventFilter(watched, event);
        if (button->property("touchDebounce").isValid()
            && !button->property("touchDebounce").toBool())
            return QObject::eventFilter(watched, event);
        if (!timer_.isValid()) timer_.start();
        const qint64 now = timer_.elapsed();

        if (event->type() == QEvent::MouseButtonPress) {
            const qint64 last = lastRelease_.value(button, -1000);
            if (now - last < 300) {
                suppressed_.insert(button);
                event->accept();
                return true;
            }
            suppressed_.remove(button);
            return QObject::eventFilter(watched, event);
        }

        if (suppressed_.remove(button)) {
            lastRelease_.insert(button, now);
            event->accept();
            return true;
        }
        lastRelease_.insert(button, now);
        return QObject::eventFilter(watched, event);
    }

private:
    QElapsedTimer timer_;
    QHash<QAbstractButton *, qint64> lastRelease_;
    QSet<QAbstractButton *> suppressed_;
};

int main(int argc, char *argv[])
{
    if (!qEnvironmentVariableIsSet("XDG_RUNTIME_DIR")) {
        const QString runtimeDirectory = QStringLiteral("/tmp/runtime-environment-monitor");
        QDir().mkpath(runtimeDirectory);
        QFile::setPermissions(runtimeDirectory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("XDG_RUNTIME_DIR", runtimeDirectory.toLocal8Bit());
    }
    QApplication app(argc, argv);
    TouchDebounceFilter touchDebounce;
    app.installEventFilter(&touchDebounce);
    qRegisterMetaType<SensorSnapshot>("SensorSnapshot");
    app.setApplicationName(QStringLiteral("environment_monitor"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget { background: #f4f6f8; color: #27313a; }"
        "#title { font-size: 26px; font-weight: 700; color: #1f2d38; }"
        "#subtitle, #lastUpdate { color: #6f7d87; font-size: 13px; }"
        "#samplingButton { background: #247ba0; color: white; border: 0; border-radius: 5px; padding: 9px 16px; font-weight: 600; }"
        "#samplingButton:hover { background: #1d6785; }"
        "#metricCard, #panel { background: white; border: 1px solid #dce3e8; border-radius: 6px; }"
        "#metricTitle, #panelTitle { color: #60717c; font-size: 14px; font-weight: 600; }"
        "#metricNumber { color: #1f2d38; font-size: 32px; font-weight: 700; }"
        "#metricUnit { color: #87939b; font-size: 13px; }"
        "#alert { background: #e8f5ee; color: #168a5b; border-radius: 5px; padding: 10px 14px; font-weight: 600; }"
        "#alert[active=true] { background: #fff0ed; color: #b54747; }"
        "QTableWidget { border: 0; gridline-color: #edf0f2; background: white; }"
        "QHeaderView::section { background: #f7f9fa; color: #65737c; border: 0; padding: 8px; font-weight: 600; }"
    ));

    auto *provider = new SimulatedSensorProvider(&app);
    MainWindow window(provider);
    window.show();
    provider->start();
    return app.exec();
}
