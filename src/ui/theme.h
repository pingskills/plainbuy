#pragma once
#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

namespace pb {
// Colours for the QML interface: the active Omarchy theme when present (and
// followed live), otherwise the Qt/system palette.
class Theme : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool dark READ dark NOTIFY changed)
  Q_PROPERTY(QColor background READ background NOTIFY changed)
  Q_PROPERTY(QColor surface READ surface NOTIFY changed)
  Q_PROPERTY(QColor text READ text NOTIFY changed)
  Q_PROPERTY(QColor muted READ muted NOTIFY changed)
  Q_PROPERTY(QColor accent READ accent NOTIFY changed)
  Q_PROPERTY(QColor border READ border NOTIFY changed)
  Q_PROPERTY(QColor input READ input NOTIFY changed)
  Q_PROPERTY(QColor onAccent READ onAccent NOTIFY changed)
  Q_PROPERTY(qreal fontSize READ fontSize NOTIFY changed)
public:
  explicit Theme(QObject *parent = nullptr);
  bool dark() const { return m_dark; }
  QColor background() const { return m_background; }
  QColor surface() const { return m_surface; }
  QColor text() const { return m_text; }
  QColor muted() const { return m_muted; }
  QColor accent() const { return m_accent; }
  QColor border() const { return m_border; }
  QColor input() const { return m_input; }
  QColor onAccent() const { return m_onAccent; }
  qreal fontSize() const;
  Q_INVOKABLE void reload();

  // WCAG 2 contrast ratio between two colours (1–21).
  static qreal contrast(const QColor &a, const QColor &b);
  // Secondary text: as close to the background as possible while keeping
  // at least WCAG AA contrast (4.5:1) against it.
  static QColor mutedText(const QColor &text, const QColor &background);

signals:
  void changed();

private:
  bool loadOmarchy();
  void rewatch();
  bool m_dark = false;
  QFileSystemWatcher m_watcher;
  QTimer m_timer;
  QColor m_background, m_surface, m_text, m_muted, m_accent, m_border, m_input,
      m_onAccent;
};
} // namespace pb
