#include "../common.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

namespace {

constexpr const char* kTriggerPath = DATA_DIR "/open-settings";

struct WidgetChoice {
    const char* value;
    const char* label;
};

const WidgetChoice kWidgetChoices[] = {
    {"Clock",           "Reloj"},
    {"Battery",         "Batería"},
    {"ChapterTitle",    "Título del capítulo"},
    {"ChapterPage",     "Página del capítulo"},
    {"ChapterProgress", "Progreso del capítulo"},
    {"ChapterTime",     "Tiempo del capítulo"},
    {"BookTitle",       "Título del libro"},
    {"BookPage",        "Página del libro"},
    {"BookProgress",    "Progreso del libro"},
    {"BookTime",        "Tiempo del libro"},
};

QString widgetLabel(const QString& value) {
    for (const auto& choice : kWidgetChoices) {
        if (value.compare(QLatin1String(choice.value), Qt::CaseInsensitive) == 0) {
            return QString::fromUtf8(choice.label);
        }
    }
    return value;
}

QString zoneSummary(const QStringList& values) {
    if (values.isEmpty()) {
        return QStringLiteral("Ninguno");
    }

    QStringList labels;
    for (const auto& value : values) {
        labels << widgetLabel(value);
    }
    return labels.join(QStringLiteral(" · "));
}

QStringList readStringList(QSettings& settings, const QString& key) {
    const QVariant value = settings.value(key);
    if (!value.isValid()) {
        return {};
    }

    QStringList list = value.toStringList();
    if (list.isEmpty()) {
        const QString single = value.toString().trimmed();
        if (!single.isEmpty()) {
            list << single;
        }
    }
    return list;
}

class IntStepper : public QWidget {
public:
    IntStepper(int value, int minimum, int maximum, int step, const QString& suffix, QWidget* parent = nullptr)
        : QWidget(parent), current(value), minValue(minimum), maxValue(maximum), stepValue(step), suffixText(suffix) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(16);

        auto* minus = new QPushButton(QStringLiteral("−"), this);
        minus->setMinimumSize(92, 72);
        valueLabel = new QLabel(this);
        valueLabel->setAlignment(Qt::AlignCenter);
        valueLabel->setMinimumWidth(150);
        auto* plus = new QPushButton(QStringLiteral("+"), this);
        plus->setMinimumSize(92, 72);

        layout->addWidget(minus);
        layout->addWidget(valueLabel, 1);
        layout->addWidget(plus);

        QObject::connect(minus, &QPushButton::clicked, this, [this](bool) {
            current = qMax(minValue, current - stepValue);
            refresh();
        });
        QObject::connect(plus, &QPushButton::clicked, this, [this](bool) {
            current = qMin(maxValue, current + stepValue);
            refresh();
        });

        refresh();
    }

    int value() const { return current; }

private:
    int current;
    int minValue;
    int maxValue;
    int stepValue;
    QString suffixText;
    QLabel* valueLabel = nullptr;

    void refresh() {
        valueLabel->setText(QStringLiteral("%1%2").arg(current).arg(suffixText));
    }
};

class CycleButton : public QPushButton {
public:
    CycleButton(const QList<QPair<QString, QString>>& values, const QString& current, QWidget* parent = nullptr)
        : QPushButton(parent), entries(values) {
        index = 0;
        for (int i = 0; i < entries.size(); ++i) {
            if (entries.at(i).first.compare(current, Qt::CaseInsensitive) == 0) {
                index = i;
                break;
            }
        }
        setMinimumHeight(72);
        QObject::connect(this, &QPushButton::clicked, this, [this](bool) {
            if (entries.isEmpty()) {
                return;
            }
            index = (index + 1) % entries.size();
            refresh();
        });
        refresh();
    }

    QString value() const {
        return entries.isEmpty() ? QString() : entries.at(index).first;
    }

private:
    QList<QPair<QString, QString>> entries;
    int index = 0;

    void refresh() {
        setText(entries.isEmpty() ? QString() : entries.at(index).second);
    }
};

class ToggleButton : public QPushButton {
public:
    ToggleButton(bool value, const QString& onText, const QString& offText, QWidget* parent = nullptr)
        : QPushButton(parent), state(value), enabledText(onText), disabledText(offText) {
        setMinimumHeight(72);
        QObject::connect(this, &QPushButton::clicked, this, [this](bool) {
            state = !state;
            refresh();
        });
        refresh();
    }

    bool value() const { return state; }

private:
    bool state;
    QString enabledText;
    QString disabledText;

    void refresh() {
        setText(state ? enabledText : disabledText);
    }
};

class SettingsOverlay : public QWidget {
public:
    explicit SettingsOverlay(QWidget* parent)
        : QWidget(parent), settings(DATA_DIR "/settings.ini", QSettings::IniFormat) {
        settings.setIniCodec("UTF-8");
        setObjectName(QStringLiteral("twksSettingsOverlay"));
        setGeometry(parent->rect());
        setFocusPolicy(Qt::StrongFocus);
        setStyleSheet(QStringLiteral(
            "#twksSettingsOverlay { background: white; color: black; }"
            "QWidget { font-size: 24px; color: black; }"
            "QPushButton { min-height: 64px; padding: 8px 16px; background: white; border: 2px solid #555; }"
            "QPushButton:checked { background: #d8d8d8; }"
            "QTabBar::tab { min-height: 64px; min-width: 190px; padding: 8px; background: white; border: 1px solid #777; }"
            "QTabBar::tab:selected { background: #d8d8d8; }"
            "QGroupBox { margin-top: 24px; padding-top: 18px; font-weight: bold; }"
        ));

        loadValues();
        buildUi();
        raise();
        show();
        setFocus(Qt::OtherFocusReason);
    }

private:
    QSettings settings;
    QStackedWidget* pages = nullptr;
    QWidget* mainPage = nullptr;
    QWidget* zonePage = nullptr;
    QLabel* statusLabel = nullptr;
    QLabel* zoneTitle = nullptr;
    QVBoxLayout* zoneChoicesLayout = nullptr;
    QStringList* editingZone = nullptr;
    QPushButton* editingZoneButton = nullptr;

    IntStepper* heightScale = nullptr;
    IntStepper* margins = nullptr;
    IntStepper* headerSpacer = nullptr;
    IntStepper* footerSpacer = nullptr;
    IntStepper* widgetSpacing = nullptr;
    IntStepper* batteryThreshold = nullptr;
    CycleButton* separator = nullptr;
    CycleButton* batteryStyle = nullptr;
    CycleButton* batteryChargingStyle = nullptr;
    ToggleButton* clock24h = nullptr;

    QStringList headerLeft;
    QStringList headerCenter;
    QStringList headerRight;
    QStringList footerLeft;
    QStringList footerCenter;
    QStringList footerRight;

    int valueInt(const QString& key, int fallback, int minValue, int maxValue) const {
        bool ok = false;
        int value = settings.value(key, fallback).toInt(&ok);
        if (!ok) {
            value = fallback;
        }
        return qBound(minValue, value, maxValue);
    }

    void loadValues() {
        headerLeft = readStringList(settings, QStringLiteral("Reading.Widget/HeaderLeft"));
        headerCenter = readStringList(settings, QStringLiteral("Reading.Widget/HeaderCenter"));
        headerRight = readStringList(settings, QStringLiteral("Reading.Widget/HeaderRight"));
        footerLeft = readStringList(settings, QStringLiteral("Reading.Widget/FooterLeft"));
        footerCenter = readStringList(settings, QStringLiteral("Reading.Widget/FooterCenter"));
        footerRight = readStringList(settings, QStringLiteral("Reading.Widget/FooterRight"));
    }

    QWidget* labeledControl(const QString& label, QWidget* control, QWidget* parent) {
        auto* row = new QWidget(parent);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 8, 0, 8);
        layout->setSpacing(24);
        auto* text = new QLabel(label, row);
        text->setWordWrap(true);
        text->setMinimumWidth(360);
        layout->addWidget(text, 1);
        layout->addWidget(control, 1);
        return row;
    }

    QWidget* buildDesignTab() {
        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);

        auto* body = new QWidget(scroll);
        auto* layout = new QVBoxLayout(body);
        layout->setContentsMargins(18, 18, 18, 18);
        layout->setSpacing(8);

        heightScale = new IntStepper(valueInt(QStringLiteral("Reading/HeaderFooterHeightScale"), 100, 50, 100), 50, 100, 5, QStringLiteral(" %"), body);
        margins = new IntStepper(valueInt(QStringLiteral("Reading/HeaderFooterMargins"), 50, 0, 100), 0, 100, 5, QString(), body);
        headerSpacer = new IntStepper(valueInt(QStringLiteral("Reading/HeaderSpacerHeight"), 0, 0, 100), 0, 100, 5, QString(), body);
        footerSpacer = new IntStepper(valueInt(QStringLiteral("Reading/FooterSpacerHeight"), 0, 0, 100), 0, 100, 5, QString(), body);
        widgetSpacing = new IntStepper(valueInt(QStringLiteral("Reading.Widget/Spacing"), 10, 0, 20), 0, 20, 1, QString(), body);
        batteryThreshold = new IntStepper(valueInt(QStringLiteral("Reading.Widget.Battery/ShowWhenBelow"), 100, 10, 100), 10, 100, 10, QStringLiteral(" %"), body);

        separator = new CycleButton({
            {QString(), QStringLiteral("Sin separador")},
            {QStringLiteral("Bullet"), QStringLiteral("Viñeta •")},
            {QStringLiteral("Dot"), QStringLiteral("Punto ·")},
            {QStringLiteral("Pipe"), QStringLiteral("Barra |")},
        }, settings.value(QStringLiteral("Reading.Widget/Separator"), QStringLiteral("Dot")).toString(), body);

        batteryStyle = new CycleButton({
            {QStringLiteral("Icon"), QStringLiteral("Solo icono")},
            {QStringLiteral("Level"), QStringLiteral("Solo porcentaje")},
            {QStringLiteral("IconLevel"), QStringLiteral("Icono + porcentaje")},
            {QStringLiteral("LevelIcon"), QStringLiteral("Porcentaje + icono")},
        }, settings.value(QStringLiteral("Reading.Widget.Battery/Style"), QStringLiteral("IconLevel")).toString(), body);

        batteryChargingStyle = new CycleButton({
            {QStringLiteral("Icon"), QStringLiteral("Solo icono")},
            {QStringLiteral("Level"), QStringLiteral("Solo porcentaje")},
            {QStringLiteral("IconLevel"), QStringLiteral("Icono + porcentaje")},
            {QStringLiteral("LevelIcon"), QStringLiteral("Porcentaje + icono")},
        }, settings.value(QStringLiteral("Reading.Widget.Battery/StyleCharging"), QStringLiteral("IconLevel")).toString(), body);

        clock24h = new ToggleButton(
            settings.value(QStringLiteral("Reading.Widget.Clock/24hFormat"), true).toBool(),
            QStringLiteral("24 horas"), QStringLiteral("12 horas"), body);

        layout->addWidget(labeledControl(QStringLiteral("Altura cabecera/pie"), heightScale, body));
        layout->addWidget(labeledControl(QStringLiteral("Márgenes laterales"), margins, body));
        layout->addWidget(labeledControl(QStringLiteral("Espacio superior"), headerSpacer, body));
        layout->addWidget(labeledControl(QStringLiteral("Espacio inferior"), footerSpacer, body));
        layout->addWidget(labeledControl(QStringLiteral("Espacio entre widgets"), widgetSpacing, body));
        layout->addWidget(labeledControl(QStringLiteral("Separador"), separator, body));
        layout->addWidget(labeledControl(QStringLiteral("Formato del reloj"), clock24h, body));
        layout->addWidget(labeledControl(QStringLiteral("Mostrar batería por debajo de"), batteryThreshold, body));
        layout->addWidget(labeledControl(QStringLiteral("Estilo de batería"), batteryStyle, body));
        layout->addWidget(labeledControl(QStringLiteral("Batería cargando"), batteryChargingStyle, body));
        layout->addStretch(1);

        scroll->setWidget(body);
        return scroll;
    }

    QWidget* buildZonesTab(const QString& title, QStringList* left, QStringList* center, QStringList* right) {
        auto* tab = new QWidget(this);
        auto* root = new QVBoxLayout(tab);
        root->setContentsMargins(24, 24, 24, 24);
        root->setSpacing(20);

        auto* intro = new QLabel(QStringLiteral("Toca una zona para elegir qué información aparece en ella."), tab);
        intro->setWordWrap(true);
        root->addWidget(intro);

        auto addZone = [&](const QString& label, QStringList* zone) {
            auto* group = new QGroupBox(label, tab);
            auto* groupLayout = new QVBoxLayout(group);
            auto* button = new QPushButton(zoneSummary(*zone), group);
            button->setMinimumHeight(92);
            QObject::connect(button, &QPushButton::clicked, this, [this, title, label, zone, button](bool) {
                openZoneEditor(title + QStringLiteral(" · ") + label, zone, button);
            });
            groupLayout->addWidget(button);
            root->addWidget(group);
        };

        addZone(QStringLiteral("Izquierda"), left);
        addZone(QStringLiteral("Centro"), center);
        addZone(QStringLiteral("Derecha"), right);
        root->addStretch(1);
        return tab;
    }

    void clearZoneChoices() {
        while (QLayoutItem* item = zoneChoicesLayout->takeAt(0)) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
    }

    void openZoneEditor(const QString& title, QStringList* zone, QPushButton* button) {
        editingZone = zone;
        editingZoneButton = button;
        zoneTitle->setText(title);
        statusLabel->clear();
        clearZoneChoices();

        for (const auto& choice : kWidgetChoices) {
            auto* option = new QPushButton(QString::fromUtf8(choice.label), zonePage);
            option->setCheckable(true);
            option->setProperty("widgetValue", QString::fromLatin1(choice.value));
            option->setChecked(zone->contains(QString::fromLatin1(choice.value), Qt::CaseInsensitive));
            zoneChoicesLayout->addWidget(option);
        }
        zoneChoicesLayout->addStretch(1);
        pages->setCurrentWidget(zonePage);
    }

    bool validateUniqueWidgets(const QStringList* replacementZone = nullptr, const QStringList& replacement = QStringList()) {
        QSet<QString> seen;
        const QList<QStringList*> zones = {
            &headerLeft, &headerCenter, &headerRight,
            &footerLeft, &footerCenter, &footerRight,
        };

        for (const auto* zone : zones) {
            const QStringList values = (zone == replacementZone) ? replacement : *zone;
            for (const auto& widget : values) {
                const QString key = widget.toLower();
                if (seen.contains(key)) {
                    statusLabel->setText(QStringLiteral("%1 ya está colocado en otra zona.").arg(widgetLabel(widget)));
                    return false;
                }
                seen.insert(key);
            }
        }
        return true;
    }

    void saveZoneEditor() {
        QStringList updated;
        for (int i = 0; i < zoneChoicesLayout->count(); ++i) {
            auto* button = qobject_cast<QPushButton*>(zoneChoicesLayout->itemAt(i)->widget());
            if (button && button->isCheckable() && button->isChecked()) {
                updated << button->property("widgetValue").toString();
            }
        }

        if (!validateUniqueWidgets(editingZone, updated)) {
            return;
        }

        *editingZone = updated;
        if (editingZoneButton) {
            editingZoneButton->setText(zoneSummary(updated));
        }
        statusLabel->clear();
        pages->setCurrentWidget(mainPage);
    }

    void saveValues() {
        settings.setValue(QStringLiteral("Reading/HeaderFooterHeightScale"), heightScale->value());
        settings.setValue(QStringLiteral("Reading/HeaderFooterMargins"), margins->value());
        settings.setValue(QStringLiteral("Reading/HeaderSpacerHeight"), headerSpacer->value());
        settings.setValue(QStringLiteral("Reading/FooterSpacerHeight"), footerSpacer->value());
        settings.setValue(QStringLiteral("Reading.Widget/HeaderLeft"), headerLeft);
        settings.setValue(QStringLiteral("Reading.Widget/HeaderCenter"), headerCenter);
        settings.setValue(QStringLiteral("Reading.Widget/HeaderRight"), headerRight);
        settings.setValue(QStringLiteral("Reading.Widget/FooterLeft"), footerLeft);
        settings.setValue(QStringLiteral("Reading.Widget/FooterCenter"), footerCenter);
        settings.setValue(QStringLiteral("Reading.Widget/FooterRight"), footerRight);
        settings.setValue(QStringLiteral("Reading.Widget/Spacing"), widgetSpacing->value());
        settings.setValue(QStringLiteral("Reading.Widget/Separator"), separator->value());
        settings.setValue(QStringLiteral("Reading.Widget.Battery/Style"), batteryStyle->value());
        settings.setValue(QStringLiteral("Reading.Widget.Battery/StyleCharging"), batteryChargingStyle->value());
        settings.setValue(QStringLiteral("Reading.Widget.Battery/ShowWhenBelow"), batteryThreshold->value());
        settings.setValue(QStringLiteral("Reading.Widget.Clock/24hFormat"), clock24h->value());
        settings.sync();
    }

    void closeOverlay() {
        hide();
        deleteLater();
    }

    QWidget* buildMainPage() {
        auto* page = new QWidget(this);
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(42, 36, 42, 36);
        root->setSpacing(18);

        auto* title = new QLabel(QStringLiteral("Kobo Tweaks · Ajustes de lectura"), page);
        title->setStyleSheet(QStringLiteral("font-size: 30px; font-weight: bold;"));
        root->addWidget(title);

        auto* tabs = new QTabWidget(page);
        tabs->addTab(buildDesignTab(), QStringLiteral("Diseño"));
        tabs->addTab(buildZonesTab(QStringLiteral("Cabecera"), &headerLeft, &headerCenter, &headerRight), QStringLiteral("Cabecera"));
        tabs->addTab(buildZonesTab(QStringLiteral("Pie"), &footerLeft, &footerCenter, &footerRight), QStringLiteral("Pie"));
        root->addWidget(tabs, 1);

        auto* note = new QLabel(QStringLiteral("Los cambios se guardan en settings.ini. Por ahora se aplican al volver a abrir el libro."), page);
        note->setWordWrap(true);
        root->addWidget(note);

        auto* buttons = new QHBoxLayout();
        auto* cancel = new QPushButton(QStringLiteral("Cancelar"), page);
        auto* apply = new QPushButton(QStringLiteral("Aplicar"), page);
        cancel->setMinimumHeight(82);
        apply->setMinimumHeight(82);
        buttons->addWidget(cancel, 1);
        buttons->addWidget(apply, 1);
        root->addLayout(buttons);

        QObject::connect(cancel, &QPushButton::clicked, this, [this](bool) { closeOverlay(); });
        QObject::connect(apply, &QPushButton::clicked, this, [this](bool) {
            statusLabel->clear();
            if (!validateUniqueWidgets()) {
                return;
            }
            saveValues();
            closeOverlay();
        });
        return page;
    }

    QWidget* buildZonePage() {
        auto* page = new QWidget(this);
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(42, 36, 42, 36);
        root->setSpacing(18);

        zoneTitle = new QLabel(page);
        zoneTitle->setStyleSheet(QStringLiteral("font-size: 30px; font-weight: bold;"));
        root->addWidget(zoneTitle);

        auto* info = new QLabel(QStringLiteral("Selecciona los elementos. Cada widget solo puede aparecer en una zona."), page);
        info->setWordWrap(true);
        root->addWidget(info);

        auto* scroll = new QScrollArea(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* choices = new QWidget(scroll);
        zoneChoicesLayout = new QVBoxLayout(choices);
        zoneChoicesLayout->setContentsMargins(10, 10, 10, 10);
        zoneChoicesLayout->setSpacing(12);
        scroll->setWidget(choices);
        root->addWidget(scroll, 1);

        auto* buttons = new QHBoxLayout();
        auto* back = new QPushButton(QStringLiteral("Cancelar"), page);
        auto* save = new QPushButton(QStringLiteral("Guardar zona"), page);
        back->setMinimumHeight(82);
        save->setMinimumHeight(82);
        buttons->addWidget(back, 1);
        buttons->addWidget(save, 1);
        root->addLayout(buttons);

        QObject::connect(back, &QPushButton::clicked, this, [this](bool) {
            statusLabel->clear();
            pages->setCurrentWidget(mainPage);
        });
        QObject::connect(save, &QPushButton::clicked, this, [this](bool) { saveZoneEditor(); });
        return page;
    }

    void buildUi() {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);

        pages = new QStackedWidget(this);
        mainPage = buildMainPage();
        zonePage = buildZonePage();
        pages->addWidget(mainPage);
        pages->addWidget(zonePage);
        pages->setCurrentWidget(mainPage);
        root->addWidget(pages, 1);

        statusLabel = new QLabel(this);
        statusLabel->setAlignment(Qt::AlignCenter);
        statusLabel->setWordWrap(true);
        statusLabel->setStyleSheet(QStringLiteral("font-weight: bold; padding: 8px;"));
        root->addWidget(statusLabel);
    }
};

QFileSystemWatcher* gWatcher = nullptr;
QPointer<SettingsOverlay> gOverlay;
bool gOpenScheduled = false;

void openSettingsOverlay() {
    gOpenScheduled = false;

    QFile trigger(QString::fromLatin1(kTriggerPath));
    if (!trigger.exists()) {
        return;
    }
    trigger.remove();

    if (gOverlay) {
        gOverlay->raise();
        gOverlay->show();
        return;
    }

    if (!MainWindowController_sharedInstance || !MainWindowController_currentView) {
        return;
    }

    QWidget* parent = MainWindowController_currentView(MainWindowController_sharedInstance());
    if (!parent) {
        return;
    }

    gOverlay = new SettingsOverlay(parent);
}

void scheduleOpen() {
    if (gOpenScheduled) {
        return;
    }
    if (!QFile::exists(QString::fromLatin1(kTriggerPath))) {
        return;
    }

    // NickelMenu launches the trigger while its popup is still unwinding.
    // Delay opening so QMenu can release its mouse/touch grab first.
    gOpenScheduled = true;
    QTimer::singleShot(650, QCoreApplication::instance(), []() { openSettingsOverlay(); });
}

void installWatcherNow() {
    if (gWatcher || !QCoreApplication::instance()) {
        return;
    }

    QDir().mkpath(QStringLiteral(DATA_DIR));
    gWatcher = new QFileSystemWatcher(QCoreApplication::instance());
    if (!gWatcher->addPath(QStringLiteral(DATA_DIR))) {
        nh_log("Kobo Tweaks settings overlay: failed to watch %s", DATA_DIR);
        return;
    }

    QObject::connect(gWatcher, &QFileSystemWatcher::directoryChanged, QCoreApplication::instance(), [](const QString&) {
        scheduleOpen();
    });

    // Handle a trigger left behind by a previous interrupted attempt.
    scheduleOpen();
}

struct LateBootstrap {
    LateBootstrap() {
        QTimer::singleShot(0, []() { installWatcherNow(); });
    }
};

LateBootstrap gLateBootstrap;

} // namespace
