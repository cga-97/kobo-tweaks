#include "settings_server.h"
#include "../common.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>

namespace SettingsServer {
namespace {

QTcpServer* gServer = nullptr;
constexpr quint16 kPort = 8765;

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

const QStringList kZoneKeys = {
    QStringLiteral("Reading.Widget/HeaderLeft"),
    QStringLiteral("Reading.Widget/HeaderCenter"),
    QStringLiteral("Reading.Widget/HeaderRight"),
    QStringLiteral("Reading.Widget/FooterLeft"),
    QStringLiteral("Reading.Widget/FooterCenter"),
    QStringLiteral("Reading.Widget/FooterRight"),
};

QString htmlEsc(const QString& s) {
    return s.toHtmlEscaped();
}

QStringList readList(QSettings& s, const QString& key) {
    QVariant v = s.value(key);
    if (!v.isValid()) return {};
    QStringList out = v.toStringList();
    if (out.isEmpty()) {
        const QString one = v.toString().trimmed();
        if (!one.isEmpty()) out << one;
    }
    return out;
}

QString makeUrl(const QString& path, const QList<QPair<QString, QString>>& params) {
    QUrl url;
    url.setPath(path);
    QUrlQuery q;
    for (const auto& p : params) q.addQueryItem(p.first, p.second);
    url.setQuery(q);
    return url.toString(QUrl::FullyEncoded);
}

QString linkButton(const QString& text, const QString& url) {
    return QStringLiteral("<a class='btn' href='%1'>%2</a>").arg(htmlEsc(url), htmlEsc(text));
}

QString stepRow(QSettings& s, const QString& label, const QString& key,
                int fallback, int minValue, int maxValue, int step, const QString& suffix = QString()) {
    bool ok = false;
    int value = s.value(key, fallback).toInt(&ok);
    if (!ok) value = fallback;
    value = qBound(minValue, value, maxValue);
    int minus = qMax(minValue, value - step);
    int plus = qMin(maxValue, value + step);

    const QString minusUrl = makeUrl(QStringLiteral("/set"), {
        {QStringLiteral("key"), key}, {QStringLiteral("value"), QString::number(minus)}
    });
    const QString plusUrl = makeUrl(QStringLiteral("/set"), {
        {QStringLiteral("key"), key}, {QStringLiteral("value"), QString::number(plus)}
    });

    return QStringLiteral(
        "<div class='row'><div class='label'>%1</div><div class='ctl'>%2"
        "<span class='value'>%3%4</span>%5</div></div>")
        .arg(htmlEsc(label), linkButton(QStringLiteral("−"), minusUrl), QString::number(value), htmlEsc(suffix), linkButton(QStringLiteral("+"), plusUrl));
}

QString enumRow(QSettings& s, const QString& label, const QString& key,
                const QList<QPair<QString, QString>>& entries, const QString& fallback) {
    QString current = s.value(key, fallback).toString();
    int idx = 0;
    for (int i = 0; i < entries.size(); ++i) {
        if (entries.at(i).first.compare(current, Qt::CaseInsensitive) == 0) {
            idx = i;
            break;
        }
    }
    const int prev = (idx - 1 + entries.size()) % entries.size();
    const int next = (idx + 1) % entries.size();
    const QString prevUrl = makeUrl(QStringLiteral("/set"), {
        {QStringLiteral("key"), key}, {QStringLiteral("value"), entries.at(prev).first}
    });
    const QString nextUrl = makeUrl(QStringLiteral("/set"), {
        {QStringLiteral("key"), key}, {QStringLiteral("value"), entries.at(next).first}
    });
    return QStringLiteral(
        "<div class='row'><div class='label'>%1</div><div class='ctl'>%2"
        "<span class='value wide'>%3</span>%4</div></div>")
        .arg(htmlEsc(label), linkButton(QStringLiteral("‹"), prevUrl), htmlEsc(entries.at(idx).second), linkButton(QStringLiteral("›"), nextUrl));
}

QString boolRow(QSettings& s, const QString& label, const QString& key, bool fallback) {
    const bool current = s.value(key, fallback).toBool();
    const QString url = makeUrl(QStringLiteral("/set"), {
        {QStringLiteral("key"), key}, {QStringLiteral("value"), current ? QStringLiteral("0") : QStringLiteral("1")}
    });
    return QStringLiteral("<div class='row'><div class='label'>%1</div><div class='ctl'>%2</div></div>")
        .arg(htmlEsc(label), linkButton(current ? QStringLiteral("24 horas") : QStringLiteral("12 horas"), url));
}

QString zoneForm(QSettings& s, const QString& title, const QString& zoneKey) {
    const QStringList selected = readList(s, zoneKey);
    QString html = QStringLiteral("<section><h2>%1</h2><form action='/zone' method='get'>").arg(htmlEsc(title));
    html += QStringLiteral("<input type='hidden' name='zone' value='%1'>").arg(htmlEsc(zoneKey));
    for (const auto& choice : kWidgetChoices) {
        const QString value = QString::fromLatin1(choice.value);
        const bool checked = selected.contains(value, Qt::CaseInsensitive);
        html += QStringLiteral("<label class='check'><input type='checkbox' name='w' value='%1'%2> %3</label>")
            .arg(htmlEsc(value), checked ? QStringLiteral(" checked") : QString(), QString::fromUtf8(choice.label));
    }
    html += QStringLiteral("<button class='save' type='submit'>Guardar esta zona</button></form></section>");
    return html;
}

QString buildPage(const QString& message = QString()) {
    QSettings s(DATA_DIR "/settings.ini", QSettings::IniFormat);
    s.setIniCodec("UTF-8");
    s.sync();

    const QList<QPair<QString, QString>> separatorEntries = {
        {QString(), QStringLiteral("Sin separador")},
        {QStringLiteral("Bullet"), QStringLiteral("Viñeta •")},
        {QStringLiteral("Dot"), QStringLiteral("Punto ·")},
        {QStringLiteral("Pipe"), QStringLiteral("Barra |")},
    };
    const QList<QPair<QString, QString>> batteryEntries = {
        {QStringLiteral("Icon"), QStringLiteral("Solo icono")},
        {QStringLiteral("Level"), QStringLiteral("Solo porcentaje")},
        {QStringLiteral("IconLevel"), QStringLiteral("Icono + porcentaje")},
        {QStringLiteral("LevelIcon"), QStringLiteral("Porcentaje + icono")},
    };

    QString html = QStringLiteral(
        "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>"
        "body{font-family:sans-serif;margin:0;padding:18px;background:#fff;color:#000;font-size:24px}"
        "h1{font-size:34px;margin:0 0 12px}h2{font-size:28px;margin:24px 0 12px}"
        ".note,.msg{padding:14px;border:2px solid #555;margin:12px 0}.msg{font-weight:bold}"
        ".row{display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #bbb;padding:14px 0;gap:16px}"
        ".label{flex:1}.ctl{display:flex;align-items:center;gap:10px}.value{display:inline-block;min-width:110px;text-align:center}.wide{min-width:240px}"
        ".btn,.save{display:inline-block;min-width:62px;min-height:54px;line-height:54px;padding:0 18px;border:2px solid #333;background:#fff;color:#000;text-decoration:none;text-align:center;font-size:24px}"
        "section{border-top:3px solid #000;margin-top:24px;padding-top:8px}.check{display:block;padding:12px 0;border-bottom:1px solid #ccc}.check input{width:30px;height:30px;vertical-align:middle}"
        ".save{width:100%;margin-top:16px;height:72px}"
        "</style></head><body><h1>Kobo Tweaks</h1>"
        "<div class='note'>Configuración táctil usando el navegador nativo de Nickel. Los cambios se guardan al momento; por ahora, cierra este popup y vuelve a abrir el libro para verlos.</div>");

    if (!message.isEmpty()) html += QStringLiteral("<div class='msg'>%1</div>").arg(htmlEsc(message));

    html += QStringLiteral("<h2>Diseño</h2>");
    html += stepRow(s, QStringLiteral("Altura cabecera/pie"), QStringLiteral("Reading/HeaderFooterHeightScale"), 100, 50, 100, 5, QStringLiteral(" %"));
    html += stepRow(s, QStringLiteral("Márgenes laterales"), QStringLiteral("Reading/HeaderFooterMargins"), 50, 0, 100, 5);
    html += stepRow(s, QStringLiteral("Espacio superior"), QStringLiteral("Reading/HeaderSpacerHeight"), 0, 0, 100, 5);
    html += stepRow(s, QStringLiteral("Espacio inferior"), QStringLiteral("Reading/FooterSpacerHeight"), 0, 0, 100, 5);
    html += stepRow(s, QStringLiteral("Espacio entre widgets"), QStringLiteral("Reading.Widget/Spacing"), 10, 0, 20, 1);
    html += enumRow(s, QStringLiteral("Separador"), QStringLiteral("Reading.Widget/Separator"), separatorEntries, QStringLiteral("Dot"));
    html += boolRow(s, QStringLiteral("Formato del reloj"), QStringLiteral("Reading.Widget.Clock/24hFormat"), true);
    html += stepRow(s, QStringLiteral("Mostrar batería por debajo de"), QStringLiteral("Reading.Widget.Battery/ShowWhenBelow"), 100, 10, 100, 10, QStringLiteral(" %"));
    html += enumRow(s, QStringLiteral("Estilo de batería"), QStringLiteral("Reading.Widget.Battery/Style"), batteryEntries, QStringLiteral("IconLevel"));
    html += enumRow(s, QStringLiteral("Batería cargando"), QStringLiteral("Reading.Widget.Battery/StyleCharging"), batteryEntries, QStringLiteral("IconLevel"));

    html += zoneForm(s, QStringLiteral("Cabecera · izquierda"), QStringLiteral("Reading.Widget/HeaderLeft"));
    html += zoneForm(s, QStringLiteral("Cabecera · centro"), QStringLiteral("Reading.Widget/HeaderCenter"));
    html += zoneForm(s, QStringLiteral("Cabecera · derecha"), QStringLiteral("Reading.Widget/HeaderRight"));
    html += zoneForm(s, QStringLiteral("Pie · izquierda"), QStringLiteral("Reading.Widget/FooterLeft"));
    html += zoneForm(s, QStringLiteral("Pie · centro"), QStringLiteral("Reading.Widget/FooterCenter"));
    html += zoneForm(s, QStringLiteral("Pie · derecha"), QStringLiteral("Reading.Widget/FooterRight"));
    html += QStringLiteral("</body></html>");
    return html;
}

bool applySet(QSettings& s, const QString& key, const QString& value) {
    struct IntSpec { const char* key; int min; int max; };
    const IntSpec ints[] = {
        {"Reading/HeaderFooterHeightScale", 50, 100},
        {"Reading/HeaderFooterMargins", 0, 100},
        {"Reading/HeaderSpacerHeight", 0, 100},
        {"Reading/FooterSpacerHeight", 0, 100},
        {"Reading.Widget/Spacing", 0, 20},
        {"Reading.Widget.Battery/ShowWhenBelow", 10, 100},
    };
    for (const auto& spec : ints) {
        if (key == QLatin1String(spec.key)) {
            bool ok = false;
            int v = value.toInt(&ok);
            if (!ok) return false;
            s.setValue(key, qBound(spec.min, v, spec.max));
            s.sync();
            return true;
        }
    }

    if (key == QLatin1String("Reading.Widget.Clock/24hFormat")) {
        s.setValue(key, value == QLatin1String("1") || value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
        s.sync();
        return true;
    }

    if (key == QLatin1String("Reading.Widget/Separator")) {
        const QStringList allowed = {QString(), QStringLiteral("Bullet"), QStringLiteral("Dot"), QStringLiteral("Pipe")};
        if (!allowed.contains(value)) return false;
        s.setValue(key, value);
        s.sync();
        return true;
    }

    if (key == QLatin1String("Reading.Widget.Battery/Style") || key == QLatin1String("Reading.Widget.Battery/StyleCharging")) {
        const QStringList allowed = {QStringLiteral("Icon"), QStringLiteral("Level"), QStringLiteral("IconLevel"), QStringLiteral("LevelIcon")};
        if (!allowed.contains(value)) return false;
        s.setValue(key, value);
        s.sync();
        return true;
    }
    return false;
}

bool applyZone(QSettings& s, const QString& zone, const QStringList& requested) {
    if (!kZoneKeys.contains(zone)) return false;

    QStringList clean;
    for (const auto& choice : kWidgetChoices) {
        const QString value = QString::fromLatin1(choice.value);
        if (requested.contains(value, Qt::CaseInsensitive)) clean << value;
    }

    // Keep each widget unique globally. Moving it here removes it from other zones.
    for (const auto& other : kZoneKeys) {
        if (other == zone) continue;
        QStringList values = readList(s, other);
        for (const auto& selected : clean) values.removeAll(selected);
        s.setValue(other, values);
    }
    s.setValue(zone, clean);
    s.sync();
    return true;
}

void sendHtml(QTcpSocket* socket, const QString& html) {
    const QByteArray body = html.toUtf8();
    QByteArray header = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Length: ";
    header += QByteArray::number(body.size());
    header += "\r\n\r\n";
    socket->write(header);
    socket->write(body);
    socket->disconnectFromHost();
}

void handleRequest(QTcpSocket* socket) {
    const QByteArray raw = socket->readAll();
    const int end = raw.indexOf("\r\n");
    if (end < 0) return;
    const QByteArray first = raw.left(end);
    const QList<QByteArray> parts = first.split(' ');
    if (parts.size() < 2 || parts.at(0) != "GET") {
        sendHtml(socket, buildPage(QStringLiteral("Solicitud no compatible")));
        return;
    }

    QUrl url = QUrl::fromEncoded(parts.at(1));
    QUrlQuery query(url);
    QString message;
    QSettings s(DATA_DIR "/settings.ini", QSettings::IniFormat);
    s.setIniCodec("UTF-8");

    if (url.path() == QLatin1String("/set")) {
        if (applySet(s, query.queryItemValue(QStringLiteral("key")), query.queryItemValue(QStringLiteral("value")))) {
            message = QStringLiteral("Cambio guardado");
        } else {
            message = QStringLiteral("No se pudo aplicar ese cambio");
        }
    } else if (url.path() == QLatin1String("/zone")) {
        if (applyZone(s, query.queryItemValue(QStringLiteral("zone")), query.allQueryItemValues(QStringLiteral("w")))) {
            message = QStringLiteral("Zona guardada");
        } else {
            message = QStringLiteral("Zona no válida");
        }
    }

    sendHtml(socket, buildPage(message));
}

} // namespace

void start() {
    if (gServer) return;
    gServer = new QTcpServer(QCoreApplication::instance());
    QObject::connect(gServer, &QTcpServer::newConnection, gServer, []() {
        while (gServer->hasPendingConnections()) {
            QTcpSocket* socket = gServer->nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() { handleRequest(socket); });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    if (!gServer->listen(QHostAddress::LocalHost, kPort)) {
        nh_log("Kobo Tweaks settings server failed to listen on 127.0.0.1:%u", static_cast<unsigned>(kPort));
    } else {
        nh_log("Kobo Tweaks settings server listening on 127.0.0.1:%u", static_cast<unsigned>(kPort));
    }
}

} // namespace SettingsServer
