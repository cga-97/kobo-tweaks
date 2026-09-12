#pragma once
#include <QWidget>
#include <QString>

#include "../common.h"
#include "../patches.h"
#include "../widgets/base/widget_zone.h"
#include "../widgets/base/widget_zones_container.h"


namespace ReadingViewHook {
    void constructor(ReadingView* self, QWidget* parent);
    void setFooterMargin(QWidget* self, int margin);

    // Reload Kobo Tweaks' header/footer widgets from settings.ini without
    // recreating Nickel's ReadingView. Returns false when the current view is
    // not an active reader or its adapters are not available yet.
    bool reloadWidgets();

    namespace DogEarDelegate {
        using Constructor = void (*)(QWidget* self, QWidget* parent, const QString& image);
        void constructor(QWidget* self, QWidget* parent, const QString& image, Constructor original);
    }

    namespace AdobeReader {
        void constructor(QWidget* self, QWidget* parent, PluginState* state, const QString& image);
    }

    namespace BrightnessEventFilterHook {
        void updateBrightnessHeader(BrightnessEventFilter* self, const QString& text, const QString& sth);
    }
}
