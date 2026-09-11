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
#include <QTimer>
#include <QWidgetAction>

#include <cstdlib>
#include <dlfcn.h>

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

QVariant readValue(const char* key, const QVariant& fallback) {
    QSettings s(QString::fromLatin1(kSettingsPath), QSettings::IniFormat);
    s.setIniCodec("UTF-8");
    s.sync();
    return s.value(QString::fromLatin1(key), fallback);
}

int readInt(const char* key, int fallback) {
    bool ok = false;
    const int value = readValue(key, fallback).toInt(&ok);
    return ok ? value : fallback;
}

bool readBool(const char* key, bool fallback) {
    const QVariant value = readValue(key, fallback);

    // Kobo Tweaks stores this setting as a QVariant bool, which QSettings may
    // serialize as "true"/"false". Using toInt() on "true" returns 0, so the
    // settings menu would incorrectly display 12 h while the clock was really
    // configured for 24 h. QVariant::toBool() handles bools, 0/1 and the
    // textual forms correctly.
    return value.toBool();
}

void writeInt(const char* key, int value) {
    QSettings s(QString::fromLatin1(kSettingsPath), QSettings::IniFormat);
    s.setIniCodec("UTF-8");
    s.setValue(QString::fromLatin1(key), value);
    s.sync();
}

void writeBool(const char* key, bool value) {
    QSettings s(QString::fromLatin1(kSettingsPath), QSettings::IniFormat);
    s.setIniCodec("UTF-8");
    s.setValue(QString::fromLatin1(key), value);
    s.sync();
}

QString readString(const char* key, const QString& fallback) {
    return readValue(key, fallback).toString();
}

void writeString(const char* key, const QString& value) {
    QSettings s(QString::fromLatin1(kSettingsPath), QSettings::IniFormat);
    s.setIniCodec("UTF-8");
    s.setValue(QString::fromLatin1(key), value);
    s.sync();
}

void showMainMenu();

void reopenMainMenu() {
    QTimer::singleShot(120, []() { showMainMenu(); });
}

void showMainMenu() {
    if (!resolveSymbols()) {
        nh_log("Kobo Tweaks settings menu: missing NickelTouchMenu/MenuTextItem symbols");
        return;
    }

    auto* menu = reinterpret_cast<NickelTouchMenu*>(calloc(1, 512));
    if (!menu) {
        return;
    }

    pNickelTouchMenuCtor(menu, nullptr, 3);

    const int height = readInt("Reading/HeaderFooterHeightScale", 100);
    addItem(menu, QStringLiteral("Altura cabecera/pie: %1 %").arg(height), [height]() {
        int next = height - 10;
        if (next < 50) next = 100;
        writeInt("Reading/HeaderFooterHeightScale", next);
        reopenMainMenu();
    });

    const int margins = readInt("Reading/HeaderFooterMargins", 50);
    addItem(menu, QStringLiteral("Márgenes laterales: %1").arg(margins), [margins]() {
        int next = margins + 10;
        if (next > 100) next = 0;
        writeInt("Reading/HeaderFooterMargins", next);
        reopenMainMenu();
    });

    const int spacing = readInt("Reading.Widget/Spacing", 10);
    addItem(menu, QStringLiteral("Espacio widgets: %1").arg(spacing), [spacing]() {
        int next = spacing + 2;
        if (next > 20) next = 0;
        writeInt("Reading.Widget/Spacing", next);
        reopenMainMenu();
    });

    const QString separator = readString("Reading.Widget/Separator", QStringLiteral("Dot"));
    const QString separatorLabel = separator.isEmpty() ? QStringLiteral("Ninguno") : separator;
    addItem(menu, QStringLiteral("Separador: %1").arg(separatorLabel), [separator]() {
        QString next;
        if (separator.isEmpty()) next = QStringLiteral("Bullet");
        else if (separator.compare(QStringLiteral("Bullet"), Qt::CaseInsensitive) == 0) next = QStringLiteral("Dot");
        else if (separator.compare(QStringLiteral("Dot"), Qt::CaseInsensitive) == 0) next = QStringLiteral("Pipe");
        else next = QString();
        writeString("Reading.Widget/Separator", next);
        reopenMainMenu();
    });

    const bool clock24 = readBool("Reading.Widget.Clock/24hFormat", true);
    addItem(menu, QStringLiteral("Reloj: %1 h").arg(clock24 ? 24 : 12), [clock24]() {
        writeBool("Reading.Widget.Clock/24hFormat", !clock24);
        reopenMainMenu();
    });

    addItem(menu, QStringLiteral("Cerrar"), []() {}, false);

    QObject::connect(menu, &QMenu::aboutToHide, menu, &QWidget::deleteLater);
    popupCentered(menu);
}

void consumeTrigger() {
    QFile trigger(QString::fromLatin1(kTriggerPath));
    if (!trigger.exists()) {
        return;
    }
    trigger.remove();

    // Let NickelMenu close its reader menu first. This menu uses Nickel's own
    // touch-aware MenuTextItem widgets, so it should receive gesture input.
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
