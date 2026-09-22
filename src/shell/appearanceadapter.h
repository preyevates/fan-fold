#pragma once

#include <QMargins>
#include <QObject>
#include <QRect>
#include <QVariantMap>

class QQuickWindow;
class QWindow;

/**
 * The presentation layer's `appearanceStore` contract over AppearanceSettings.
 *
 * The settings panel depends on the adapter returning `{ok, settings, warning}` and on
 * every key being normalized and clamped here, not in QML. AppearanceSettings itself only
 * persists a flat QVariantMap, so schema, defaults, bounds and refusal semantics live in
 * this adapter and the engine is used purely for storage.
 */
class AppearanceAdapter final : public QObject
{
    Q_OBJECT

public:
    explicit AppearanceAdapter(class AppearanceSettings *settings, QObject *parent = nullptr);

    Q_INVOKABLE QVariantMap load();
    Q_INVOKABLE QVariantMap save(const QVariantMap &values);
    /** Normalize without persisting; drives the live settings preview. */
    Q_INVOKABLE QVariantMap preview(const QVariantMap &values) { return normalize(values); }
    Q_INVOKABLE QVariantMap reset() { return save(defaults()); }

    /** The global appearance defaults the settings panel resets to. */
    static QVariantMap defaults();
    /** Clamp known keys; drop unknown and colour keys. Appearance is colourless — colour
     *  is per note, so an appearance change can never repaint one note only. */
    static QVariantMap normalize(QVariantMap input);
    /** A family name safe to interpolate into a CSS font-family string literal. */
    static bool safeFamily(const QString &name);

private:
    AppearanceSettings *m_settings = nullptr;
};

/**
 * The screen rectangle this window actually lives on.
 *
 * QML's `Screen` attached property exposes the full screen only. A fan docked at the
 * right edge must stay inside the available work area instead, so panels and struts are
 * not painted over. QScreen::availableGeometry is the supported source, re-read whenever
 * the window changes screen or the compositor changes the work area.
 */
class ScreenGeometry final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QRect full READ full NOTIFY changed)
    Q_PROPERTY(QRect available READ available NOTIFY changed)

public:
    explicit ScreenGeometry(QObject *parent = nullptr);

    QRect full() const;
    QRect available() const;
    void setWindow(QWindow *window);

signals:
    void changed();

private:
    void retarget();

    QWindow *m_window = nullptr;
    QList<QMetaObject::Connection> m_screenLinks;
};

/**
 * KDE's global animation-speed setting, read from the file the desktop actually writes.
 *
 * `[KDE] AnimationDurationFactor` in kdeglobals is authoritative: Plasma's "Animation
 * speed" control writes it and 0 means animations are disabled. A standalone Qt host does
 * not load Kirigami's Plasma platform plugin, so reading the key directly is what makes
 * "globally disable animations" apply to this window too. The config directory is
 * watched, so the setting takes effect without a restart.
 */
class AnimationSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal factor READ factor NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed)

public:
    explicit AnimationSettings(QObject *parent = nullptr);

    qreal factor() const { return m_factor; }
    /** The file the current factor came from, or empty when the default 1.0 is in use. */
    QString source() const { return m_present ? m_path : QString(); }
    Q_INVOKABLE void reload();

signals:
    void changed();

private:
    QString m_path;
    qreal m_factor = 1.0;
    bool m_present = false;
    class QFileSystemWatcher *m_watcher = nullptr;
};
