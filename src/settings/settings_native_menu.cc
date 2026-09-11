#include "../common.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
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
#include <dlfcn.h>
#include <functional>

namespace {

constexpr const char* kTriggerPath = DATA_DIR "/open-settings";
constexpr const char* kSettingsPath = DATA_DIR "/settings.ini";

typedef QWidget MenuTextItem;
typedef QMenu NickelTouchMenu;
typedef int DecorationPosition;

using NickelTouchMenuCtor = void (*)(NickelTouchMenu*, QWidget*, DecorationPosition);
using MenuTextItemCtor = void (*)(MenuTextItem*, QWidget*, bool, bool);
using MenuTextItemSetText = void (*)(MenuTextItem*, const QString&);
using MenuTextItemRegisterForTapGestures = void (*)(MenuTextItem*);

NickelTouchMenuCtor pNickelTouchMenuCtor = nullptr;
MenuTextItemCtor pMenuTextItemCtor = nullptr;
MenuTextItemSetText pMenuTextItemSetText = nullptr;
MenuTextItemRegisterForTapGestures pMenuTextItemRegisterForTapGestures = nullptr;

QFileSystemWatcher* gWatcher = nullptr;

struct WidgetChoice {
    const char* value;
    const char* label;
};

const WidgetChoice kWidgetChoices[] = {
    {"Clock",           "Reloj"},
    {"Battery",         "Batería"},
    {"ChapterTitle",    "Título capítulo"},
    {"ChapterPage",     "Página capítulo"},
    {"ChapterProgress", "Progreso capítulo"},
    {"ChapterTime",     "Tiempo capítulo"},
    {"BookTitle",       "Título libro"},
    {"BookPage",        "Página libro"},
    {"BookProgress",    "Progreso libro"},
    {"BookTime",        "Tiempo libro"},
};

const QString kHeaderLeftKey   = QStringLiteral("Reading.Widget/HeaderLeft");
const QString kHeaderCenterKey = QStringLiteral("Reading.Widget/HeaderCenter");
const QString kHeaderRightKey  = QStringLiteral("Reading.Widget/HeaderRight");
const QString kFooterLeftKey   = QStringLiteral("Reading.Widget/FooterLeft");
const QString kFooterCenterKey = QStringLiteral("Reading.Widget/FooterCenter");
const QString kFooterRightKey  = QStringLiteral("Reading.Widget/FooterRight");

const QList<QString> kAllZoneKeys = {
    kHeaderLeftKey, kHeaderCenterKey, kHeaderRightKey,
    kFooterLeftKey, kFooterCenterKey, kFooterRightKey,
};

bool resolveSymbols() {
    if (pNickelTouchMenuCtor && pMenuTextItemCtor && pMenuTextItemSetText && pMenuTextItemRegisterForTapGestures) {
        return true;
    }

    reinterpret_cast<void*&>(pNickelTouchMenuCtor) = dlsym(RTLD_DEFAULT, "_ZN15NickelTouchMenuC2EP7QWidget18DecorationPosition");
    reinterpret_cast<void*&>(pMenuTextItemCtor) = dlsym(RTLD_DEFAULT, "_ZN12MenuTextItemC1EP7QWidgetbb");
    reinterpret_cast<void*&>(pMenuTextItemSetText) = dlsym(RTLD_DEFAULT, "_ZN12MenuTextItem7setTextERK7QString");
    reinterpret_cast<void*&>(pMenuTextItemRegisterForTapGestures) = dlsym(RTLD_DEFAULT, "_ZN12MenuTextItem22registerForTapGesturesEv");

    return pNickelTouchMenuCtor && pMenuTextItemCtor && pMenuTextItemSetText && pMenuTextItemRegisterForTapGestures;
}

NickelTouchMenu* createMenu() {
    if (!resolveSymbols()) {
        nh_log("Kobo Tweaks settings menu: missing NickelTouchMenu/MenuTextItem symbols");
        return nullptr;
    }

    auto* menu = reinterpret_cast<NickelTouchMenu*>(calloc(1, 512));
    if (!menu) {
        return nullptr;
    }

    pNickelTouchMenuCtor(menu, nullptr, 3);
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

    pMenuTextItemCtor(mti, menu, false, true);
    pMenuTextItemSetText(mti, label);
    pMenuTextItemRegisterForTapGestures(mti);

    auto* action = new QWidgetAction(menu);
    action->setDefaultWidget(mti);
    action->setEnabled(true);
    menu->addAction(action);

    QObject::connect(mti, SIGNAL(tapped(bool)), action, SIGNAL(triggered()));
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
    return readValue(key, fallback).toBool();
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

void writeValue(const QString& key, const QVariant& value) {
    QSettings s(QString::fromLatin1(kSettingsPath), QSettings::IniFormat);
    s.setIniCodec("UTF-8");
    s.setValue(key, value);
    s.sync();
}

void writeInt(const QString& key, int value) {
    writeValue(key, value);
}

void writeBool(const QString& key, bool value) {
    writeValue(key, value);
}

void writeString(const QString& key, const QString& value) {
    writeValue(key, value);
}

void writeStringList(const QString& key, const QStringList& values) {
    // Kobo Tweaks deliberately stores an empty zone as an empty string instead
    // of an empty QStringList, which QSettings may serialize as @Invalid().
    writeValue(key, values.isEmpty() ? QVariant(QString()) : QVariant(values));
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
    for (const auto& choice : kWidgetChoices) {
        if (value.compare(QString::fromLatin1(choice.value), Qt::CaseInsensitive) == 0) {
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

void toggleWidgetInZone(const QString& targetKey, const QString& widget) {
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
}

QString nextBatteryStyle(const QString& current) {
    if (current.compare(QStringLiteral("IconLevel"), Qt::CaseInsensitive) == 0) return QStringLiteral("LevelIcon");
    if (current.compare(QStringLiteral("LevelIcon"), Qt::CaseInsensitive) == 0) return QStringLiteral("Icon");
    if (current.compare(QStringLiteral("Icon"), Qt::CaseInsensitive) == 0) return QStringLiteral("Level");
    return QStringLiteral("IconLevel");
}

QString batteryStyleLabel(const QString& value) {
    if (value.compare(QStringLiteral("Icon"), Qt::CaseInsensitive) == 0) return QStringLiteral("Solo icono");
    if (value.compare(QStringLiteral("Level"), Qt::CaseInsensitive) == 0) return QStringLiteral("Solo porcentaje");
    if (value.compare(QStringLiteral("LevelIcon"), Qt::CaseInsensitive) == 0) return QStringLiteral("Porcentaje + icono");
    return QStringLiteral("Icono + porcentaje");
}

QString separatorLabel(const QString& value) {
    if (value.isEmpty()) return QStringLiteral("Ninguno");
    if (value.compare(QStringLiteral("Bullet"), Qt::CaseInsensitive) == 0) return QStringLiteral("Viñeta •");
    if (value.compare(QStringLiteral("Pipe"), Qt::CaseInsensitive) == 0) return QStringLiteral("Barra |");
    return QStringLiteral("Punto ·");
}

QString nextSeparator(const QString& current) {
    if (current.isEmpty()) return QStringLiteral("Bullet");
    if (current.compare(QStringLiteral("Bullet"), Qt::CaseInsensitive) == 0) return QStringLiteral("Dot");
    if (current.compare(QStringLiteral("Dot"), Qt::CaseInsensitive) == 0) return QStringLiteral("Pipe");
    return QString();
}

void showMainMenu();
void showDesignMenu();
void showBatteryMenu();
void showZonesMenu(bool header);
void showZoneEditor(const QString& key, const QString& title);

void scheduleMenu(const std::function<void()>& callback) {
    QTimer::singleShot(120, [callback]() { callback(); });
}

void showDesignMenu() {
    NickelTouchMenu* menu = createMenu();
    if (!menu) return;

    const int height = readInt(QStringLiteral("Reading/HeaderFooterHeightScale"), 100);
    addItem(menu, QStringLiteral("Altura cabecera/pie: %1 %").arg(height), [height]() {
        int next = height - 5;
        if (next < 50) next = 100;
        writeInt(QStringLiteral("Reading/HeaderFooterHeightScale"), next);
        scheduleMenu(showDesignMenu);
    });

    const int margins = readInt(QStringLiteral("Reading/HeaderFooterMargins"), 50);
    addItem(menu, QStringLiteral("Márgenes laterales: %1").arg(margins), [margins]() {
        int next = margins + 5;
        if (next > 100) next = 0;
        writeInt(QStringLiteral("Reading/HeaderFooterMargins"), next);
        scheduleMenu(showDesignMenu);
    });

    const int headerSpacer = readInt(QStringLiteral("Reading/HeaderSpacerHeight"), 0);
    addItem(menu, QStringLiteral("Espacio superior: %1").arg(headerSpacer), [headerSpacer]() {
        int next = headerSpacer + 5;
        if (next > 100) next = 0;
        writeInt(QStringLiteral("Reading/HeaderSpacerHeight"), next);
        scheduleMenu(showDesignMenu);
    });

    const int footerSpacer = readInt(QStringLiteral("Reading/FooterSpacerHeight"), 0);
    addItem(menu, QStringLiteral("Espacio inferior: %1").arg(footerSpacer), [footerSpacer]() {
        int next = footerSpacer + 5;
        if (next > 100) next = 0;
        writeInt(QStringLiteral("Reading/FooterSpacerHeight"), next);
        scheduleMenu(showDesignMenu);
    });

    const int spacing = readInt(QStringLiteral("Reading.Widget/Spacing"), 10);
    addItem(menu, QStringLiteral("Espacio widgets: %1").arg(spacing), [spacing]() {
        int next = spacing + 1;
        if (next > 20) next = 0;
        writeInt(QStringLiteral("Reading.Widget/Spacing"), next);
        scheduleMenu(showDesignMenu);
    });

    const QString separator = readString(QStringLiteral("Reading.Widget/Separator"), QStringLiteral("Dot"));
    addItem(menu, QStringLiteral("Separador: %1").arg(separatorLabel(separator)), [separator]() {
        writeString(QStringLiteral("Reading.Widget/Separator"), nextSeparator(separator));
        scheduleMenu(showDesignMenu);
    });

    addItem(menu, QStringLiteral("‹ Volver"), []() { scheduleMenu(showMainMenu); }, false);
    popupCentered(menu);
}

void showBatteryMenu() {
    NickelTouchMenu* menu = createMenu();
    if (!menu) return;

    const int threshold = readInt(QStringLiteral("Reading.Widget.Battery/ShowWhenBelow"), 100);
    addItem(menu, QStringLiteral("Mostrar por debajo de: %1 %").arg(threshold), [threshold]() {
        int next = threshold - 10;
        if (next < 10) next = 100;
        writeInt(QStringLiteral("Reading.Widget.Battery/ShowWhenBelow"), next);
        scheduleMenu(showBatteryMenu);
    });

    const QString style = readString(QStringLiteral("Reading.Widget.Battery/Style"), QStringLiteral("IconLevel"));
    addItem(menu, QStringLiteral("Estilo: %1").arg(batteryStyleLabel(style)), [style]() {
        writeString(QStringLiteral("Reading.Widget.Battery/Style"), nextBatteryStyle(style));
        scheduleMenu(showBatteryMenu);
    });

    const QString charging = readString(QStringLiteral("Reading.Widget.Battery/StyleCharging"), QStringLiteral("IconLevel"));
    addItem(menu, QStringLiteral("Cargando: %1").arg(batteryStyleLabel(charging)), [charging]() {
        writeString(QStringLiteral("Reading.Widget.Battery/StyleCharging"), nextBatteryStyle(charging));
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
        const QString value = QString::fromLatin1(choice.value);
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

    const bool clock24 = readBool(QStringLiteral("Reading.Widget.Clock/24hFormat"), true);
    addItem(menu, QStringLiteral("Reloj: %1 h").arg(clock24 ? 24 : 12), [clock24]() {
        writeBool(QStringLiteral("Reading.Widget.Clock/24hFormat"), !clock24);
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
    QTimer::singleShot(300, []() { showMainMenu(); });
}

void installWatcher() {
    if (gWatcher || !QCoreApplication::instance()) {
        return;
    }

    gWatcher = new QFileSystemWatcher(QCoreApplication::instance());
    gWatcher->addPath(QStringLiteral(DATA_DIR));
    QObject::connect(gWatcher, &QFileSystemWatcher::directoryChanged, QCoreApplication::instance(), [](const QString&) {
        consumeTrigger();
    });

    consumeTrigger();
    nh_log("Kobo Tweaks native settings menu watcher installed");
}

struct NativeMenuBootstrap {
    NativeMenuBootstrap() {
        QTimer::singleShot(0, []() { installWatcher(); });
    }
};

NativeMenuBootstrap gNativeMenuBootstrap;

} // namespace
