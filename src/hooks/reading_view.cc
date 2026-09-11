#include "reading_view.h"
#include "../adapters/reading_view.h"
#include "colour_attr_cleaner.h"

#include <QHBoxLayout>
#include <QLayout>
#include <QMetaEnum>
#include <QMetaObject>
#include <QPointer>
#include <vector>

// On colour Kobos, SelectionController::onInlineDefinitionResults adds two
// extra Qt::WidgetAttribute flags to the ReadingView when the dictionary popup
// appears (enabling "full colour refresh" waveform), but never clears them.
// Because they are never cleared, our widgets that repaint periodically also
// flash with the expensive waveform every time they update.
//
// We fix this by connecting to SelectionController::closeFooterMenu (the
// canonical selection teardown signal) and clearing those attrs from the
// ReadingView. The attrs are looked up by name via QMetaEnum so we don't
// hardcode magic numbers that may shift between firmware versions, and the
// whole path is gated behind Device::hasColorDisplay() so it is a no-op on
// B&W devices.
static const char* const kExtraColourAttrs[] = {
    "WA_KoboEpdUpdateModeFull",
    "WA_KoboEpdWfModeGCC16",
};

// QObject::staticQtMetaObject is protected; re-expose via a derived class
// so we can look up Qt namespace enums by name on older Qt (pre-Q_NAMESPACE).
namespace {
struct QtMetaAccess : QObject {
    using QObject::staticQtMetaObject;
};
}

const std::vector<Qt::WidgetAttribute>& resolvedColourAttrs()
{
    static const std::vector<Qt::WidgetAttribute> v = [] {
        std::vector<Qt::WidgetAttribute> r;

        if (!kt_has_color_display) {
            nh_log("No colour display, skipping SelectionController colour-flash fix");
            return r;
        }

        const QMetaObject& mo = QtMetaAccess::staticQtMetaObject;
        int enumIdx = mo.indexOfEnumerator("WidgetAttribute");
        if (enumIdx < 0) {
            nh_log("could not find Qt::WidgetAttribute meta enum");
            return r;
        }
        QMetaEnum me = mo.enumerator(enumIdx);
        for (auto& name : kExtraColourAttrs) {
            bool ok = false;
            int value = me.keyToValue(name, &ok);
            if (ok) {
                r.push_back(static_cast<Qt::WidgetAttribute>(value));
            } else {
                nh_log("unknown Qt::WidgetAttribute key: %s", name);
            }
        }
        return r;
    }();
    return v;
}

namespace ReadingViewHook {
    static TweaksSettings settings;
    static bool isDarkMode = false;
    static int originalContentsMargins = 0;

    QString contentTitle;

    namespace {
        bool zonesEmpty(const QVector<WidgetTypeEnum>& left, const QVector<WidgetTypeEnum>& center, const QVector<WidgetTypeEnum>& right) {
            return left.isEmpty() && center.isEmpty() && right.isEmpty();
        }

        QString makeWidgetQss(const TweaksReadingSettings& readingSettings) {
            QString readingFooterQss = Qss::getContent(QStringLiteral(":/qss/ReadingFooter.qss"));
            QString patchedQss = Qss::copySelectors(
                readingFooterQss,
                QStringLiteral("#caption"),
                QStringList() << QStringLiteral("#twksLabel") << QStringLiteral("#twksSeparator")
            );
            if (readingSettings.headerFooterHeightScale < 100) {
                patchedQss = Patch::ReadingView::scaleHeaderFooterHeight(patchedQss, readingSettings.headerFooterHeightScale);
            }
            patchedQss.replace(QStringLiteral("ReadingFooter"), QStringLiteral("TwWidgetZonesContainer"));
            return patchedQss;
        }

        void applySpacerHeightQss(ReadingView* view, const TweaksReadingSettings& readingSettings, bool emptyHeader, bool emptyFooter, bool includeBrightnessQss) {
            QString rootQss = view->styleSheet();

            if (emptyHeader) {
                rootQss = Patch::ReadingView::setFixedHeight(rootQss, QStringLiteral("#topSpacer"), readingSettings.headerSpacerHeight);
            } else {
                rootQss = Patch::ReadingView::resetHeight(rootQss, QStringLiteral("#topSpacer"));
            }

            if (emptyFooter) {
                rootQss = Patch::ReadingView::setFixedHeight(rootQss, QStringLiteral("#bottomSpacer"), readingSettings.footerSpacerHeight);
            } else {
                rootQss = Patch::ReadingView::resetHeight(rootQss, QStringLiteral("#bottomSpacer"));
            }

            if (includeBrightnessQss) {
                rootQss = Patch::ReadingView::addBrightnessLabelQss(rootQss);
            }
            view->setStyleSheet(rootQss);
        }

        void clearTweaksSpacer(QWidget* spacer) {
            if (!spacer) {
                return;
            }

            const auto containers = spacer->findChildren<TwWidgetZonesContainer*>(QString(), Qt::FindDirectChildrenOnly);
            for (auto* container : containers) {
                delete container;
            }

            // Kobo Tweaks owns the layout it installs on topSpacer/bottomSpacer.
            // Once the custom container is gone, remove that layout too so a new
            // one can be installed with the updated spacer/margin settings.
            if (QLayout* layout = spacer->layout()) {
                delete layout;
            }
        }

        TwWidgetZonesContainer* installContainer(
            QWidget* spacer,
            const TweaksReadingSettings& readingSettings,
            const QString& patchedQss,
            bool header
        ) {
            auto* container = new TwWidgetZonesContainer(readingSettings, patchedQss);
            container->setObjectName(header ? QStringLiteral("twksHeaderContainer") : QStringLiteral("twksFooterContainer"));

            auto* layout = new QHBoxLayout(spacer);
            if (header) {
                layout->setContentsMargins(0, readingSettings.headerSpacerHeight, 0, 0);
            } else {
                layout->setContentsMargins(0, 0, 0, readingSettings.footerSpacerHeight);
            }
            layout->addWidget(container, 1);
            return container;
        }
    }

    bool reloadWidgets() {
        if (!MainWindowController_sharedInstance || !MainWindowController_currentView) {
            nh_log("Kobo Tweaks runtime reload: MainWindowController symbols unavailable");
            return false;
        }

        void* mwc = MainWindowController_sharedInstance();
        ReadingView* view = MainWindowController_currentView(mwc);
        if (!view) {
            nh_log("Kobo Tweaks runtime reload: no current view");
            return false;
        }

        auto* gestureContainer = view->findChild<GestureReceivingContainer*>(QStringLiteral("gestureContainer"), Qt::FindDirectChildrenOnly);
        if (!gestureContainer) {
            nh_log("Kobo Tweaks runtime reload: current view is not a ReadingView");
            return false;
        }

        auto* topSpacer = gestureContainer->findChild<ReadingFooter*>(QStringLiteral("topSpacer"), Qt::FindDirectChildrenOnly);
        auto* bottomSpacer = gestureContainer->findChild<ReadingFooter*>(QStringLiteral("bottomSpacer"), Qt::FindDirectChildrenOnly);
        if (!topSpacer || !bottomSpacer) {
            nh_log("Kobo Tweaks runtime reload: topSpacer/bottomSpacer unavailable");
            return false;
        }

        ReadingViewAdapters adapters {};
        adapters.pageChanged = view->findChild<ReadingViewAdapter::PageChanged*>(QString(), Qt::FindDirectChildrenOnly);
        adapters.renderVolume = view->findChild<ReadingViewAdapter::RenderVolume*>(QString(), Qt::FindDirectChildrenOnly);
        adapters.readerDoneLoading = view->findChild<ReadingViewAdapter::ReaderDoneLoading*>(QString(), Qt::FindDirectChildrenOnly);
        adapters.darkMode = gestureContainer->findChild<ReadingViewAdapter::DarkMode*>(QString(), Qt::FindDirectChildrenOnly);

        if (!adapters.pageChanged || !adapters.renderVolume || !adapters.readerDoneLoading || !adapters.darkMode) {
            nh_log("Kobo Tweaks runtime reload: reader adapters unavailable");
            return false;
        }

        // Re-read the INI but do not recreate ReadingView. Existing widget
        // signal connections disappear automatically when their receivers are
        // deleted below; the long-lived adapters remain attached to Nickel.
        settings.load();
        const TweaksReadingSettings readingSettings = settings.getReadingSettings();

        const bool emptyHeader = zonesEmpty(
            readingSettings.widgetHeaderLeft,
            readingSettings.widgetHeaderCenter,
            readingSettings.widgetHeaderRight
        );
        const bool emptyFooter = zonesEmpty(
            readingSettings.widgetFooterLeft,
            readingSettings.widgetFooterCenter,
            readingSettings.widgetFooterRight
        );

        clearTweaksSpacer(topSpacer);
        clearTweaksSpacer(bottomSpacer);
        applySpacerHeightQss(view, readingSettings, emptyHeader, emptyFooter, false);

        const QString patchedQss = makeWidgetQss(readingSettings);
        TwWidgetZonesContainer* headerContainer = emptyHeader ? nullptr : installContainer(topSpacer, readingSettings, patchedQss, true);
        TwWidgetZonesContainer* footerContainer = emptyFooter ? nullptr : installContainer(bottomSpacer, readingSettings, patchedQss, false);

        const int minimumSideWidth = qMax(10, originalContentsMargins - readingSettings.headerFooterMargins);
        if (headerContainer) {
            headerContainer->setupZones(
                view,
                adapters,
                contentTitle,
                minimumSideWidth,
                readingSettings.widgetHeaderLeft,
                readingSettings.widgetHeaderCenter,
                readingSettings.widgetHeaderRight
            );
        }
        if (footerContainer) {
            footerContainer->setupZones(
                view,
                adapters,
                contentTitle,
                minimumSideWidth,
                readingSettings.widgetFooterLeft,
                readingSettings.widgetFooterCenter,
                readingSettings.widgetFooterRight
            );
        }

        // Newly-created page/progress/time widgets normally get their first
        // content on the next pageChanged signal. Invoke the existing adapter's
        // private Qt slot through the meta-object system so the current page is
        // populated immediately without turning a page or touching ReadingView.
        QMetaObject::invokeMethod(adapters.pageChanged, "notifyPageChanged", Qt::QueuedConnection);

        topSpacer->updateGeometry();
        bottomSpacer->updateGeometry();
        gestureContainer->updateGeometry();
        view->updateGeometry();
        view->update();

        nh_log("Kobo Tweaks runtime reload: reading widgets rebuilt");
        return true;
    }

    void constructor(ReadingView* view) {
        // Must parse settings before constructor since other widgets use them
        settings.load();
        settings.sync();

        ReadingView_constructor(view);

        // MUST NOT KEEP REFS TO THE WIDGETS, AS WE DON'T CONTROL THE LIFETIME

        // Fix flashing widgets on colour Kobos after text is selected / dictionary is shown.
        // SelectionController::onInlineDefinitionResults sets WA_KoboEpdUpdateModeFull (and
        // WA_KoboEpdWfModeGCC16) on the ReadingView but never clears them, causing every
        // subsequent repaint (including our periodic widget updates) to use the slow full-colour
        // waveform. Connect to closeFooterMenu — the canonical selection teardown signal — and
        // clear those attrs. resolvedColourAttrs() returns an empty list on B&W devices.
        auto children = view->findChildren<QObject*>(QString(), Qt::FindDirectChildrenOnly);
        for (auto child : children) {
            if (QLatin1String("SelectionController") == child->metaObject()->className()) {
                // Parent the cleaner to the SelectionController so lifetime is tied to it
                auto* cleaner = new ColourAttrCleaner(view, child);
                QObject::connect(child, SIGNAL(closeFooterMenu()),
                                 cleaner, SLOT(onFooterMenuClosed()));
                break;
            }
        }

        // Note: created and passed to grabGestures in the constructor, most events are passed to it
        QWidget* gestureContainer = view->findChild<GestureReceivingContainer*>(QStringLiteral("gestureContainer"), Qt::FindDirectChildrenOnly);
        if (!gestureContainer) {
            nh_log("could not find \"gestureContainer\"");
            return;
        }

        // Find topSpacer/bottomSpacer
        QWidget* topSpacer = gestureContainer->findChild<ReadingFooter*>(QStringLiteral("topSpacer"), Qt::FindDirectChildrenOnly);
        QWidget* bottomSpacer = gestureContainer->findChild<ReadingFooter*>(QStringLiteral("bottomSpacer"), Qt::FindDirectChildrenOnly);
        if (!topSpacer || !bottomSpacer) {
            nh_log("could not find \"topSpacer/bottomSpacer\"");
            return;
        }

        auto readingSettings = settings.getReadingSettings();
        const bool emptyHeader = zonesEmpty(readingSettings.widgetHeaderLeft, readingSettings.widgetHeaderCenter, readingSettings.widgetHeaderRight);
        const bool emptyFooter = zonesEmpty(readingSettings.widgetFooterLeft, readingSettings.widgetFooterCenter, readingSettings.widgetFooterRight);
        applySpacerHeightQss(view, readingSettings, emptyHeader, emptyFooter, true);

        // These adapters abstract the logic and ensure that the update methods on the widgets aren't called after either the widget or the ReadingView has been destroyed
        auto renderVolumeAdapter = new ReadingViewAdapter::RenderVolume(view);
        QObject::connect(renderVolumeAdapter, &ReadingViewAdapter::RenderVolume::renderVolume, view, [](const Volume& volume) {
            Content_getTitle(&contentTitle, &volume);
        });

        auto darkModeAdapter = new ReadingViewAdapter::DarkMode(gestureContainer, view);
        isDarkMode = darkModeAdapter->getDarkMode();
        QObject::connect(darkModeAdapter, &ReadingViewAdapter::DarkMode::darkModeChanged, view, [](bool dark) {
            isDarkMode = dark;
        });

        auto readerDoneLoadingAdapter = new ReadingViewAdapter::ReaderDoneLoading(view);

        ReadingViewAdapters adapters {};
        adapters.pageChanged = new ReadingViewAdapter::PageChanged(view);
        adapters.darkMode = darkModeAdapter;
        adapters.renderVolume = renderVolumeAdapter;
        adapters.readerDoneLoading = readerDoneLoadingAdapter;

        const QString patchedQss = makeWidgetQss(readingSettings);
        TwWidgetZonesContainer* headerContainer = emptyHeader ? nullptr : installContainer(topSpacer, readingSettings, patchedQss, true);
        TwWidgetZonesContainer* footerContainer = emptyFooter ? nullptr : installContainer(bottomSpacer, readingSettings, patchedQss, false);

        // The containers may later be destroyed by runtime reload. QPointer
        // keeps this readerDoneLoading handler safe if Nickel emits the signal
        // again after a reflow or other reader lifecycle event.
        QPointer<TwWidgetZonesContainer> headerGuard(headerContainer);
        QPointer<TwWidgetZonesContainer> footerGuard(footerContainer);
        QObject::connect(readerDoneLoadingAdapter, &ReadingViewAdapter::ReaderDoneLoading::readerDoneLoading, view, [view, adapters, readingSettings, headerGuard, footerGuard] {
            int minimumSideWidth = qMax(10, originalContentsMargins - readingSettings.headerFooterMargins);
            if (headerGuard) {
                headerGuard->setupZones(view, adapters, contentTitle, minimumSideWidth, readingSettings.widgetHeaderLeft, readingSettings.widgetHeaderCenter, readingSettings.widgetHeaderRight);
            }

            if (footerGuard) {
                footerGuard->setupZones(view, adapters, contentTitle, minimumSideWidth, readingSettings.widgetFooterLeft, readingSettings.widgetFooterCenter, readingSettings.widgetFooterRight);
            }
        });

        // Create a QLabel for showing "Brightness" text
        // Add directly to #gestureContainer
        QLabel* label = new QLabel(gestureContainer);
        label->hide();
        label->setObjectName(QStringLiteral("twksBrightnessLabel"));
        label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
        label->move(20, 20);
    }

    void setFooterMargin(QWidget* self, int margin) {
        // Save the original margin
        originalContentsMargins = margin;

        QLayout* layout = self->layout();
        layout->setContentsMargins(margin, 0, margin, 0);
    }

    namespace DogEarDelegate {
        void constructor(QWidget* self, QWidget* parent, const QString& orgImgPath) {
            QString imgPath = settings.getReadingBookmarkImage(isDarkMode);
            if (imgPath.isEmpty()) {
                imgPath = orgImgPath;
            }

            DogEarDelegate_constructor(self, parent, imgPath);
        }
    }

    namespace AdobeReader {
        void constructor(QWidget* self, QWidget* parent, PluginState* state, const QString& orgImgPath) {
            QString imgPath = settings.getReadingBookmarkImage(isDarkMode);
            if (imgPath.isEmpty()) {
                imgPath = orgImgPath;
            }

            AdobeReader_constructor(self, parent, state, imgPath);
        }
    }

    namespace BrightnessEventFilterHook {
        QLabel* findBrightnessLabel(BrightnessEventFilter* self) {
            // Check cached label
            QObject* cachedObj = self->property("cachedLabel").value<QObject*>();
            if (cachedObj) {
                return static_cast<QLabel*>(cachedObj);
            }

            // Find QLabel
            void* mwc = MainWindowController_sharedInstance();
            QWidget* view = MainWindowController_currentView(mwc);
            QWidget* gestureContainer = view->findChild<GestureReceivingContainer*>(QStringLiteral("gestureContainer"), Qt::FindDirectChildrenOnly);
            if (!gestureContainer) {
                return nullptr;
            }

            QLabel* label = gestureContainer->findChild<QLabel*>(QStringLiteral("twksBrightnessLabel"), Qt::FindDirectChildrenOnly);
            if (label) {
                // Cache
                self->setProperty("cachedLabel", QVariant::fromValue((QObject*)label));
                // Remove cached property when the label is destroyed
                QObject::connect(label, &QObject::destroyed, self, [self]() {
                    self->setProperty("cachedLabel", QVariant());
                });
            }

            return label;
        }

        void updateBrightnessHeader(BrightnessEventFilter* self, const QString& text, const QString&) {
            self->setProperty("pendingText", text);

            QTimer* hideTimer = self->findChild<QTimer*>(QStringLiteral("hideTimer"));
            if (hideTimer) {
                hideTimer->stop();
            } else {
                hideTimer = new QTimer(self);
                hideTimer->setObjectName(QStringLiteral("hideTimer"));
                hideTimer->setSingleShot(true);

                QObject::connect(hideTimer, &QTimer::timeout, self, [self]() {
                    QLabel* label = findBrightnessLabel(self);
                    if (label) {
                        label->hide();
                    }
                });
            }

            QTimer* showTimer = self->findChild<QTimer*>(QStringLiteral("showTimer"));
            if (!showTimer) {
                showTimer = new QTimer(self);
                showTimer->setObjectName(QStringLiteral("showTimer"));
                showTimer->setSingleShot(true);
                showTimer->setInterval(25);

                QObject::connect(showTimer, &QTimer::timeout, self, [self, hideTimer]() {
                    QLabel* label = findBrightnessLabel(self);
                    if (!label) {
                        return;
                    }

                    QString finalText = self->property("pendingText").toString();

                    label->show();
                    label->setText(finalText);
                    label->adjustSize();

                    hideTimer->start(2000);
                });
            }

            showTimer->start();
        }
    }
}
