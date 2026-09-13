#pragma once
#include "../../common.h"
#include "../../settings/settings.h"
#include "../../adapters/reading_view.h"
#include "../../widgets/base/widget_zone.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLayout>
#include <QPointer>
#include <QTimer>
#include <QWidget>

class TwWidgetZonesContainer : public QWidget {
    Q_OBJECT

public:
    TwWidgetZonesContainer(TweaksReadingSettings stt, const QString& qss, QWidget* parent = nullptr) : QWidget(parent), readingSettings(stt) {
        setContentsMargins(readingSettings.headerFooterMargins, 0, readingSettings.headerFooterMargins, 0);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
        setStyleSheet(qss);
        setAttribute(Qt::WA_TransparentForMouseEvents);

        lay = new QHBoxLayout(this);
        lay->setSpacing(qMax(20, readingSettings.widgetSpacing));
        lay->setContentsMargins(0, 0, 0, 0);
    }

    ~TwWidgetZonesContainer() override {
        if (observedSpacer) {
            observedSpacer->removeEventFilter(this);

            // A previous scaled container may have fixed the spacer height.
            // Release that constraint before reload installs a replacement or
            // switches back to an empty spacer with its own QSS height rule.
            observedSpacer->setMinimumHeight(0);
            observedSpacer->setMaximumHeight(QWIDGETSIZE_MAX);
        }
    }

    void setupZones(ReadingView* readingView, ReadingViewAdapters adapters, const QString& contentTitle, int minimumSideWidth, QVector<WidgetTypeEnum> leftWidgets, QVector<WidgetTypeEnum> centerWidgets, QVector<WidgetTypeEnum> rightWidgets) {
        if (addedWidgets) {
            return;
        }
        addedWidgets = true;

        bool hasLeft = !leftWidgets.isEmpty();
        bool hasCenter = !centerWidgets.isEmpty();
        bool hasRight = !rightWidgets.isEmpty();

        // 1. Nothing
        if (!hasLeft && !hasCenter && !hasRight) {
            return;
        }

        // 2. Only Left
        if (hasLeft && !hasCenter && !hasRight) {
            TwWidgetZone* leftZone = new TwWidgetZone(readingSettings, readingView, adapters, contentTitle, leftWidgets);
            lay->addWidget(leftZone, 1, Qt::AlignLeft);
            return;
        }

        // 3. Only Center
        if (!hasLeft && hasCenter && !hasRight) {
            TwWidgetZone* centerZone = new TwWidgetZone(readingSettings, readingView, adapters, contentTitle, centerWidgets);
            lay->addWidget(centerZone, 1, Qt::AlignCenter);
            return;
        }

        // 4. Only Right
        if (!hasLeft && !hasCenter && hasRight) {
            TwWidgetZone* rightZone = new TwWidgetZone(readingSettings, readingView, adapters, contentTitle, rightWidgets, true);
            lay->addWidget(rightZone, 1, Qt::AlignRight);
            return;
        }

        // 5. Left + Right, no Center
        if (hasLeft && !hasCenter && hasRight) {
            TwWidgetZone* leftZone = new TwWidgetZone(readingSettings, readingView, adapters, contentTitle, leftWidgets);
            TwWidgetZone* rightZone = new TwWidgetZone(readingSettings, readingView, adapters, contentTitle, rightWidgets, true);
            lay->addWidget(leftZone, 0, Qt::AlignLeft);
            lay->addStretch(0);
            lay->addWidget(rightZone, 0, Qt::AlignRight);
            return;
        }

        // 6. Center and either Left/Right or both
        if (hasCenter) {
            TwWidgetZone* leftZone = new TwWidgetZone(readingSettings, readingView, adapters, contentTitle, leftWidgets);
            TwWidgetZone* centerZone = new TwWidgetZone(readingSettings, readingView, adapters, contentTitle, centerWidgets);
            TwWidgetZone* rightZone = new TwWidgetZone(readingSettings, readingView, adapters, contentTitle, rightWidgets, true);
            lay->addWidget(leftZone, 0, Qt::AlignLeft);
            lay->addWidget(centerZone, 1, Qt::AlignCenter);
            lay->addWidget(rightZone, 0, Qt::AlignRight);

            // Set min width for left & right sides
            leftZone->setMinimumWidth(minimumSideWidth);
            leftZone->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
            rightZone->setMinimumWidth(minimumSideWidth);
            rightZone->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
        }
    }

protected:
    bool event(QEvent* ev) override {
        const bool handled = QWidget::event(ev);

        if (ev->type() == QEvent::ParentChange) {
            if (observedSpacer) {
                observedSpacer->removeEventFilter(this);
            }

            observedSpacer = parentWidget();
            if (observedSpacer) {
                observedSpacer->installEventFilter(this);

                // addWidget() reparents us before the surrounding layout and
                // stylesheet have necessarily completed their geometry pass.
                QTimer::singleShot(0, this, [this]() {
                    syncSpacerGeometry(true);
                });
            }
        } else if (ev->type() == QEvent::StyleChange || ev->type() == QEvent::Polish) {
            QTimer::singleShot(0, this, [this]() {
                syncSpacerGeometry(false);
            });
        }

        return handled;
    }

    bool eventFilter(QObject* watched, QEvent* ev) override {
        if (observedSpacer && watched == observedSpacer.data()
            && (ev->type() == QEvent::LayoutRequest
                || ev->type() == QEvent::Resize
                || ev->type() == QEvent::Show)) {
            syncSpacerGeometry(false);
        }

        return QWidget::eventFilter(watched, ev);
    }

private:
    void syncSpacerGeometry(bool forceLog) {
        QWidget* spacer = observedSpacer.data();
        if (!spacer) {
            return;
        }

        QLayout* spacerLayout = spacer->layout();
        if (!spacerLayout) {
            return;
        }

        const bool isHeader = spacer->objectName() == QStringLiteral("topSpacer");
        const bool isFooter = spacer->objectName() == QStringLiteral("bottomSpacer");
        if (!isHeader && !isFooter) {
            return;
        }

        // ReadingFooter::setFooterMargin() may run after Kobo Tweaks has
        // installed this container. If it writes Nickel's native left/right
        // margin into our spacer layout, HeaderFooterMargins gets stacked on
        // top of that native margin and changing the setting barely moves the
        // widgets. Our layout owns horizontal positioning, so keep the spacer
        // layout horizontal margins at zero and preserve only the configured
        // vertical spacer.
        const int expectedTop = isHeader ? readingSettings.headerSpacerHeight : 0;
        const int expectedBottom = isFooter ? readingSettings.footerSpacerHeight : 0;
        const QMargins currentMargins = spacerLayout->contentsMargins();
        const bool marginsWrong = currentMargins.left() != 0
            || currentMargins.right() != 0
            || currentMargins.top() != expectedTop
            || currentMargins.bottom() != expectedBottom;

        if (marginsWrong) {
            spacerLayout->setContentsMargins(0, expectedTop, 0, expectedBottom);
        }

        // ReadingFooter.qss gives the container a fixed min/max height after
        // HeaderFooterHeightScale is applied. Mirror that fixed height onto the
        // parent spacer so Nickel's ReadingFooter geometry cannot keep the
        // original (e.g. 131 px on Daylight/Sage) height around a smaller child.
        ensurePolished();
        const int childMinHeight = minimumHeight();
        const int childMaxHeight = maximumHeight();
        int targetSpacerHeight = -1;
        if (childMinHeight > 0 && childMinHeight == childMaxHeight) {
            targetSpacerHeight = childMinHeight + expectedTop + expectedBottom;
            if (spacer->minimumHeight() != targetSpacerHeight || spacer->maximumHeight() != targetSpacerHeight) {
                spacer->setMinimumHeight(targetSpacerHeight);
                spacer->setMaximumHeight(targetSpacerHeight);
            }
        }

        if (forceLog || !geometryLogged) {
            geometryLogged = true;
            nh_log(
                "Kobo Tweaks geometry: %s scale=%d margin=%d spacerTop=%d spacerBottom=%d child[min=%d max=%d hint=%d height=%d] parent[min=%d max=%d height=%d] target=%d",
                spacer->objectName().toUtf8().constData(),
                readingSettings.headerFooterHeightScale,
                readingSettings.headerFooterMargins,
                expectedTop,
                expectedBottom,
                childMinHeight,
                childMaxHeight,
                sizeHint().height(),
                height(),
                spacer->minimumHeight(),
                spacer->maximumHeight(),
                spacer->height(),
                targetSpacerHeight
            );
        }

        if (marginsWrong && !marginCorrectionLogged) {
            marginCorrectionLogged = true;
            nh_log(
                "Kobo Tweaks geometry: corrected Nickel spacer margins on %s (was %d,%d,%d,%d)",
                spacer->objectName().toUtf8().constData(),
                currentMargins.left(),
                currentMargins.top(),
                currentMargins.right(),
                currentMargins.bottom()
            );
        }
    }

    TweaksReadingSettings readingSettings;
    QHBoxLayout* lay;
    QPointer<QWidget> observedSpacer;
    bool addedWidgets = false;
    bool geometryLogged = false;
    bool marginCorrectionLogged = false;
};
