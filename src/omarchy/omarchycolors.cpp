#include "omarchy/omarchycolors.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace pb::omarchy {
namespace {
QString xdgDir(const char *variable, const QString &fallbackUnderHome) {
  const QString value = qEnvironmentVariable(variable);
  return !value.isEmpty() && QDir::isAbsolutePath(value)
             ? value
             : QDir::home().filePath(fallbackUnderHome);
}
} // namespace

QStringList colorsFileCandidates() {
  const QString rel = QStringLiteral("omarchy/current/theme/colors.toml");
  return {QDir(xdgDir("XDG_STATE_HOME", QStringLiteral(".local/state")))
              .filePath(rel),
          QDir(xdgDir("XDG_CONFIG_HOME", QStringLiteral(".config")))
              .filePath(rel)};
}

QString activeColorsFile() {
  for (const QString &path : colorsFileCandidates())
    if (QFileInfo(path).isFile())
      return path;
  return {};
}

QHash<QString, QString> parseColorsToml(const QString &text) {
  static const QRegularExpression line(QStringLiteral(
      R"re(^\s*([A-Za-z0-9_\-]+)\s*=\s*(?:"([^"]*)"|'([^']*)')\s*(?:#.*)?$)re"));
  QHash<QString, QString> values;
  for (const QString &raw : text.split(QLatin1Char('\n'))) {
    const auto m = line.match(raw);
    if (!m.hasMatch())
      continue;
    const QString value = m.captured(2).isNull() ? m.captured(3) : m.captured(2);
    values.insert(m.captured(1).toLower(), value.trimmed());
  }
  return values;
}

bool disabledByEnvironment() {
  const QString v = qEnvironmentVariable("PLAINBUY_NO_OMARCHY");
  return !v.isEmpty() && v != QLatin1String("0");
}
} // namespace pb::omarchy
