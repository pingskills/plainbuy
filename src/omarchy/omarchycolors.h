#pragma once
#include <QHash>
#include <QString>
#include <QStringList>

// Optional Omarchy integration: locating and parsing the active theme's
// colors.toml. Qt Core only, so it is tested without a display. When these
// files are absent PlainBuy uses the Qt/system palette.
namespace pb::omarchy {
// Most likely first:
//   $XDG_STATE_HOME/omarchy/current/theme/colors.toml   (Omarchy 4)
//   $XDG_CONFIG_HOME/omarchy/current/theme/colors.toml  (earlier releases)
QStringList colorsFileCandidates();
QString activeColorsFile();
// Flat `key = "value"` lines; comments, tables and malformed lines ignored.
QHash<QString, QString> parseColorsToml(const QString &text);
// PLAINBUY_NO_OMARCHY=1 ignores Omarchy even when present.
bool disabledByEnvironment();
} // namespace pb::omarchy
