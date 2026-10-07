#include "config.h"
#include "services/buyer.h"
#include "ui/theme.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQmlEngine>
#include <QQuickStyle>

Q_IMPORT_QML_PLUGIN(PlainBuyPlugin)
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("plainbuy"));
  QCoreApplication::setApplicationVersion(QStringLiteral(PLAINBUY_VERSION));
  QGuiApplication::setApplicationDisplayName(QStringLiteral("PlainBuy"));
  QGuiApplication::setDesktopFileName(QStringLiteral("io.github.pingskills.plainbuy"));
  if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
  pb::Buyer buyer;
  pb::Theme theme;
  qmlRegisterSingletonInstance("PlainBuy.Core", 1, 0, "Buyer", &buyer);
  qmlRegisterSingletonInstance("PlainBuy.Core", 1, 0, "Theme", &theme);
  QQmlApplicationEngine engine;
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                   [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule(QStringLiteral("PlainBuy"), QStringLiteral("Main"));
  return app.exec();
}
