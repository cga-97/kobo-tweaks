#include "../common.h"
#include "../hooks/reading_view.h"
#include "settings.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QMenu>
#include <QPoint>
#include <QScreen>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QWidgetAction>

#include <cstdlib>
#include <functional>

namespace {

constexpr const char* kTriggerPath = DATA_DIR "/open-settings";
constexpr const char* kSettingsPath = DATA_DIR "/settings.ini";
constexpr int kSubmenuDelayMs = 120;
constexpr int kNickelMenuCloseDelayMs = 300;
// /mnt/onboard disappears while Kobo USB mass storage is active. Retry once
// quickly after it returns, then back off so a genuinely unavailable storage
// volume cannot cause a continuous wakeup/IO loop.
constexpr int kWatcherRecoveryInitialDelayMs = 1000;
constexpr int kWatcherRecoveryMaxDelayMs = 30000;

typedef QWidget MenuTextItem;
typedef QMenu NickelTouchMenu;

QFileSystemWatcher* gWatcher = nullptr;
QTimer* gWatcherRecoveryTimer = nullptr;
bool gMissingSymbolsReported = false;
bool gRuntimeReloadPending = false;
bool gWatcherPathWasMissing = false;
int gWatcherRecoveryDelayMs = kWatcherRecoveryInitialDelayMs;

struct WidgetChoice {
    WidgetTypeEnum value;
    const char* label;
};

const WidgetChoice kWidgetChoices[] = {
    {WidgetTypeEnum::Clock,           "Reloj"},
    {WidgetTypeEnum::Battery,         "Batería"},
    {WidgetTypeEnum::ChapterTitle,    "Título capítulo"},
    {WidgetTypeEnum::ChapterPage,     "Página capítulo"},
    {WidgetTypeEnum::ChapterProgress, "Progreso capítulo"},
    {WidgetTypeEnum::ChapterTime,     "Tiempo capítulo"},
    {WidgetTypeEnum::BookTitle,       "Título libro"},
    {WidgetTypeEnum::BookPage,        "Página libro"},
    {WidgetTypeEnum::BookProgress,    "Progreso libro"},
    {WidgetTypeEnum::BookTime,        "Tiempo libro"},
};

const QString kHeaderLeftKey   = QString::fromLatin1(SettingsKeys::ReadingWidgetHeaderLeft);
const QString kHeaderCenterKey = QString::fromLatin1(SettingsKeys::ReadingWidgetHeaderCenter);
const QString kHeaderRightKey  = QString::fromLatin1(SettingsKeys::ReadingWidgetHeaderRight);
const QString kFooterLeftKey   = QString::fromLatin1(SettingsKeys::ReadingWidgetFooterLeft);
const QString kFooterCenterKey = QString::fromLatin1(SettingsKeys::ReadingWidgetFooterCenter);
const QString kFooterRightKey  = QString::fromLatin1(SettingsKeys::ReadingWidgetFooterRight);

const QList<QString> kAllZoneKeys = {
    kHeaderLeftKey, kHeaderCenterKey, kHeaderRightKey,
    kFooterLeftKey, kFooterCenterKey, kFooterRightKey,
};

bool nativeMenuSymbolsAvailable() {
    return NickelTouchMenu_constructor
        && MenuTextItem_constructor
        && MenuTextItem_setText
        && MenuTextItem_registerForTapGestures;
}

void reportMissingSymbols() {
    nh_log("Kobo Tweaks settings menu: missing NickelTouchMenu/MenuTextItem symbols");

    if (!gMissingSymbolsReported && ConfirmationDialogFactory_showOKDialog) {
        gMissingSymbolsReported = true;
        ConfirmationDialogFactory_showOKDialog(
            QStringLiteral("Kobo Tweaks"),
            QStringLiteral("El menú nativo de ajustes no es compatible con este firmware. Puedes seguir editando .adds/tweaks/settings.ini manualmente.")
        );
    }
}

NickelTouchMenu* createMenu() {
    if (!nativeMenuSymbolsAvailable()) {
        reportMissingSymbols();
        return nullptr;
    }

    // Match NickelMenu's conservative opaque allocations. These classes are
    // private Nickel ABI, so we intentionally allocate more than their known
    // object sizes instead of depending on a firmware-specific C++ layout.
    auto* menu = reinterpret_cast<NickelTouchMenu*>(calloc(1, 512));
    if (!menu) {
        return nullptr;
    }

    NickelTouchMenu_constructor(menu, nullptr, 3);
    QObject::connect(menu, &QMenu::aboutToHide, menu, &QWidget::deleteLater);
    return menu;
}

void popupCentered(NickelTouchMenu* menu) {
    menu->ensurePolished();
    const QSize hint = menu->sizeHint();
    const QRect screen = QApplication::primaryScreen() ? QApplication::primaryScreen()->geometry() : QRect(0, 0, 1080, 1440);
    const int x = qMax(0, (screen.width() - hint.width()) / 2);
    const int y = qMax(0, (screen.height() - hint.height()) / 2);
    menu->popup(QPoint(x, y));
}

void addItem(NickelTouchMenu* menu, const QString& label, const std::function<void()>& callback, bool separatorAfter = true) {
    auto* mti = reinterpret_cast<MenuTextItem*>(calloc(1, 256));
    if (!mti) {
        return;
    }

    MenuTextItem_constructor(mti, menu, false, true);
    MenuTextItem_setText(mti, label);
    MenuTextItem_registerForTapGestures(mti);

    auto* action = new QWidgetAction(menu);
    action->setDefaultWidget(mti);
    action->setEnabled(true);
    menu->addAction(action);

    if (!QObject::connect(mti, SIGNAL(tapped(bool)), action, SIGNAL(triggered()))) {
        nh_log("Kobo Tweaks settings menu: could not connect MenuTextItem::tapped");
        action->setEnabled(false);
    }
    QObject::connect(action, &QAction::triggered, menu, &QMenu::hide);
    QObject::connect(action, &QAction::triggered, [callback](bool) { callback(); });

    if (separatorAfter) {
        menu->addSeparator();
    }
}

QVariant readValue(const QString& key, const QVariant& fallback) {
    QSettings s(QString::fromLatin1(kSettingsPath), QSettings::IniFormat);
    s.setIniCodec("UTF-8");
    s.sync();
    return s.value(key, fallback);
}

int readInt(const QString& key, int fallback) {
    bool ok = false;
    const int value = readValue(key, fallback).toInt(&ok);
    return ok ? value : fallback;
}

bool readBool(const QString& key, bool fallback) {
    const QVariant value = readValue(key, fallback);
    if (value.type() == QVariant::String) {
        const QString normalized = value.toString().trimmed().toLower();
        if (normalized == QStringLiteral("on")) return true;
        if (normalized == QStringLiteral("off")) return false;
    }
    return value.toBool();
}

QString readString(const QString& key, const QString& fallback) {
    return readValue(key, fallback).toString();
}

QStringList readStringList(const QString& key) {
    const QVariant value = readValue(key, QString());
    QStringList list = value.toStringList();
    if (list.isEmpty()) {
        const QString single = value.toString().trimmed();
        if (!single.isEmpty()) {
            list << single;
        }
    }
    return list;
}

void scheduleRuntimeReload() {
    // Run after the current Nickel menu action returns. This keeps menu gesture
    // handling isolated from the reader widget rebuild while still making the
    // change visible before the submenu is reopened.
    if (gRuntimeReloadPending) {
        return;
    }
    gRuntimeReloadPending = true;
    QTimer::singleShot(0, []() {
        gRuntimeReloadPending = false;
        ReadingViewHook::reloadWidgets();
    });
}

bool writeValue(const QString& key, const QVariant& value) {
    QSettings s(QString::fromLatin1(kSettingsPath), QSettings::IniFormat);
    s.setIniCodec("UTF-8");
    s.setValue(key, value);
    s.sync();
    if (s.status() != QSettings::NoError) {
        nh_log("Kobo Tweaks settings menu: failed to persist '%s' (QSettings status %d)",
               key.toUtf8().constData(), static_cast<int>(s.status()));
        return false;
    }
    scheduleRuntimeReload();
    return true;
}

bool writeInt(const QString& key, int value) {
    return writeValue(key, value);
}

bool writeBool(const QString& key, bool value) {
    return writeValue(key, value);
}

bool writeString(const QString& key, const QString& value) {
    return writeValue(key, value);
}

bool writeStringList(const QString& key, const QStringList& values) {
    // Kobo Tweaks deliberately stores an empty zone as an empty string instead
    // of an empty QStringList, which QSettings may serialize as @Invalid().
    return writeValue(key, values.isEmpty() ? QVariant(QString()) : QVariant(values));
}

bool containsIgnoreCase(const QStringList& values, const QString& needle) {
    for (const QString& value : values) {
        if (value.compare(needle, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

void removeIgnoreCase(QStringList& values, const QString& needle) {
    for (int i = values.size() - 1; i >= 0; --i) {
        if (values.at(i).compare(needle, Qt::CaseInsensitive) == 0) {
            values.removeAt(i);
        }
    }
}

QString widgetLabel(const QString& value) {
    const WidgetTypeEnum widget = WidgetTypeSetting::fromString(value, WidgetTypeEnum::Invalid);
    for (const auto& choice : kWidgetChoices) {
        if (choice.value == widget) {
            return QString::fromUtf8(choice.label);
        }
    }
    return value;
}

QString zoneSummary(const QString& key) {
    const QStringList values = readStringList(key);
    if (values.isEmpty()) {
        return QStringLiteral("Vacía");
    }

    QStringList labels;
    for (const QString& value : values) {
        labels << widgetLabel(value);
    }
    return labels.join(QStringLiteral(" · "));
}

bool toggleWidgetInZone(const QString& targetKey, const QString& widget) {
    QSettings s(QString::fromLatin1(kSettingsPath), QSettings::IniFormat);
    s.setIniCodec("UTF-8");
    s.sync();

    auto readList = [&s](const QString& key) {
        const QVariant value = s.value(key, QString());
        QStringList list = value.toStringList();
        if (list.isEmpty()) {
            const QString single = value.toString().trimmed();
            if (!single.isEmpty()) {
                list << single;
            }
        }
        return list;
    };

    const bool wasSelected = containsIgnoreCase(readList(targetKey), widget);

    for (const QString& key : kAllZoneKeys) {
        QStringList values = readList(key);
        removeIgnoreCase(values, widget);
        s.setValue(key, values.isEmpty() ? QVariant(QString()) : QVariant(values));
    }

    if (!wasSelected) {
        QStringList target = readList(targetKey);
        target << widget;
        s.setValue(targetKey, target);
    }

    s.sync();
    if (s.status() != QSettings::NoError) {
        nh_log("Kobo Tweaks settings menu: failed to persist widget zones (QSettings status %d)",
               static_cast<int>(s.status()));
        return false;
    }
    scheduleRuntimeReload();
    return true;
}

BatteryStyleEnum nextBatteryStyle(BatteryStyleEnum current) {
    switch (current) {
        case BatteryStyleEnum::IconLevel: return BatteryStyleEnum::LevelIcon;
        case BatteryStyleEnum::LevelIcon: return BatteryStyleEnum::Icon;
        case BatteryStyleEnum::Icon:      return BatteryStyleEnum::Level;
        case BatteryStyleEnum::Level:     return BatteryStyleEnum::IconLevel;
        default:                          return BatteryStyleEnum::IconLevel;
    }
}

QString batteryStyleLabel(BatteryStyleEnum value) {
    switch (value) {
        case BatteryStyleEnum::Icon:      return QStringLiteral("Solo icono");
        case BatteryStyleEnum::Level:     return QStringLiteral("Solo porcentaje");
        case BatteryStyleEnum::LevelIcon: return QStringLiteral("Porcentaje + icono");
        case BatteryStyleEnum::IconLevel: return QStringLiteral("Icono + porcentaje");
        default:                          return QStringLiteral("Icono + porcentaje");
    }
}

QString separatorLabel(WidgetSeparatorEnum value) {
    switch (value) {
        case WidgetSeparatorEnum::Bullet: return QStringLiteral("Viñeta •");
        case WidgetSeparatorEnum::Dot:    return QStringLiteral("Punto ·");
        case WidgetSeparatorEnum::Pipe:   return QStringLiteral("Barra |");
        default:                          return QStringLiteral("Ninguno");
    }
}

WidgetSeparatorEnum nextSeparator(WidgetSeparatorEnum current) {
    switch (current) {
        case WidgetSeparatorEnum::Invalid: return WidgetSeparatorEnum::Bullet;
        case WidgetSeparatorEnum::Bullet:  return WidgetSeparatorEnum::Dot;
        case WidgetSeparatorEnum::Dot:     return WidgetSeparatorEnum::Pipe;
        case WidgetSeparatorEnum::Pipe:    return WidgetSeparatorEnum::Invalid;
    }
    return WidgetSeparatorEnum::Invalid;
}

void showMainMenu();
void showDesignMenu();
void showBatteryMenu();
void showZonesMenu(bool header);
void showZoneEditor(const QString& key, const QString& title);

void scheduleMenu(const std::function<void()>& callback) {
    QTimer::singleShot(kSubmenuDelayMs, [callback]() { callback(); });
}

void showDesignMenu() {
    NickelTouchMenu* menu = createMenu();
    if (!menu) return;

    const QString heightKey = QString::fromLatin1(SettingsKeys::ReadingHeaderFooterHeightScale);
    const int height = readInt(heightKey, 100);
    addItem(menu, QStringLiteral("Altura cabecera/pie: %1 %").arg(height), [height, heightKey]() {
        int next = height - 5;
        if (next < 50) next = 100;
        writeInt(heightKey, next);
        scheduleMenu(showDesignMenu);
    });

    const QString marginsKey = QString::fromLatin1(SettingsKeys::ReadingHeaderFooterMargins);
    const int margins = readInt(marginsKey, 50);
    addItem(menu, QStringLiteral("Márgenes laterales: %1").arg(margins), [margins, marginsKey]() {
        int next = margins + 5;
        if (next > 100) next = 0;
        writeInt(marginsKey, next);
        scheduleMenu(showDesignMenu);
    });

    const QString headerSpacerKey = QString::fromLatin1(SettingsKeys::ReadingHeaderSpacerHeight);
    const int headerSpacer = readInt(headerSpacerKey, 0);
    addItem(menu, QStringLiteral("Espacio superior: %1").arg(headerSpacer), [headerSpacer, headerSpacerKey]() {
        int next = headerSpacer + 5;
        if (next > 100) next = 0;
        writeInt(headerSpacerKey, next);
        scheduleMenu(showDesignMenu);
    });

    const QString footerSpacerKey = QString::fromLatin1(SettingsKeys::ReadingFooterSpacerHeight);
    const int footerSpacer = readInt(footerSpacerKey, 0);
    addItem(menu, QStringLiteral("Espacio inferior: %1").arg(footerSpacer), [footerSpacer, footerSpacerKey]() {
        int next = footerSpacer + 5;
        if (next > 100) next = 0;
        writeInt(footerSpacerKey, next);
        scheduleMenu(showDesignMenu);
    });

    const QString spacingKey = QString::fromLatin1(SettingsKeys::ReadingWidgetSpacing);
    const int spacing = readInt(spacingKey, 10);
    addItem(menu, QStringLiteral("Espacio widgets: %1").arg(spacing), [spacing, spacingKey]() {
        int next = spacing + 1;
        if (next > 20) next = 0;
        writeInt(spacingKey, next);
        scheduleMenu(showDesignMenu);
    });

    const QString separatorKey = QString::fromLatin1(SettingsKeys::ReadingWidgetSeparator);
    const WidgetSeparatorEnum separator = WidgetSeparatorSetting::fromString(
        readString(separatorKey, WidgetSeparatorSetting::toString(WidgetSeparatorEnum::Dot)),
        WidgetSeparatorEnum::Dot
    );
    addItem(menu, QStringLiteral("Separador: %1").arg(separatorLabel(separator)), [separator, separatorKey]() {
        writeString(separatorKey, WidgetSeparatorSetting::toString(nextSeparator(separator)));
        scheduleMenu(showDesignMenu);
    });

    addItem(menu, QStringLiteral("‹ Volver"), []() { scheduleMenu(showMainMenu); }, false);
    popupCentered(menu);
}

void showBatteryMenu() {
    NickelTouchMenu* menu = createMenu();
    if (!menu) return;

    const QString thresholdKey = QString::fromLatin1(SettingsKeys::ReadingWidgetBatteryShowWhenBelow);
    const int threshold = readInt(thresholdKey, 100);
    addItem(menu, QStringLiteral("Mostrar por debajo de: %1 %").arg(threshold), [threshold, thresholdKey]() {
        int next = threshold - 10;
        if (next < 10) next = 100;
        writeInt(thresholdKey, next);
        scheduleMenu(showBatteryMenu);
    });

    const QString styleKey = QString::fromLatin1(SettingsKeys::ReadingWidgetBatteryStyle);
    const BatteryStyleEnum style = BatteryStyleSetting::fromString(
        readString(styleKey, BatteryStyleSetting::toString(BatteryStyleEnum::IconLevel)),
        BatteryStyleEnum::IconLevel
    );
    addItem(menu, QStringLiteral("Estilo: %1").arg(batteryStyleLabel(style)), [style, styleKey]() {
        writeString(styleKey, BatteryStyleSetting::toString(nextBatteryStyle(style)));
        scheduleMenu(showBatteryMenu);
    });

    const QString chargingKey = QString::fromLatin1(SettingsKeys::ReadingWidgetBatteryStyleCharging);
    const BatteryStyleEnum charging = BatteryStyleSetting::fromString(
        readString(chargingKey, BatteryStyleSetting::toString(BatteryStyleEnum::IconLevel)),
        BatteryStyleEnum::IconLevel
    );
    addItem(menu, QStringLiteral("Cargando: %1").arg(batteryStyleLabel(charging)), [charging, chargingKey]() {
        writeString(chargingKey, BatteryStyleSetting::toString(nextBatteryStyle(charging)));
        scheduleMenu(showBatteryMenu);
    });

    addItem(menu, QStringLiteral("‹ Volver"), []() { scheduleMenu(showMainMenu); }, false);
    popupCentered(menu);
}

void showZoneEditor(const QString& key, const QString& title) {
    NickelTouchMenu* menu = createMenu();
    if (!menu) return;

    const QStringList selected = readStringList(key);
    for (const auto& choice : kWidgetChoices) {
        const QString value = WidgetTypeSetting::toString(choice.value);
        const bool enabled = containsIgnoreCase(selected, value);
        const QString label = QStringLiteral("%1 %2").arg(enabled ? QStringLiteral("[x]") : QStringLiteral("[ ]"), QString::fromUtf8(choice.label));
        addItem(menu, label, [key, title, value]() {
            toggleWidgetInZone(key, value);
            scheduleMenu([key, title]() { showZoneEditor(key, title); });
        });
    }

    addItem(menu, QStringLiteral("Vaciar zona"), [key, title]() {
        writeStringList(key, QStringList());
        scheduleMenu([key, title]() { showZoneEditor(key, title); });
    });

    addItem(menu, QStringLiteral("‹ Volver a %1").arg(title.startsWith(QStringLiteral("Cabecera")) ? QStringLiteral("cabecera") : QStringLiteral("pie")), [title]() {
        scheduleMenu([title]() { showZonesMenu(title.startsWith(QStringLiteral("Cabecera"))); });
    }, false);

    popupCentered(menu);
}

void showZonesMenu(bool header) {
    NickelTouchMenu* menu = createMenu();
    if (!menu) return;

    const QString side = header ? QStringLiteral("Cabecera") : QStringLiteral("Pie");
    const QString leftKey = header ? kHeaderLeftKey : kFooterLeftKey;
    const QString centerKey = header ? kHeaderCenterKey : kFooterCenterKey;
    const QString rightKey = header ? kHeaderRightKey : kFooterRightKey;

    addItem(menu, QStringLiteral("Izquierda: %1").arg(zoneSummary(leftKey)), [leftKey, side]() {
        scheduleMenu([leftKey, side]() { showZoneEditor(leftKey, side + QStringLiteral(" · Izquierda")); });
    });
    addItem(menu, QStringLiteral("Centro: %1").arg(zoneSummary(centerKey)), [centerKey, side]() {
        scheduleMenu([centerKey, side]() { showZoneEditor(centerKey, side + QStringLiteral(" · Centro")); });
    });
    addItem(menu, QStringLiteral("Derecha: %1").arg(zoneSummary(rightKey)), [rightKey, side]() {
        scheduleMenu([rightKey, side]() { showZoneEditor(rightKey, side + QStringLiteral(" · Derecha")); });
    });
    addItem(menu, QStringLiteral("‹ Volver"), []() { scheduleMenu(showMainMenu); }, false);

    popupCentered(menu);
}

void showMainMenu() {
    NickelTouchMenu* menu = createMenu();
    if (!menu) return;

    addItem(menu, QStringLiteral("Diseño ›"), []() { scheduleMenu(showDesignMenu); });
    addItem(menu, QStringLiteral("Batería ›"), []() { scheduleMenu(showBatteryMenu); });
    addItem(menu, QStringLiteral("Cabecera ›"), []() { scheduleMenu([]() { showZonesMenu(true); }); });
    addItem(menu, QStringLiteral("Pie ›"), []() { scheduleMenu([]() { showZonesMenu(false); }); });

    const QString clockKey = QString::fromLatin1(SettingsKeys::ReadingWidgetClock24hFormat);
    const bool clock24 = readBool(clockKey, true);
    addItem(menu, QStringLiteral("Reloj: %1 h").arg(clock24 ? 24 : 12), [clock24, clockKey]() {
        writeBool(clockKey, !clock24);
        scheduleMenu(showMainMenu);
    });

    addItem(menu, QStringLiteral("Cerrar"), []() {}, false);
    popupCentered(menu);
}

void consumeTrigger() {
    QFile trigger(QString::fromLatin1(kTriggerPath));
    if (!trigger.exists()) {
        return;
    }
    trigger.remove();

    // Let NickelMenu close its reader menu first. The settings UI itself is
    // composed entirely of NickelTouchMenu + MenuTextItem, the same touch-aware
    // widgets NickelMenu uses for its own menus. No browser, network or Wi-Fi is
    // involved.
    QTimer::singleShot(kNickelMenuCloseDelayMs, []() { showMainMenu(); });
}

void retryWatcherPath();

void scheduleWatcherRecovery() {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) {
        return;
    }

    if (!gWatcherRecoveryTimer) {
        gWatcherRecoveryTimer = new QTimer(app);
        gWatcherRecoveryTimer->setSingleShot(true);
        QObject::connect(gWatcherRecoveryTimer, &QTimer::timeout, app, []() {
            retryWatcherPath();
        });
    }

    if (gWatcherRecoveryTimer->isActive()) {
        return;
    }

    gWatcherRecoveryTimer->start(gWatcherRecoveryDelayMs);
    gWatcherRecoveryDelayMs = qMin(kWatcherRecoveryMaxDelayMs, gWatcherRecoveryDelayMs * 2);
}

void clearWatcherRecovery() {
    if (gWatcherRecoveryTimer) {
        gWatcherRecoveryTimer->stop();
    }
    if (gWatcherPathWasMissing) {
        nh_log("Kobo Tweaks settings menu: data directory watch restored");
    }
    gWatcherPathWasMissing = false;
    gWatcherRecoveryDelayMs = kWatcherRecoveryInitialDelayMs;
}

bool ensureWatcherPath() {
    if (!gWatcher) {
        return false;
    }

    const QString dataDir = QStringLiteral(DATA_DIR);
    const bool watched = gWatcher->directories().contains(dataDir);
    if (watched && QDir(dataDir).exists()) {
        clearWatcherRecovery();
        return true;
    }

    if (watched) {
        // QFSWatcher may still report a stale path while delivering the
        // removal notification. Best-effort removal makes the re-add below
        // independent of that event ordering.
        gWatcher->removePath(dataDir);
    }

    // QFileSystemWatcher stops monitoring a directory once it is removed.
    // This happens when /mnt/onboard is exported over USB, so add the path
    // again after the storage volume returns.
    if (!gWatcher->addPath(dataDir)) {
        if (!gWatcherPathWasMissing) {
            nh_log("Kobo Tweaks settings menu: data directory watch unavailable; retrying after storage returns");
            gWatcherPathWasMissing = true;
        }
        scheduleWatcherRecovery();
        return false;
    }

    clearWatcherRecovery();
    return true;
}

void retryWatcherPath() {
    if (ensureWatcherPath()) {
        // A trigger created immediately after USB disconnect may already be
        // present when the watch is restored.
        consumeTrigger();
    }
}

void installWatcher() {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) {
        return;
    }

    if (!gWatcher) {
        gWatcher = new QFileSystemWatcher(app);
        QObject::connect(gWatcher, &QFileSystemWatcher::directoryChanged, app, [](const QString&) {
            // If the directory was unmounted, avoid touching /mnt/onboard
            // until the watch has been restored successfully.
            retryWatcherPath();
        });
    }

    if (ensureWatcherPath()) {
        consumeTrigger();
        nh_log("Kobo Tweaks native settings menu watcher installed");
    }
}

struct NativeMenuBootstrap {
    NativeMenuBootstrap() {
        QTimer::singleShot(0, []() { installWatcher(); });
    }
};

NativeMenuBootstrap gNativeMenuBootstrap;

} // namespace
