#include "ui/theme.h"
#include "omarchy/omarchycolors.h"
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>
#include <algorithm>
#include <cmath>

namespace pb {
namespace {
QColor mix(const QColor &a, const QColor &b, qreal t) {
  return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                          a.greenF() + (b.greenF() - a.greenF()) * t,
                          a.blueF() + (b.blueF() - a.blueF()) * t);
}
qreal luminance(const QColor &c) {
  auto channel = [](qreal v) {
    return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) +
         0.0722 * channel(c.blueF());
}
QColor colorValue(const QHash<QString, QString> &values, const char *key) {
  const QString v = values.value(QLatin1String(key));
  return v.isEmpty() ? QColor() : QColor::fromString(v);
}
} // namespace

qreal Theme::contrast(const QColor &a, const QColor &b) {
  const qreal la = luminance(a), lb = luminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor Theme::mutedText(const QColor &text, const QColor &background) {
  // Start 40% of the way towards the background and step back towards the
  // text colour until secondary text is comfortably readable.
  for (qreal t = 0.40; t > 0.0; t -= 0.02) {
    const QColor c = mix(text, background, t);
    if (contrast(c, background) >= 4.5)
      return c;
  }
  return text;
}

Theme::Theme(QObject *parent) : QObject(parent) {
  m_timer.setSingleShot(true);
  m_timer.setInterval(250); // theme switches write several files; settle first
  connect(&m_timer, &QTimer::timeout, this, &Theme::reload);
  connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_timer,
          qOverload<>(&QTimer::start));
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_timer,
          qOverload<>(&QTimer::start));
  connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
          [this] { reload(); });
  reload();
}

qreal Theme::fontSize() const {
  auto f = QGuiApplication::font();
  return f.pointSizeF() > 0 ? f.pointSizeF() : 10.0;
}

bool Theme::loadOmarchy() {
  if (omarchy::disabledByEnvironment())
    return false;
  const QString path = omarchy::activeColorsFile();
  QFile file(path);
  if (path.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
    return false;
  const auto values = omarchy::parseColorsToml(QString::fromUtf8(file.readAll()));
  const QColor bg = colorValue(values, "background");
  const QColor fg = colorValue(values, "foreground");
  if (!bg.isValid() || !fg.isValid())
    return false;
  m_background = bg;
  m_text = fg;
  const QColor accent = colorValue(values, "accent");
  if (accent.isValid())
    m_accent = accent;
  // Honour the theme's declared mode; infer it only when absent.
  const QString mode = values.value(QStringLiteral("mode")).toLower();
  m_dark = mode == QLatin1String("dark")    ? true
           : mode == QLatin1String("light") ? false
                                            : bg.lightnessF() < 0.5;
  m_input = mix(bg, m_dark ? fg : QColor(Qt::white), m_dark ? 0.07 : 0.6);
  return true;
}

void Theme::reload() {
  const QPalette palette = QGuiApplication::palette();
  m_background = palette.color(QPalette::Window);
  m_text = palette.color(QPalette::WindowText);
  m_input = palette.color(QPalette::Base);
  m_accent = palette.color(QPalette::Highlight);
  m_dark = m_background.lightnessF() < 0.5;
  loadOmarchy();
  m_surface = mix(m_background, m_text, 0.06);
  m_border = mix(m_background, m_text, 0.18);
  m_muted = mutedText(m_text, m_background);
  m_onAccent = contrast(m_accent, Qt::white) >= contrast(m_accent, Qt::black)
                   ? QColor(Qt::white)
                   : QColor(Qt::black);
  rewatch();
  emit changed();
}

void Theme::rewatch() {
  if (!m_watcher.files().isEmpty())
    m_watcher.removePaths(m_watcher.files());
  if (!m_watcher.directories().isEmpty())
    m_watcher.removePaths(m_watcher.directories());
  if (omarchy::disabledByEnvironment())
    return;
  // Watch the file and the directories above it: switching themes replaces
  // the theme directory, which drops file watches.
  QStringList watch;
  for (const QString &path : omarchy::colorsFileCandidates()) {
    const QString themeDir = QFileInfo(path).absolutePath();
    for (const QString &p :
         {path, themeDir, QFileInfo(themeDir).absolutePath()})
      if (QFileInfo::exists(p))
        watch << p;
  }
  if (!watch.isEmpty())
    m_watcher.addPaths(watch);
}
} // namespace pb
