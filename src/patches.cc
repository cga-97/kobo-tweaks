#include "patches.h"
#include "common.h"
#include "utils.h"
#include <QString>
#include <QVariant>
#include <QPair>
#include <QVector>

namespace {
    QString replaceGeneratedRule(QString qss, const QString& id, const QString& rule) {
        const QString begin = QStringLiteral("/* KoboTweaks:%1:begin */").arg(id);
        const QString end = QStringLiteral("/* KoboTweaks:%1:end */").arg(id);

        int start = qss.indexOf(begin);
        while (start >= 0) {
            const int endPos = qss.indexOf(end, start);
            if (endPos < 0) {
                nh_log("Kobo Tweaks QSS: incomplete generated rule '%s'; leaving stylesheet unchanged", id.toUtf8().constData());
                return qss;
            }

            // Include the separator inserted before the previous generated
            // block so a live reload replaces the whole owned block exactly.
            if (start > 0 && qss.at(start - 1) == QLatin1Char('\n')) {
                --start;
            }
            int removeEnd = endPos + end.size();
            if (removeEnd < qss.size() && qss.at(removeEnd) == QLatin1Char('\n')) {
                ++removeEnd;
            }
            qss.remove(start, removeEnd - start);
            start = qss.indexOf(begin);
        }

        if (!qss.isEmpty() && !qss.endsWith(QLatin1Char('\n'))) {
            qss.append(QLatin1Char('\n'));
        }
        qss.append(QStringLiteral("%1\n%2\n%3\n").arg(begin, rule, end));
        return qss;
    }
}

namespace Patch {
    namespace ReadingView {
        QString scaleHeaderFooterHeight(const QString& qss, int scale) {
            // Based on: Reduce new header/footer height - jackie_w
            const QVariant fn = QVariant::fromValue<QssPropertyFunc>([scale](const QString& property, const QString& value) {
                return Qss::scaleValue(property, value, scale);
            });

            const QVector<QPair<QString, QVariant>> properties = {
                {QStringLiteral("min-height"), fn},
                {QStringLiteral("max-height"), fn},
            };

            QString result(qss);

            // Touch/Mini (Trilogy)
            result = Qss::updateProperties(result, QStringLiteral("ReadingFooter[qApp_deviceIsTrilogy=true]"), properties);
            // Glo/Aura/Aura2/Nia (Phoenix)
            result = Qss::updateProperties(result, QStringLiteral("ReadingFooter[qApp_deviceIsPhoenix=true]"), properties);
            // AuraHD/AuraH2O/AuraH202/GloHD/ClaraHD/Clara2E (Dragon)
            result = Qss::updateProperties(result, QStringLiteral("ReadingFooter[qApp_deviceIsDragon=true]"), properties);
            // AuraOne/Forma/Sage/Elipsa/Elipsa2E (Daylight)
            result = Qss::updateProperties(result, QStringLiteral("ReadingFooter[qApp_deviceIsDaylight=true]"), properties);
            // LibraH2O/Libra2 (Storm)
            result = Qss::updateProperties(result, QStringLiteral("ReadingFooter[qApp_deviceIsStorm=true]"), properties);

            return result;
        }

        QString setFixedHeight(QString qss, const QString& selector, int height) {
            return replaceGeneratedRule(
                qss,
                QStringLiteral("height:%1").arg(selector),
                QStringLiteral("%1 { min-height: %2px; max-height: %2px; }")
                    .arg(selector)
                    .arg(height)
            );
        }

        QString resetHeight(QString qss, const QString& selector) {
            // 16777215 = QWIDGETSIZE_MAX. Use the same generated-rule id as
            // setFixedHeight so repeated live reloads replace the previous
            // rule instead of growing ReadingView's stylesheet indefinitely.
            return replaceGeneratedRule(
                qss,
                QStringLiteral("height:%1").arg(selector),
                QStringLiteral("%1 { min-height: 0px; max-height: 16777215px; }").arg(selector)
            );
        }

        QString setPaddings(QString qss, const QString& selector, int top, int right, int bottom, int left) {
            return replaceGeneratedRule(
                qss,
                QStringLiteral("padding:%1").arg(selector),
                QStringLiteral("%1 { padding-top: %2px; padding-right: %3px; padding-bottom: %4px; padding-left: %5px; }")
                    .arg(selector)
                    .arg(top)
                    .arg(right)
                    .arg(bottom)
                    .arg(left)
            );
        }

        QString addBrightnessLabelQss(const QString& qss) {
            const QString rule =
                QStringLiteral("#twksBrightnessLabel { border: 1px solid black; background: white; padding: 6px; }\n")
                + QStringLiteral("#gestureContainer[darkMode=true] #twksBrightnessLabel { border: 1px solid white; background: black; }\n")
                + QStringLiteral("#twksBrightnessLabel[qApp_deviceIsTrilogy=true] { font-size: 14px; }\n")
                + QStringLiteral("#twksBrightnessLabel[qApp_deviceIsPhoenix=true] { font-size: 17px; }\n")
                + QStringLiteral("#twksBrightnessLabel[qApp_deviceIsDragon=true] { font-size: 25px; }\n")
                + QStringLiteral("#twksBrightnessLabel[qApp_deviceIsStorm=true] { font-size: 29px; }\n")
                + QStringLiteral("#twksBrightnessLabel[qApp_deviceIsDaylight=true] { font-size: 32px; }");

            return replaceGeneratedRule(qss, QStringLiteral("brightness-label"), rule);
        }
    }
}
