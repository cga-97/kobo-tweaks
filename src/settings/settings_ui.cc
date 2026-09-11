#include "../common.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

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

class SettingsDialog : public QDialog {
public:
    explicit SettingsDialog(QWidget* parent = nullptr)
        : QDialog(parent), settings(DATA_DIR "/settings.ini", QSettings::IniFormat) {
        settings.setIniCodec("UTF-8");
        setWindowTitle(QStringLiteral("Kobo Tweaks · Ajustes de lectura"));
        setModal(true);
        setMinimumSize(900, 1100);
        setStyleSheet(QStringLiteral(
            "QDialog, QWidget { font-size: 24px; }"
            "QPushButton, QComboBox, QSpinBox { min-height: 62px; }"
            "QPushButton { padding: 8px 18px; }"
            "QListWidget::item { min-height: 58px; }"
            "QTabBar::tab { min-height: 62px; min-width: 190px; padding: 8px; }"
        ));
        loadValues();
        buildUi();
    }

private:
    QSettings settings;
    QSpinBox* heightScale = nullptr;
    QSpinBox* margins = nullptr;
    QSpinBox* headerSpacer = nullptr;
    QSpinBox* footerSpacer = nullptr;
    QSpinBox* widgetSpacing = nullptr;
    QSpinBox* batteryThreshold = nullptr;
    QComboBox* separator = nullptr;
    QComboBox* batteryStyle = nullptr;
    QComboBox* batteryChargingStyle = nullptr;
    QCheckBox* clock24h = nullptr;
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

    QSpinBox* makeSpin(int value, int minValue, int maxValue, int step = 1, const QString& suffix = QString()) {
        auto* spin = new QSpinBox(this);
        spin->setRange(minValue, maxValue);
        spin->setSingleStep(step);
        spin->setValue(value);
        if (!suffix.isEmpty()) {
            spin->setSuffix(suffix);
        }
        return spin;
    }

    QComboBox* makeEnumCombo(const QList<QPair<QString, QString>>& entries, const QString& current) {
        auto* combo = new QComboBox(this);
        for (const auto& entry : entries) {
            combo->addItem(entry.second, entry.first);
        }
        const int index = combo->findData(current, Qt::UserRole, Qt::MatchFixedString);
        combo->setCurrentIndex(index >= 0 ? index : 0);
        return combo;
    }

    void loadValues() {
        headerLeft = readStringList(settings, QStringLiteral("Reading.Widget/HeaderLeft"));
        headerCenter = readStringList(settings, QStringLiteral("Reading.Widget/HeaderCenter"));
        headerRight = readStringList(settings, QStringLiteral("Reading.Widget/HeaderRight"));
        footerLeft = readStringList(settings, QStringLiteral("Reading.Widget/FooterLeft"));
        footerCenter = readStringList(settings, QStringLiteral("Reading.Widget/FooterCenter"));
        footerRight = readStringList(settings, QStringLiteral("Reading.Widget/FooterRight"));
    }

    QWidget* buildLayoutTab() {
        auto* tab = new QWidget(this);
        auto* layout = new QFormLayout(tab);
        layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        layout->setVerticalSpacing(22);

        heightScale = makeSpin(valueInt(QStringLiteral("Reading/HeaderFooterHeightScale"), 100, 50, 100), 50, 100, 5, QStringLiteral(" %"));
        margins = makeSpin(valueInt(QStringLiteral("Reading/HeaderFooterMargins"), 50, 0, 100), 0, 100, 5);
        headerSpacer = makeSpin(valueInt(QStringLiteral("Reading/HeaderSpacerHeight"), 0, 0, 100), 0, 100, 5);
        footerSpacer = makeSpin(valueInt(QStringLiteral("Reading/FooterSpacerHeight"), 0, 0, 100), 0, 100, 5);
        widgetSpacing = makeSpin(valueInt(QStringLiteral("Reading.Widget/Spacing"), 10, 0, 20), 0, 20, 1);

        separator = makeEnumCombo({
            {QString(), QStringLiteral("Sin separador")},
            {QStringLiteral("Bullet"), QStringLiteral("Viñeta •")},
            {QStringLiteral("Dot"), QStringLiteral("Punto ·")},
            {QStringLiteral("Pipe"), QStringLiteral("Barra |")},
        }, settings.value(QStringLiteral("Reading.Widget/Separator"), QStringLiteral("Dot")).toString());

        clock24h = new QCheckBox(QStringLiteral("Formato de 24 horas"), this);
        clock24h->setChecked(settings.value(QStringLiteral("Reading.Widget.Clock/24hFormat"), true).toBool());

        batteryThreshold = makeSpin(valueInt(QStringLiteral("Reading.Widget.Battery/ShowWhenBelow"), 100, 10, 100), 10, 100, 10, QStringLiteral(" %"));
        batteryStyle = makeEnumCombo({
            {QStringLiteral("Icon"), QStringLiteral("Solo icono")},
            {QStringLiteral("Level"), QStringLiteral("Solo porcentaje")},
            {QStringLiteral("IconLevel"), QStringLiteral("Icono + porcentaje")},
            {QStringLiteral("LevelIcon"), QStringLiteral("Porcentaje + icono")},
        }, settings.value(QStringLiteral("Reading.Widget.Battery/Style"), QStringLiteral("IconLevel")).toString());
        batteryChargingStyle = makeEnumCombo({
            {QStringLiteral("Icon"), QStringLiteral("Solo icono")},
            {QStringLiteral("Level"), QStringLiteral("Solo porcentaje")},
            {QStringLiteral("IconLevel"), QStringLiteral("Icono + porcentaje")},
            {QStringLiteral("LevelIcon"), QStringLiteral("Porcentaje + icono")},
        }, settings.value(QStringLiteral("Reading.Widget.Battery/StyleCharging"), QStringLiteral("IconLevel")).toString());

        layout->addRow(QStringLiteral("Altura cabecera/pie"), heightScale);
        layout->addRow(QStringLiteral("Márgenes laterales"), margins);
        layout->addRow(QStringLiteral("Espacio superior"), headerSpacer);
        layout->addRow(QStringLiteral("Espacio inferior"), footerSpacer);
        layout->addRow(QStringLiteral("Espacio entre widgets"), widgetSpacing);
        layout->addRow(QStringLiteral("Separador"), separator);
        layout->addRow(QStringLiteral("Reloj"), clock24h);
        layout->addRow(QStringLiteral("Mostrar batería por debajo de"), batteryThreshold);
        layout->addRow(QStringLiteral("Estilo de batería"), batteryStyle);
        layout->addRow(QStringLiteral("Batería cargando"), batteryChargingStyle);
        return tab;
    }

    void editZone(const QString& title, QStringList* values, QPushButton* button) {
        QDialog dialog(this);
        dialog.setWindowTitle(title);
        dialog.setMinimumSize(760, 1050);
        dialog.setStyleSheet(styleSheet());

        auto* root = new QVBoxLayout(&dialog);
        auto* info = new QLabel(QStringLiteral("Selecciona los elementos. Un widget solo puede estar en una zona."), &dialog);
        info->setWordWrap(true);
        root->addWidget(info);

        auto* list = new QListWidget(&dialog);
        for (const auto& choice : kWidgetChoices) {
            auto* item = new QListWidgetItem(QString::fromUtf8(choice.label), list);
            item->setData(Qt::UserRole, QString::fromLatin1(choice.value));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(values->contains(QString::fromLatin1(choice.value), Qt::CaseInsensitive) ? Qt::Checked : Qt::Unchecked);
        }
        root->addWidget(list, 1);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Guardar zona"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancelar"));
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        root->addWidget(buttons);

        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        QStringList updated;
        for (int i = 0; i < list->count(); ++i) {
            auto* item = list->item(i);
            if (item->checkState() == Qt::Checked) {
                updated << item->data(Qt::UserRole).toString();
            }
        }
        *values = updated;
        button->setText(zoneSummary(*values));
    }

    QWidget* buildZonesTab(const QString& title, QStringList* left, QStringList* center, QStringList* right) {
        auto* tab = new QWidget(this);
        auto* root = new QVBoxLayout(tab);
        auto* intro = new QLabel(QStringLiteral("Toca una zona para elegir qué información aparece en ella."), tab);
        intro->setWordWrap(true);
        root->addWidget(intro);

        auto addZone = [&](const QString& label, QStringList* zone) {
            auto* group = new QGroupBox(label, tab);
            auto* groupLayout = new QVBoxLayout(group);
            auto* button = new QPushButton(zoneSummary(*zone), group);
            button->setMinimumHeight(84);
            QObject::connect(button, &QPushButton::clicked, this, [this, title, label, zone, button]() {
                editZone(title + QStringLiteral(" · ") + label, zone, button);
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

    bool validateUniqueWidgets() {
        QSet<QString> seen;
        const QList<QStringList*> zones = {
            &headerLeft, &headerCenter, &headerRight,
            &footerLeft, &footerCenter, &footerRight,
        };
        for (const auto* zone : zones) {
            for (const auto& widget : *zone) {
                const QString key = widget.toLower();
                if (seen.contains(key)) {
                    QMessageBox::warning(this, QStringLiteral("Widget duplicado"),
                        QStringLiteral("%1 está seleccionado en más de una zona. Cada widget solo puede aparecer una vez.").arg(widgetLabel(widget)));
                    return false;
                }
                seen.insert(key);
            }
        }
        return true;
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
        settings.setValue(QStringLiteral("Reading.Widget/Separator"), separator->currentData().toString());
        settings.setValue(QStringLiteral("Reading.Widget.Battery/Style"), batteryStyle->currentData().toString());
        settings.setValue(QStringLiteral("Reading.Widget.Battery/StyleCharging"), batteryChargingStyle->currentData().toString());
        settings.setValue(QStringLiteral("Reading.Widget.Battery/ShowWhenBelow"), batteryThreshold->value());
        settings.setValue(QStringLiteral("Reading.Widget.Clock/24hFormat"), clock24h->isChecked());
        settings.sync();
    }

    void buildUi() {
        auto* root = new QVBoxLayout(this);
        auto* tabs = new QTabWidget(this);
        tabs->addTab(buildLayoutTab(), QStringLiteral("Diseño"));
        tabs->addTab(buildZonesTab(QStringLiteral("Cabecera"), &headerLeft, &headerCenter, &headerRight), QStringLiteral("Cabecera"));
        tabs->addTab(buildZonesTab(QStringLiteral("Pie"), &footerLeft, &footerCenter, &footerRight), QStringLiteral("Pie"));
        root->addWidget(tabs, 1);

        auto* note = new QLabel(QStringLiteral("Los cambios se guardan directamente en settings.ini. En esta primera versión se aplican al volver a abrir el libro; la recarga visual en vivo será la siguiente fase."), this);
        note->setWordWrap(true);
        root->addWidget(note);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Aplicar"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancelar"));
        QObject::connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            if (!validateUniqueWidgets()) {
                return;
            }
            saveValues();
            accept();
        });
        QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(buttons);
    }
};

QFileSystemWatcher* gWatcher = nullptr;
bool gInstallScheduled = false;

void openSettingsDialog() {
    QFile trigger(QString::fromLatin1(kTriggerPath));
    if (!trigger.exists()) {
        return;
    }
    trigger.remove();

    QWidget* parent = nullptr;
    if (MainWindowController_sharedInstance && MainWindowController_currentView) {
        parent = MainWindowController_currentView(MainWindowController_sharedInstance());
    }

    SettingsDialog dialog(parent);
    dialog.exec();
}

void installWatcherNow() {
    if (gWatcher || !QCoreApplication::instance()) {
        return;
    }

    QDir().mkpath(QStringLiteral(DATA_DIR));
    gWatcher = new QFileSystemWatcher(QCoreApplication::instance());
    if (!gWatcher->addPath(QStringLiteral(DATA_DIR))) {
        nh_log("Kobo Tweaks settings UI: could not watch %s", DATA_DIR);
        return;
    }

    QObject::connect(gWatcher, &QFileSystemWatcher::directoryChanged, QCoreApplication::instance(), [](const QString&) {
        QTimer::singleShot(0, QCoreApplication::instance(), []() { openSettingsDialog(); });
    });
    QTimer::singleShot(0, QCoreApplication::instance(), []() { openSettingsDialog(); });
    nh_log("Kobo Tweaks settings UI watcher installed");
}

void scheduleInstall() {
    if (gInstallScheduled) {
        return;
    }
    gInstallScheduled = true;
    if (QCoreApplication::instance()) {
        QTimer::singleShot(1000, QCoreApplication::instance(), []() { installWatcherNow(); });
    }
}

Q_COREAPP_STARTUP_FUNCTION(scheduleInstall)

struct LateBootstrap {
    LateBootstrap() { scheduleInstall(); }
};

LateBootstrap gLateBootstrap;

} // namespace
