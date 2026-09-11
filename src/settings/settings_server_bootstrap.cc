#include "settings_server.h"

#include <QTimer>

namespace {
struct SettingsServerBootstrap {
    SettingsServerBootstrap() {
        QTimer::singleShot(0, []() { SettingsServer::start(); });
    }
};

SettingsServerBootstrap gSettingsServerBootstrap;
}
