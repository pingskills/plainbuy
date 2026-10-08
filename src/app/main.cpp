#include "config.h"
#include "services/buyer.h"
#include "ui/theme.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQmlEngine>
#include <QQuickStyle>
#include <cstdio>
#include <cstring>

Q_IMPORT_QML_PLUGIN(PlainBuyPlugin)
int main(int argc, char **argv) {
  // Answered before the GUI starts, so they work without a display.
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
      std::printf("plainbuy %s\n", PLAINBUY_VERSION);
      return 0;
    }
    if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      std::printf("Usage: plainbuy [options]\n"
                  "A small Bitcoin buy app for CoinSpot.\n\n"
                  "  -h, --help     Show this help and exit.\n"
                  "  -v, --version  Show the version and exit.\n\n"
                  "With no options, PlainBuy opens its window.\n");
      return 0;
    }
  }
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
