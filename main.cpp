// ScopeBridge - CRC profile to EuroScope converter.
// Copyright (C) 2026 ScopeBridge contributors.
// SPDX-License-Identifier: GPL-3.0-only
// This program is provided without warranty; see LICENSE.
#include <QApplication>
#include <QBoxLayout>
#include <QCoreApplication>
#include <QColor>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QFrame>
#include <QFont>
#include <QFontDatabase>
#include <QListWidget>
#include <QMessageBox>
#include <QMap>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringDecoder>
#include <QTabBar>
#include <QTimer>
#include <QTableWidget>
#include <QTextStream>
#include <QUrl>
#include <QSet>
#include <oclero/qlementine.hpp>
#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace {
struct Map { QString id, name, source; bool selected = false; };
struct Profile { QString name, artcc, path, display, facility; QList<Map> maps; QStringList warnings; QJsonObject view; };
QString projectRoot() {
    const QString local = qEnvironmentVariable("SCOPEBRIDGE_ASSETS");
    if (!local.isEmpty()) return local;
    return QCoreApplication::applicationDirPath() + "/assets";
}
void copyRequired(const QString &from, const QString &to) {
    if (!QFileInfo(from).isFile()) throw QString("Required package asset is missing: %1").arg(from);
    if (QFileInfo::exists(to) && !QFile::remove(to)) throw QString("Cannot replace %1").arg(to);
    if (!QFile::copy(from, to)) throw QString("Cannot copy %1 to %2").arg(from, to);
}
void copyTree(const QString &from, const QString &to) {
    if (!QDir(from).exists() || !QDir().mkpath(to)) throw QString("Cannot copy assets from %1 to %2").arg(from, to);
    const QDir source(from);
    for (const QFileInfo &item : source.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dest = QDir(to).filePath(item.fileName());
        if (item.isDir()) copyTree(item.filePath(), dest);
        else copyRequired(item.filePath(), dest);
    }
}
QString rendererDll() {
    const QString asset = QDir(projectRoot()).filePath("JurinasRenderer.dll");
    if (QFileInfo::exists(asset)) return asset;
    throw QString("Renderer DLL is missing from ScopeBridge/assets. Rebuild and package the x86 plugin first.");
}

QJsonObject json(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw QString("Cannot open %1: %2").arg(path, file.errorString());
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        throw QString("Invalid JSON in %1: %2").arg(path, error.errorString());
    return doc.object();
}
struct ProfileEntry { QString name, artcc, path; };
QList<ProfileEntry> scanProfiles(const QString &path, QStringList &errors) {
    const QDir directory(path);
    if (!directory.exists()) throw QString("Profiles directory does not exist: %1").arg(path);
    QList<ProfileEntry> entries;
    for (const auto &file : directory.entryInfoList({"*.json"}, QDir::Files | QDir::Readable, QDir::Name)) {
        try {
            const auto data = json(file.absoluteFilePath());
            const QString name = data.value("Name").toString().trimmed();
            if (name.isEmpty()) { errors << file.fileName() + ": missing Name"; continue; }
            entries << ProfileEntry{name, data.value("ArtccId").toString(), file.absoluteFilePath()};
        } catch (const QString &e) { errors << e; }
    }
    std::sort(entries.begin(), entries.end(), [](const ProfileEntry &a, const ProfileEntry &b) {
        const int compared = QString::compare(a.name, b.name, Qt::CaseInsensitive);
        return compared == 0 ? a.path < b.path : compared < 0;
    });
    return entries;
}
void write(const QString &path, const QString &text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        throw QString("Cannot write %1: %2").arg(path, file.errorString());
    if (file.write(text.toUtf8()) < 0) throw QString("Cannot write %1: %2").arg(path, file.errorString());
}
QString safe(QString name) {
    name.replace(QRegularExpression("[^A-Za-z0-9_-]+"), "_");
    name = name.left(70);
    return name.isEmpty() ? "Profile" : name;
}
QString readableMapName(QString raw, const QString &facility, int number) {
    if (raw.isEmpty() || QRegularExpression("^[0-9A-HJKMNP-TV-Z]{20,}$").match(raw).hasMatch())
        return facility + " Video Map " + QString::number(number);
    raw.replace('_', ' ');
    raw.replace(QRegularExpression("\\s+"), " ");
    raw = raw.trimmed();
    // CRC's ERAM labels often prepend the facility/group and a filter slot.
    raw.remove(QRegularExpression("^[A-Za-z0-9]+\\s+[A-Za-z0-9]+\\s+F\\d+B\\d+\\s+", QRegularExpression::CaseInsensitiveOption));
    if (raw.isEmpty()) return facility + " Video Map " + QString::number(number);
    // The panel is narrow; keep the meaningful suffix (usually the chart
    // name) instead of a repeated facility prefix.
    if (raw.size() > 20 && raw.startsWith(facility + " ", Qt::CaseInsensitive)) raw = raw.mid(facility.size() + 1);
    return raw;
}
void facilities(const QJsonObject &f, QHash<QString, QJsonObject> &byId,
                QHash<QString, QString> &positions, QHash<QString, QString> &parents,
                const QString &parent = {}) {
    const QString id = f.value("id").toString();
    if (!id.isEmpty()) byId.insert(id, f);
    if (!parent.isEmpty()) parents.insert(id, parent);
    for (const auto &p : f.value("positions").toArray()) {
        const QString position = p.toObject().value("id").toString();
        if (!position.isEmpty()) positions.insert(position, id);
    }
    for (const auto &child : f.value("childFacilities").toArray())
        facilities(child.toObject(), byId, positions, parents, id);
}
QJsonObject activeDisplay(const QJsonObject &profile) {
    const auto windows = profile.value("DisplayWindowSettings").toArray();
    for (const auto &w : windows) {
        const auto window = w.toObject();
        if (window.value("WindowSettings").toObject().value("IsVisible").toBool(true)) {
            const auto displays = window.value("DisplaySettings").toArray();
            const auto selected = window.value("SelectedDisplayId").toString();
            for (const auto &d : displays)
                if (!selected.isEmpty() && d.toObject().value("Id").toString() == selected) return d.toObject();
            for (const auto &d : displays)
                if (!d.toObject().value("PositionId").toString().isEmpty() ||
                    !d.toObject().value("ActiveGeoMap").toString().isEmpty()) return d.toObject();
            if (!displays.isEmpty()) return displays.first().toObject();
        }
    }
    return {};
}
QString mapPath(const QString &root, const QString &artcc, const QString &id) {
    const QString nested = QDir(root).filePath(artcc + "/" + id + ".geojson");
    return QFileInfo::exists(nested) ? nested : QDir(root).filePath(id + ".geojson");
}
QString artccFile(const QString &profileFile, const QString &maps, const QString &artcc) {
    const QDir profileDir = QFileInfo(profileFile).dir();
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    const QStringList candidates = {
        profileDir.filePath("../ARTCCs/" + artcc + ".json"),
        QDir(maps).filePath("../ARTCCs/" + artcc + ".json"),
        QDir(maps).filePath("../../ARTCCs/" + artcc + ".json"),
        QDir(local).filePath("CRC/ARTCCs/" + artcc + ".json")
    };
    for (const auto &path : candidates) if (QFileInfo::exists(path)) return path;
    throw QString("Facility data for %1 was not found. Keep CRC/ARTCCs next to CRC/Profiles and CRC/VideoMaps, or install CRC locally.").arg(artcc);
}
Profile inspect(const QString &file, const QString &mapRoot) {
    const auto data = json(file);
    Profile p;
    p.path = file; p.name = data.value("Name").toString(QFileInfo(file).baseName());
    p.artcc = data.value("ArtccId").toString();
    if (p.artcc.isEmpty()) throw QString("Profile has no ArtccId: %1").arg(file);
    const auto root = json(artccFile(file, mapRoot, p.artcc)).value("facility").toObject();
    QHash<QString, QJsonObject> byId;
    QHash<QString, QString> positions;
    QHash<QString, QString> parents;
    facilities(root, byId, positions, parents);
    p.view = activeDisplay(data);
    if (p.view.isEmpty()) p.warnings << "No visible CRC display was found.";
    p.facility = positions.value(p.view.value("PositionId").toString(), p.view.value("FacilityId").toString());
    p.display = p.view.value("$type").toString().section('.', -1).section(',', 0, 0);
    QStringList ids;
    QStringList geoGroupIds;
    const QString geo = p.view.value("ActiveGeoMap").toString();
    if (!geo.isEmpty()) {
        bool matched = false;
        for (const auto &entry : root.value("eramConfiguration").toObject().value("geoMaps").toArray()) {
            const auto group = entry.toObject();
            if (group.value("name").toString() == geo) {
                matched = true;
                for (const auto &id : group.value("videoMapIds").toArray()) geoGroupIds << id.toString();
                break;
            }
        }
        const auto filter = p.view.value("MapFilters").toString();
        const auto re = QRegularExpression("\\bMap(\\d+)\\b", QRegularExpression::CaseInsensitiveOption);
        auto matches = re.globalMatch(filter);
        QSet<int> activeFilters;
        while (matches.hasNext()) {
            activeFilters.insert(matches.next().captured(1).toInt());
        }
        if (!matched || geoGroupIds.isEmpty()) p.warnings << "Active ERAM geo group was not found or is empty: " + geo;
        // ERAM MapFilters refer to feature 'filters' numbers, not the position
        // in geoMaps.videoMapIds (ZLA has Map11 with only one videoMapId).
        for (const auto &id : geoGroupIds) {
            const QString path = mapPath(mapRoot, p.artcc, id);
            if (!QFileInfo::exists(path)) continue;
            const auto features = json(path).value("features").toArray();
            bool enabled = false;
            for (const auto &feature : features) {
                for (const auto &number : feature.toObject().value("properties").toObject().value("filters").toArray())
                    if (activeFilters.contains(number.toInt())) { enabled = true; break; }
                if (enabled) break;
            }
            if (enabled) ids << id;
        }
    } else if (p.view.value("$type").toString().contains("Stars", Qt::CaseInsensitive)) {
        auto list = byId.value(p.facility).value("starsConfiguration").toObject().value("videoMapIds").toArray();
        QString ancestor = p.facility;
        while (list.isEmpty() && parents.contains(ancestor)) {
            ancestor = parents.value(ancestor);
            list = byId.value(ancestor).value("starsConfiguration").toObject().value("videoMapIds").toArray();
        }
        const auto selected = p.view.value("CurrentPrefSet").toObject().value("SelectedVideoMapIds").toArray();
        for (const auto &number : selected) {
            const int index = number.toInt(-1) - 1;
            if (index >= 0 && index < list.size()) ids << list.at(index).toString();
            else p.warnings << QString("STARS map number %1 is out of range.").arg(number.toInt());
        }
        if (ids.isEmpty() && !list.isEmpty()) {
            // Stale or empty selections must not silently turn every map on.
            p.warnings << "No saved STARS map resolves; available maps will start hidden (" + ancestor + ").";
        }
        if (ids.isEmpty()) {
            const auto f = byId.value(p.facility);
            const QString cab = f.value("towerCabConfiguration").toObject().value("videoMapId").toString();
            if (!cab.isEmpty()) {
                p.warnings << "No STARS maps configured; " + p.facility + " tower-cab map is available but starts hidden.";
            }
        }
    } else {
        const auto f = byId.value(p.facility);
        const QString type = p.view.value("$type").toString();
        const QString saidMap = f.value("saidConfiguration").toObject()
                                    .value("saabConfiguration").toObject().value("videoMapId").toString();
        if (type.contains("SaabSaid", Qt::CaseInsensitive)) {
            if (!saidMap.isEmpty()) ids << saidMap;
        } else {
            const QString key = type.contains("Asdex", Qt::CaseInsensitive) ? "asdexConfiguration" : "towerCabConfiguration";
            const auto id = f.value(key).toObject().value("videoMapId").toString();
            if (!id.isEmpty()) ids << id;
            // Some CRC ASDEX displays (e.g. ZOA/SMF) use a Saab SAID map instead.
            if (ids.isEmpty() && type.contains("Asdex", Qt::CaseInsensitive) && !saidMap.isEmpty()) ids << saidMap;
        }
        if (ids.isEmpty()) p.warnings << "No video map is configured for this display.";
    }
    ids.removeDuplicates();
    QStringList offered = ids;
    const QString starsType = p.view.value("$type").toString();
    if (starsType.contains("Stars", Qt::CaseInsensitive)) {
        auto facility = p.facility;
        auto available = byId.value(facility).value("starsConfiguration").toObject().value("videoMapIds").toArray();
        while (available.isEmpty() && parents.contains(facility)) {
            facility = parents.value(facility);
            available = byId.value(facility).value("starsConfiguration").toObject().value("videoMapIds").toArray();
        }
        for (const auto &id : available) if (!id.toString().isEmpty()) offered << id.toString();
    } else if (!p.view.value("ActiveGeoMap").toString().isEmpty()) {
        offered << geoGroupIds;
    } else {
        const auto f = byId.value(p.facility);
        for (const QString &id : {f.value("towerCabConfiguration").toObject().value("videoMapId").toString(),
                                 f.value("asdexConfiguration").toObject().value("videoMapId").toString(),
                                 f.value("saidConfiguration").toObject().value("saabConfiguration").toObject().value("videoMapId").toString()})
            if (!id.isEmpty()) offered << id;
    }
    offered.removeDuplicates();
    const QSet<QString> selected(ids.cbegin(), ids.cend());
    for (const auto &id : offered) {
        if (id.isEmpty()) continue;
        const QString path = mapPath(mapRoot, p.artcc, id);
        if (!QFileInfo::exists(path)) { if (selected.contains(id)) p.warnings << "Missing map: " + path; continue; }
        const auto data = json(path);
        p.maps << Map{id, readableMapName(data.value("name").toString(), p.facility.isEmpty() ? p.artcc : p.facility,
                                           offered.indexOf(id) + 1), path, selected.contains(id)};
    }
    return p;
}
QString dms(double value, bool latitude) {
    const QChar direction = latitude ? (value < 0 ? 'S' : 'N') : (value < 0 ? 'W' : 'E');
    const qint64 millis = qRound64(std::abs(value) * 3600000.0);
    const qint64 degrees = millis / 3600000, minutes = millis / 60000 % 60;
    const double seconds = (millis % 60000) / 1000.0;
    return QString("%1%2.%3.%4").arg(direction).arg(degrees, 3, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0')).arg(seconds, 6, 'f', 3, QChar('0'));
}
QString point(const QJsonValue &v) {
    const auto a = v.toArray();
    if (a.size() < 2 || !a.at(0).isDouble() || !a.at(1).isDouble()) return {};
    const double lon = a.at(0).toDouble(), lat = a.at(1).toDouble();
    if (!std::isfinite(lat) || !std::isfinite(lon) || std::abs(lat) > 90 || std::abs(lon) > 180) return {};
    return dms(lat, true) + " " + dms(lon, false);
}
void lines(const QJsonArray &path, const QString &name, QStringList &out, int &count) {
    Q_UNUSED(name);
    for (int i = 1; i < path.size(); ++i) {
        const auto a = point(path.at(i - 1)), b = point(path.at(i));
        if (!a.isEmpty() && !b.isEmpty() && a != b) {
            out << a + " " + b + " CRCMap";
            ++count;
        }
    }
}
void geometry(const QJsonObject &g, const QString &name, QStringList &out, int &count) {
    const auto type = g.value("type").toString();
    const auto c = g.value("coordinates").toArray();
    if (type == "LineString") lines(c, name, out, count);
    else if (type == "MultiLineString" || type == "Polygon") {
        for (const auto &path : c) lines(path.toArray(), name, out, count);
    } else if (type == "MultiPolygon") {
        for (const auto &polygon : c)
            for (const auto &ring : polygon.toArray()) lines(ring.toArray(), name, out, count);
    } else if (type == "GeometryCollection") {
        for (const auto &part : g.value("geometries").toArray()) geometry(part.toObject(), name, out, count);
    }
}
QString baseSector(const QString &path, const QString &artcc, const QJsonObject &view) {
    if (!path.isEmpty()) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) throw QString("Cannot read base sector: %1").arg(path);
        // Preserve all non-GEO sections verbatim, including navigation and runway records.
        const QByteArray bytes = f.readAll();
        const QString text = bytes.startsWith("\xEF\xBB\xBF") ? QString::fromUtf8(bytes) : QString::fromLocal8Bit(bytes);
        QStringList out;
        bool skip = false;
        for (const auto &line : text.split('\n')) {
            const QString clean = line.trimmed();
            if (clean.startsWith('[') && clean.endsWith(']')) skip = clean.compare("[GEO]", Qt::CaseInsensitive) == 0;
            if (!skip) out << QString(line).remove('\r');
        }
        return out.join('\n').trimmed() + "\n\n";
    }
    auto center = view.value("Center").toObject();
    if (center.isEmpty()) center = view.value("CurrentPrefSet").toObject().value("DisplayCenter").toObject();
    if (center.isEmpty()) {
        const auto windows = view.value("CurrentPrefSet").toObject().value("Windows").toArray();
        if (!windows.isEmpty()) center = windows.first().toObject().value("Center").toObject();
    }
    const double lat = center.value("Lat").toDouble(35), lon = center.value("Lon").toDouble(-90);
    return QString("; Generated by CRC to EuroScope\n[INFO]\n%1\n%1_CTR\n%1\n%2\n%3\n60\n49\n-1\n1\n\n")
        .arg(artcc, dms(lat, true), dms(lon, false));
}
struct XPlaneNav {
    QString fixes, vors, ndbs, airways, procedures;
    int fixCount = 0, vorCount = 0, ndbCount = 0, airwayCount = 0, procedureCount = 0;
    QSet<QString> regionalIds;
};
struct XPlaneOptions { bool fixes=false, navaids=false, airways=false, procedures=false; bool areaOnly=true; double centerLat=35, centerLon=-90, rangeNm=100; };
bool inNavArea(double lat, double lon, const XPlaneOptions &o) {
    const double latDelta = o.rangeNm / 60.0;
    const double lonDelta = latDelta / std::max(0.25, std::cos(o.centerLat * 3.141592653589793 / 180.0));
    return !o.areaOnly || (lat >= o.centerLat-latDelta && lat <= o.centerLat+latDelta && lon >= o.centerLon-lonDelta && lon <= o.centerLon+lonDelta);
}
QStringList dataLines(const QString &path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).split('\n');
}
XPlaneNav importXPlane(const QString &root, const XPlaneOptions &options) {
    XPlaneNav out;
    const QDir dir(root);
    if (!dir.exists()) return out;
    const QRegularExpression coordinate("^-?\\d+(?:\\.\\d+)?$");
    if (options.fixes) for (const auto &line : dataLines(dir.filePath("earth_fix.dat"))) {
        const auto f = line.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (f.size() < 3 || !coordinate.match(f[0]).hasMatch() || !coordinate.match(f[1]).hasMatch()) continue;
        const double lat = f[0].toDouble(), lon = f[1].toDouble();
        if (lat < -90 || lat > 90 || lon < -180 || lon > 180 || !inNavArea(lat, lon, options)) continue;
        const QString id = f[2]; if (id.isEmpty()) continue;
        out.regionalIds.insert(id);
        out.fixes += id + " " + dms(lat, true) + " " + dms(lon, false) + "\n"; ++out.fixCount;
    }
    if (options.navaids) for (const auto &line : dataLines(dir.filePath("earth_nav.dat"))) {
        const auto f = line.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (f.size() < 8 || !f[0].toInt() || !coordinate.match(f[1]).hasMatch() || !coordinate.match(f[2]).hasMatch()) continue;
        const int type = f[0].toInt(); const double lat=f[1].toDouble(), lon=f[2].toDouble();
        const QString id=f[7]; if (id.isEmpty() || !inNavArea(lat, lon, options)) continue;
        out.regionalIds.insert(id);
        // X-Plane types 4-9 and 14-16 are ILS/LOC/GS/marker/GLS
        // procedures, not standalone VOR/NDB facilities. Never export them
        // into EuroScope's VOR/NDB sections.
        if (type == 3 || type == 12) { out.vors += id + " " + QString::number(f[4].toDouble()/100.0,'f',2) + " " + dms(lat,true) + " " + dms(lon,false) + "\n"; ++out.vorCount; }
        else if (type == 2 || type == 13) { out.ndbs += id + " " + f[4] + " " + dms(lat,true) + " " + dms(lon,false) + "\n"; ++out.ndbCount; }
        else if (type >= 4 && type <= 9) continue;
        else if (type >= 14 && type <= 16) continue;
    }
    if (options.airways) for (const auto &line : dataLines(dir.filePath("earth_awy.dat"))) {
        const auto f=line.trimmed().split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
        if (f.size() < 12) continue;
        const QString route=f.last(), from=f[0], to=f[3];
        if (route.isEmpty() || from.isEmpty() || to.isEmpty()) continue;
        if (options.areaOnly && (!out.regionalIds.contains(from) || !out.regionalIds.contains(to))) continue;
        out.airways += route + " " + from + " " + from + " " + to + " " + to + "\n"; ++out.airwayCount;
    }
    const QDir cifp(dir.filePath("CIFP"));
    if (options.procedures && cifp.exists()) {
        for (const auto &file : cifp.entryInfoList({"*.dat"}, QDir::Files | QDir::Readable)) {
            int sid=0, star=0, app=0;
            for (const auto &line : dataLines(file.absoluteFilePath())) {
                if (line.startsWith("SID:")) ++sid; else if (line.startsWith("STAR:")) ++star; else if (line.startsWith("APPCH:")) ++app;
            }
            if (sid || star || app) { out.procedures += "; " + file.baseName() + " SID=" + QString::number(sid) + " STAR=" + QString::number(star) + " APPROACH=" + QString::number(app) + "\n"; out.procedureCount += sid+star+app; }
        }
    }
    return out;
}
QString addXPlaneNavigation(QString sector, const XPlaneNav &nav) {
    auto insertAfter = [&](const QString &section, const QString &content) {
        if (content.isEmpty()) return;
        const QString marker = "[" + section + "]";
        const int pos = sector.indexOf(marker);
        if (pos < 0) { sector += "\n" + marker + "\n" + content; return; }
        const int end = sector.indexOf(QRegularExpression("\\n\\[[^\\]]+\\]"), pos + marker.size());
        sector.insert(end < 0 ? sector.size() : end, "\n" + content);
    };
    insertAfter("VOR", nav.vors); insertAfter("NDB", nav.ndbs); insertAfter("FIXES", nav.fixes);
    insertAfter("LOW AIRWAY", nav.airways); insertAfter("HIGH AIRWAY", nav.airways);
    if (!nav.procedures.isEmpty()) insertAfter("SIDSSTARS", nav.procedures);
    return sector;
}
QJsonArray swapped(const QJsonValue &v) {
    const auto pair = v.toArray();
    if (pair.size() < 2 || !pair.at(0).isDouble() || !pair.at(1).isDouble()) return {};
    const double lon = pair.at(0).toDouble(), lat = pair.at(1).toDouble();
    if (!std::isfinite(lon) || !std::isfinite(lat) || std::abs(lat) > 90 || std::abs(lon) > 180) return {};
    return QJsonArray{lat, lon};
}
void shapes(const QJsonObject &geometry, QJsonArray &lines, QJsonArray &polygons) {
    const QString type = geometry.value("type").toString();
    const QJsonArray coords = geometry.value("coordinates").toArray();
    auto path = [](const QJsonArray &input, int minimum) {
        QJsonArray output;
        for (const auto &point : input) { auto converted = swapped(point); if (!converted.isEmpty()) output.append(converted); }
        return output.size() >= minimum ? output : QJsonArray();
    };
    if (type == "LineString") { auto p = path(coords, 2); if (!p.isEmpty()) lines.append(p); }
    else if (type == "MultiLineString") {
        for (const auto &value : coords) { auto p = path(value.toArray(), 2); if (!p.isEmpty()) lines.append(p); }
    } else if (type == "Polygon" || type == "MultiPolygon") {
        const QJsonArray groups = type == "Polygon" ? QJsonArray{coords} : coords;
        for (const auto &group : groups)
            for (const auto &ring : group.toArray()) { auto p = path(ring.toArray(), 3); if (!p.isEmpty()) polygons.append(p); }
    } else if (type == "GeometryCollection") {
        for (const auto &part : geometry.value("geometries").toArray()) shapes(part.toObject(), lines, polygons);
    }
}
QJsonArray rgb(const QString &hex) {
    const QColor color(hex);
    return QJsonArray{color.red(), color.green(), color.blue()};
}
void buildMapLayers(const Map &map, QJsonArray &world, QJsonObject &colors, QJsonObject &styles) {
    const auto document = json(map.source);
    struct Group { QJsonArray paths; QString kind, style, color; };
    QMap<QString, Group> groups;
    const auto features = document.value("features").toArray();
    for (const auto &entry : features) {
        const auto feature = entry.toObject();
        const auto properties = feature.value("properties").toObject();
        const QString raw = properties.value("color").toString();
        const QColor color(raw);
        const QString hex = color.isValid() ? color.name().mid(1) : "adc1d7";
        QJsonArray strokes, fills;
        shapes(feature.value("geometry").toObject(), strokes, fills);
        for (const auto &kind : {QString("line"), QString("polygon")}) {
            const QJsonArray paths = kind == "line" ? strokes : fills;
            if (paths.isEmpty()) continue;
            const QString key = kind + "_" + hex;
            auto &group = groups[key]; group.kind = kind; group.color = hex;
            group.style = "crc_" + key;
            for (const auto &p : paths) group.paths.append(p);
        }
    }
    int counter = 0;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const Group &group = it.value();
        const QString colorName = "crc_color_" + group.color;
        colors.insert(colorName, rgb("#" + group.color));
        if (group.kind == "polygon") styles.insert(group.style, QJsonObject{{"fill", colorName}});
        else styles.insert(group.style, QJsonObject{{"line", colorName}, {"line-width", 1.4}});
        world.append(QJsonObject{{"id", "CRC_" + map.id + "_" + QString::number(++counter)},
                                 {"name", map.name + (groups.size() > 1 ? " " + group.kind + " #" + group.color : "")},
                                 {"style", group.style}, {"kind", group.kind}, {"visible", map.selected},
                                 {"lod", QJsonArray{0, 10000}}, {"geom", group.paths}});
    }
}
void installAssets(const QString &folder) {
    const QString assets = projectRoot();
    const QString plugins = QDir(folder).filePath("Plugins");
    if (!QDir().mkpath(QDir(plugins).filePath("JurinasRenderer"))) throw QString("Cannot create Plugins/JurinasRenderer");
    copyRequired(rendererDll(), QDir(plugins).filePath("JurinasRenderer/JurinasRenderer.dll"));
    copyTree(QDir(assets).filePath("TopSky"), QDir(plugins).filePath("TopSky"));
    // This ZME reference shipped with a Japan airspace file; do not apply
    // RJxx altitude rules to a different ARTCC. Keep the rest of its theme.
    write(QDir(plugins).filePath("TopSky/TopSkyAirspace.txt"), "; ScopeBridge: no ARTCC-specific TopSky airspace definitions supplied.\n");
    const QString sym = QDir(assets).filePath("Symbology.txt");
    if (QFileInfo::exists(sym)) {
        QDir().mkpath(QDir(folder).filePath("Settings"));
        copyRequired(sym, QDir(folder).filePath("Settings/Symbology.txt"));
    } else throw QString("EuroScope symbology preset is missing: %1").arg(sym);
    const QString general =
        "; ScopeBridge default radar settings\n"
        "; Transition altitude / level: 18000 ft / FL180\n"
        "m_TransitionAltitude:18000\n"
        "m_TransitionLevel:180\n"
        "; Default one-minute line display\n"
        "m_OneMinuteLine:1\n"
        "m_OneMinuteLineLength:1\n";
    write(QDir(folder).filePath("Settings/General.txt"), general);
}
void voiceAndPositions(const Profile &p, const QString &folder, const QString &stem) {
    const auto root = json(artccFile(p.path, QFileInfo(p.maps.first().source).dir().absolutePath(), p.artcc))
                          .value("facility").toObject();
    QStringList records, voices, profiles;
    auto center = p.view.value("Center").toObject();
    if (center.isEmpty()) center = p.view.value("CurrentPrefSet").toObject().value("DisplayCenter").toObject();
    if (center.isEmpty()) {
        const auto windows = p.view.value("CurrentPrefSet").toObject().value("Windows").toArray();
        if (!windows.isEmpty()) center = windows.first().toObject().value("Center").toObject();
    }
    const QString lat = dms(center.value("Lat").toDouble(35), true);
    const QString lon = dms(center.value("Lon").toDouble(-90), false);
    auto recurse = [&](const auto &self, const QJsonObject &f) -> void {
        for (const auto &entry : f.value("positions").toArray()) {
            const auto pos = entry.toObject();
            const QString callsign = pos.value("callsign").toString();
            const double hz = pos.value("frequency").toDouble();
            if (callsign.isEmpty() || hz <= 0) continue;
            const QString frequency = QString::number(hz / 1000000.0, 'f', 3);
            const QString name = pos.value("name").toString(f.value("name").toString(callsign));
            const QStringList parts = callsign.split('_');
            const QString type = parts.isEmpty() ? "CTR" : parts.last();
            const QString airport = parts.isEmpty() ? p.artcc : parts.first();
            const QString sector = parts.size() > 2 ? parts.at(1) : "-";
            records << QString("%1:%2:%3:SB%4:%5:%6:%7:-:-:2000:2077:%8:%9")
                           .arg(callsign, name, frequency, QString::number(records.size(), 36).toUpper(), sector, airport, type, lat, lon);
            voices << "AG:" + callsign + ":" + frequency;
            profiles << "PROFILE:" + callsign + ":600:6" << "ATIS2:" + name;
        }
        for (const auto &child : f.value("childFacilities").toArray()) self(self, child.toObject());
    };
    recurse(recurse, root);
    write(QDir(folder).filePath(stem + ".ese"), "[FREETEXT]\n\n[POSITIONS]\n" + records.join('\n') + "\n\n[SIDSSTARS]\n\n[AIRSPACE]\n");
    write(QDir(folder).filePath("Settings/Voice.txt"), "VOICE\n" + voices.join('\n') + "\n");
    write(QDir(folder).filePath("Settings/Profile.txt"), "PROFILE\n" + profiles.join('\n') + "\n");
}
struct Result { int maps = 0, segments = 0; QStringList warnings; };
Result convert(const Profile &p, const QString &dest, const QString &sector, const QString &xplane = {}, const XPlaneOptions &navOptions = {}) {
    if (p.maps.isEmpty()) throw QString("No maps are available for %1. Check the selected display and CRC data.").arg(p.name);
    const QString folder = QDir(dest).filePath(safe(p.artcc + "_" + p.name));
    if (!QDir().mkpath(folder)) throw QString("Cannot create %1").arg(folder);
    const QString stem = safe(p.artcc + "_" + p.name);
    Result result; result.warnings = p.warnings;
    if (sector.isEmpty()) result.warnings << "No legacy sector found: this package contains CRC video maps but no navigation or runway data.";
    installAssets(folder);
    voiceAndPositions(p, folder, stem);
    QJsonArray world;
    QJsonObject colors, styles;
    QStringList activeLayerIds;
    for (int i = 0; i < p.maps.size(); ++i) {
        const auto &m = p.maps.at(i);
        const int before = world.size();
        buildMapLayers(m, world, colors, styles);
        if (world.size() == before) {
            if (m.selected) result.warnings << "Map contains only unsupported point/text features: " + m.name;
        } else {
            if (m.selected) ++result.maps;
            for (int j = before; j < world.size(); ++j) {
                result.segments += world.at(j).toObject().value("geom").toArray().size();
                if (m.selected) activeLayerIds << world.at(j).toObject().value("id").toString();
            }
        }
    }
    if (world.isEmpty()) throw QString("None of the available maps contains drawable geometry.");
    if (activeLayerIds.isEmpty()) result.warnings << "Selected maps have no drawable lines or polygons; all available layers start hidden.";
    const QString rendererDir = QDir(folder).filePath("Plugins/JurinasRenderer");
    const QJsonObject groundData{{"version", 1}, {"unit", "deg"}, {"lod_unit", "nm_view_width"},
                             {"airports", QJsonObject{}}, {"world", world}};
    const QJsonObject style{{"version", 2}, {"colors", colors}, {"layers", styles}};
    write(QDir(rendererDir).filePath("ground.json"), QString::fromUtf8(QJsonDocument(groundData).toJson(QJsonDocument::Compact)));
    write(QDir(rendererDir).filePath("style.json"), QString::fromUtf8(QJsonDocument(style).toJson(QJsonDocument::Indented)));
    QString foundation = baseSector(sector, p.artcc, p.view);
    if (!xplane.isEmpty() && (navOptions.fixes || navOptions.navaids || navOptions.airways || navOptions.procedures)) {
        XPlaneOptions regional = navOptions;
        auto navCenter = p.view.value("Center").toObject();
        if (navCenter.isEmpty()) navCenter = p.view.value("CurrentPrefSet").toObject().value("DisplayCenter").toObject();
        if (navCenter.isEmpty()) {
            const auto windows = p.view.value("CurrentPrefSet").toObject().value("Windows").toArray();
            if (!windows.isEmpty()) navCenter = windows.first().toObject().value("Center").toObject();
        }
        regional.centerLat = navCenter.value("Lat").toDouble(35);
        regional.centerLon = navCenter.value("Lon").toDouble(-90);
        double viewRange = p.view.value("Range").toDouble(p.view.value("CurrentPrefSet").toObject().value("Range").toDouble(100));
        if (!std::isfinite(viewRange) || viewRange <= 0) viewRange = 100;
        regional.rangeNm = qBound(80.0, viewRange * 2.0, 500.0);
        foundation = addXPlaneNavigation(foundation, importXPlane(xplane, regional));
    }
    write(QDir(folder).filePath(stem + ".sct"), foundation);
    const bool ground = p.display.contains("Cab", Qt::CaseInsensitive) || p.display.contains("Asdex", Qt::CaseInsensitive) || p.display.contains("Said", Qt::CaseInsensitive);
    auto center = p.view.value("Center").toObject();
    if (center.isEmpty()) center = p.view.value("CurrentPrefSet").toObject().value("DisplayCenter").toObject();
    if (center.isEmpty()) {
        const auto windows = p.view.value("CurrentPrefSet").toObject().value("Windows").toArray();
        if (!windows.isEmpty()) center = windows.first().toObject().value("Center").toObject();
    }
    const double lat = center.value("Lat").toDouble(35), lon = center.value("Lon").toDouble(-90);
    double range = p.view.value("Range").toDouble(p.view.value("CurrentPrefSet").toObject().value("Range").toDouble(100));
    if (range == 100 && p.view.value("Range").isUndefined()) {
        const auto windows = p.view.value("CurrentPrefSet").toObject().value("Windows").toArray();
        if (!windows.isEmpty()) range = windows.first().toObject().value("Range").toDouble(100);
    }
    if (!std::isfinite(range) || range <= 0) range = 100;
    const double delta = qBound(0.02, range / 60.0, 60.0), lonDelta = delta / qMax(0.25, std::cos(lat * 3.141592653589793 / 180));
    QString asr = QString("DisplayTypeName:%1\nDisplayTypeNeedRadarContent:%2\nDisplayTypeGeoReferenced:1\nSECTORFILE:\nSECTORTITLE:%3.sct\nSHOWC:1\nSHOWSB:1\n")
        .arg(ground ? "Ground Radar display" : "Standard ES radar screen").arg(ground ? 0 : 1).arg(stem);
    for (const auto &layer : world) {
        const auto entry = layer.toObject();
        asr += "PLUGIN:Jurina's Renderer:JurinaRender_" + entry.value("id").toString()
             + (entry.value("visible").toBool() ? ":1\n" : ":0\n");
    }
    asr += QString("WINDOWAREA:%1:%2:%3:%4\n")
        .arg(lat - delta, 0, 'f', 6).arg(lon - lonDelta, 0, 'f', 6)
        .arg(lat + delta, 0, 'f', 6).arg(lon + lonDelta, 0, 'f', 6);
    write(QDir(folder).filePath(stem + ".asr"), asr);
    write(QDir(folder).filePath(stem + ".prf"),
          "Settings\tsector\t\\" + stem + ".sct\n"
          "Settings\tSettingsfileVOICE\t\\Settings\\Voice.txt\n"
          "Settings\tSettingsfilePROFILE\t\\Settings\\Profile.txt\n"
          "Settings\tSettingsfile\t\\Settings\\General.txt\n"
          "Plugins\tPlugin0\t\\Plugins\\JurinasRenderer\\JurinasRenderer.dll\n"
          "Plugins\tPlugin0Display0\tStandard ES radar screen\n"
          "Plugins\tPlugin0Display1\tGround Radar display\n"
          "Plugins\tPlugin0Display2\tGround Map\n"
          "Plugins\tPlugin1\t\\Plugins\\TopSky\\TopSky.dll\n"
          "Plugins\tPlugin1Display0\tStandard ES radar screen\n"
          "Plugins\tPlugin1Display1\tGround Radar display\n"
          "Settings\tSettingsfileSYMBOLOGY\t\\Settings\\Symbology.txt\n"
          "ASRFastKeys\t1\t\\" + stem + ".asr\nRecentFiles\tRecent1\t\\" + stem + ".asr\n");
    return result;
}

// ---- EuroScope (.sct/.ese) to VRC (.sct2) ----
// VRC reads the classic SCT2 layout. EuroScope extends it with named GEO
// lines, REGIONNAME headers, inline numeric colors, fix-name coordinates in
// any section, UTF-8 text and the separate .ese file; all of those are
// normalized here so VRC can load the result.
struct VrcOptions { bool freetext = true, sectorLines = true, positions = true; };
struct VrcResult { QString sector, pof; QStringList summary, warnings; };
const QStringList vrcSections = {"INFO", "VOR", "NDB", "AIRPORT", "RUNWAY", "FIXES", "ARTCC", "ARTCC HIGH", "ARTCC LOW",
                                 "SID", "STAR", "LOW AIRWAY", "HIGH AIRWAY", "GEO", "REGIONS", "LABELS"};
QString readSectorText(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) throw QString("Cannot read %1: %2").arg(path, f.errorString());
    const QByteArray bytes = f.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF")) return QString::fromUtf8(bytes.mid(3));
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8.decode(bytes);
    // Only ASCII survives into the VRC file, so a legacy code page does not
    // need to be identified; Latin-1 keeps every byte at a single character.
    return utf8.hasError() ? QString::fromLatin1(bytes) : text;
}
// VRC reads sector files as ANSI. Non-ASCII text is replaced in data and
// dropped from comments.
QString vrcAscii(const QString &text, bool replace) {
    QString out; out.reserve(text.size());
    for (const QChar c : text) {
        if (c == '\t') out += ' ';
        else if (c.unicode() >= 32 && c.unicode() < 127) out += c;
        else if (replace) out += '?';
    }
    return out;
}
const QRegularExpression &dmsPattern() {
    static const QRegularExpression re("^([NSEW])(\\d{1,3})\\.(\\d{1,2})\\.(\\d{1,2}(?:\\.\\d*)?)$",
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}
bool isDms(const QString &token) { return dmsPattern().match(token).hasMatch(); }
bool parseDms(const QString &token, bool latitude, double &value) {
    const auto m = dmsPattern().match(token);
    if (!m.hasMatch()) return false;
    const QChar hemisphere = m.captured(1).at(0).toUpper();
    if (latitude != (hemisphere == 'N' || hemisphere == 'S')) return false;
    QString seconds = m.captured(4); if (seconds.endsWith('.')) seconds.chop(1);
    double v = m.captured(2).toInt() + m.captured(3).toInt() / 60.0 + seconds.toDouble() / 3600.0;
    if (hemisphere == 'S' || hemisphere == 'W') v = -v;
    if (std::abs(v) > (latitude ? 90 : 180)) return false;
    value = v;
    return true;
}
QPair<QString, QString> splitComment(const QString &line) {
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        if (line.at(i) == '"') quoted = !quoted;
        else if (line.at(i) == ';' && !quoted) return {line.left(i), line.mid(i + 1)};
    }
    return {line, {}};
}
QStringList tokens(const QString &text) {
    static const QRegularExpression space("\\s+");
    return text.split(space, Qt::SkipEmptyParts);
}
struct EsToVrc {
    struct NavPoint { double lat = 0, lon = 0; };
    struct Segment { QString name, color; QStringList raw; NavPoint a, b; };
    QStringList header, defineOrder, unknownSections, warnings, positions;
    QHash<QString, QString> defineName, defineValue;    // keyed by lower-case color name
    QHash<QString, QStringList> input, output;
    QHash<QString, NavPoint> nav;                       // VOR/NDB/AIRPORT/FIXES by upper-case name
    QSet<QString> missingNames, undefinedColors;
    int dropped = 0;
    bool truncatedNames = false;

    static QString text(const NavPoint &p) { return dms(p.lat, true) + " " + dms(p.lon, false); }
    static const QHash<QString, QString> &standardColors() {
        static const QHash<QString, QString> colors = {
            {"aqua", "16776960"}, {"black", "0"}, {"blue", "16711680"}, {"fuchsia", "16711935"},
            {"gray", "8421504"}, {"green", "32768"}, {"lime", "65280"}, {"maroon", "128"},
            {"navy", "8388608"}, {"olive", "32896"}, {"purple", "8388736"}, {"red", "255"},
            {"silver", "12632256"}, {"teal", "8421376"}, {"white", "16777215"}, {"yellow", "65535"}};
        return colors;
    }
    static bool numeric(const QString &t) { static const QRegularExpression re("^\\d+$"); return re.match(t).hasMatch(); }
    void addDefine(const QString &name, const QString &value) {
        const QString key = name.toLower();
        if (!defineName.contains(key)) defineOrder << key;
        defineName.insert(key, name); defineValue.insert(key, value);
    }
    bool isColor(const QString &t) const {
        const QString key = t.toLower();
        return defineName.contains(key) || numeric(t) || standardColors().contains(key);
    }
    // Returns a #define name VRC understands, creating one when EuroScope
    // used a raw number or an undefined name.
    QString color(const QString &token) {
        const QString key = token.toLower();
        if (defineName.contains(key)) return defineName.value(key);
        if (numeric(token)) { addDefine("COLOR-" + token, token); return "COLOR-" + token; }
        if (standardColors().contains(key)) { addDefine(token.toUpper(), standardColors().value(key)); return token.toUpper(); }
        undefinedColors.insert(token);
        const QString name = vrcAscii(token, true);
        addDefine(name, "16777215");
        return name;
    }
    bool resolve(const QString &latToken, const QString &lonToken, NavPoint &p) const {
        const bool latDms = isDms(latToken), lonDms = isDms(lonToken);
        if (latDms != lonDms) return false;
        if (latDms) return parseDms(latToken, true, p.lat) && parseDms(lonToken, false, p.lon);
        // EuroScope repeats a fix/navaid/airport name in both coordinate slots.
        if (latToken.compare(lonToken, Qt::CaseInsensitive) != 0) return false;
        const auto it = nav.constFind(latToken.toUpper());
        if (it == nav.cend()) return false;
        p = *it;
        return true;
    }
    void noteMissing(const QStringList &t) {
        for (const auto &token : t.mid(qMax(0, t.size() - 5)))
            if (!isDms(token) && !nav.contains(token.toUpper()) && !isColor(token)) missingNames.insert(token);
    }
    // name? lat lon lat lon color? -- the name may contain spaces.
    bool segment(const QStringList &t, Segment &s) const {
        const bool colorFirst = t.size() >= 5 && !isDms(t.last()) && (isColor(t.last()) || !nav.contains(t.last().toUpper()));
        for (const int strip : colorFirst ? QList<int>{1, 0} : QList<int>{0, 1}) {
            if (strip && (t.size() < 5 || isDms(t.last()))) continue;
            const int n = t.size() - strip;
            if (n < 4) continue;
            NavPoint a, b;
            if (!resolve(t[n - 4], t[n - 3], a) || !resolve(t[n - 2], t[n - 1], b)) continue;
            s.name = t.mid(0, n - 4).join(' ');
            s.color = strip ? t.last() : QString();
            s.raw = t.mid(n - 4, 4); s.a = a; s.b = b;
            return true;
        }
        return false;
    }
    void comment(const QString &section, const QString &note) {
        const QString clean = vrcAscii(note, false).trimmed();
        if (!clean.isEmpty()) output[section] << "; " + clean;
    }
    static QString trailing(const QString &note) {
        const QString clean = vrcAscii(note, false).trimmed();
        return clean.isEmpty() ? QString() : " ;" + clean;
    }

    void load(const QString &source) {
        static const QRegularExpression define("^#define\\s+(\\S+)\\s+(\\S+)", QRegularExpression::CaseInsensitiveOption);
        QString section;
        bool started = false;
        for (QString line : source.split('\n')) {
            line.remove('\r');
            const QString clean = line.trimmed();
            if (clean.startsWith("#define", Qt::CaseInsensitive)) {
                const auto m = define.match(clean);
                const QString value = m.captured(2).section(';', 0, 0);
                if (m.hasMatch() && numeric(value)) addDefine(vrcAscii(m.captured(1), true), value);
                else warnings << "Ignored malformed color definition: " + vrcAscii(clean, true);
                continue;
            }
            if (clean.startsWith('[') && clean.indexOf(']') > 1) {
                QString name = clean.mid(1, clean.indexOf(']') - 1).trimmed().toUpper();
                name.replace(QRegularExpression("\\s+"), " ");
                section = vrcSections.contains(name) ? name : QString();
                if (section.isEmpty() && name != "DEFINE") unknownSections << name;
                started = true;
                continue;
            }
            if (!started) { if (clean.startsWith(';')) header << line; continue; }
            if (!section.isEmpty()) input[section] << line;
        }
        if (!input.contains("INFO")) warnings << "The source has no [INFO] section; default values were written.";
        for (const QPair<QString, int> &source : QList<QPair<QString, int>>{{"VOR", 2}, {"NDB", 2}, {"AIRPORT", 2}, {"FIXES", 1}}) {
            for (const auto &line : input.value(source.first)) {
                const QStringList t = tokens(splitComment(line).first);
                const int i = source.second;
                NavPoint p;
                if (t.size() >= i + 2 && parseDms(t[i], true, p.lat) && parseDms(t[i + 1], false, p.lon)
                    && !nav.contains(t[0].toUpper()))
                    nav.insert(t[0].toUpper(), p);
            }
        }
    }
    void convertInfo() {
        QStringList values;
        for (const auto &line : input.value("INFO")) {
            const QString content = splitComment(line).first.trimmed();
            if (!content.isEmpty()) values << content;
        }
        const QStringList defaults = {"ScopeBridge VRC sector", "OBS", "ZZZZ", "N000.00.00.000", "E000.00.00.000", "60", "60", "0", "1"};
        if (!values.isEmpty() && values.size() < 9) warnings << QString("[INFO] has %1 of 9 lines; missing lines use defaults.").arg(values.size());
        for (int i = 0; i < defaults.size(); ++i) {
            QString value = i < values.size() ? values.at(i) : defaults.at(i);
            if (i == 3 || i == 4) {
                NavPoint p; double v = 0;
                if (parseDms(value, i == 3, v)) value = dms(v, i == 3);
                else if (resolve(value, value, p)) value = dms(i == 3 ? p.lat : p.lon, i == 3);
            }
            output["INFO"] << vrcAscii(value, true);
        }
    }
    void convertNavigation(const QString &section) {
        const int index = section == "FIXES" ? 1 : section == "RUNWAY" ? 4 : 2;
        for (const auto &line : input.value(section)) {
            const auto [content, note] = splitComment(line);
            const QStringList t = tokens(content);
            if (t.isEmpty()) { comment(section, note); continue; }
            NavPoint p, q;
            const bool runway = section == "RUNWAY";
            if (t.size() < index + (runway ? 4 : 2) || !resolve(t[index], t[index + 1], p)
                || (runway && !resolve(t[index + 2], t[index + 3], q))) {
                noteMissing(t); ++dropped; continue;
            }
            QStringList fields = t.mid(0, index);
            if (runway) { fields[0] = fields[0].leftJustified(3); fields[1] = fields[1].leftJustified(3); }
            fields << text(p);
            if (runway) fields << text(q);
            fields << t.mid(index + (runway ? 4 : 2));
            if (section == "AIRPORT" && t.size() < 5) fields << "D";   // VRC expects an airspace class
            output[section] << vrcAscii(fields.join(' '), true) + trailing(note);
        }
    }
    void convertLines(const QString &section) {
        const bool geo = section == "GEO", artcc = section.startsWith("ARTCC");
        QString current;
        for (const auto &line : input.value(section)) {
            const auto [content, note] = splitComment(line);
            const QStringList t = tokens(content);
            if (t.isEmpty()) { comment(section, note); continue; }
            Segment s;
            if (!segment(t, s)) { noteMissing(t); ++dropped; continue; }
            if (geo) {
                // VRC GEO lines carry no name; keep EuroScope's name as a group comment.
                if (!s.name.isEmpty() && s.name != current) { current = s.name; comment(section, current); }
                output[section] << text(s.a) + " " + text(s.b) + (s.color.isEmpty() ? QString() : " " + color(s.color)) + trailing(note);
                continue;
            }
            if (!s.name.isEmpty()) current = vrcAscii(s.name, true).replace(' ', '_');
            const QString name = current.isEmpty() ? (artcc ? "ARTCC" : "AIRWAY") : current;
            // VRC colors boundaries and airways itself; EuroScope colors are dropped.
            output[section] << name.leftJustified(13) + " " + text(s.a) + " " + text(s.b) + trailing(note);
        }
    }
    void convertDiagrams(const QString &section) {
        QString current;
        bool pending = false;
        for (const auto &line : input.value(section)) {
            const auto [content, note] = splitComment(line);
            const QStringList t = tokens(content);
            if (t.isEmpty()) { comment(section, note); continue; }
            Segment s;
            if (!segment(t, s)) {
                // EuroScope allows a diagram name on a line of its own.
                if (!content.at(0).isSpace() && t.size() < 4 && std::none_of(t.cbegin(), t.cend(), isDms)) {
                    current = vrcAscii(t.join(' '), true); pending = true;
                } else { noteMissing(t); ++dropped; }
                continue;
            }
            if (!s.name.isEmpty()) { current = vrcAscii(s.name, true); pending = true; }
            QString prefix(26, ' ');
            if (pending) {
                // VRC reads the diagram name from the first 26 columns.
                QString name = current.isEmpty() ? QString("UNNAMED") : current;
                if (name.size() > 25) { name.truncate(25); truncatedNames = true; }
                prefix = name.leftJustified(26);
                pending = false;
            }
            QStringList points;
            for (const int i : {0, 2}) {
                const NavPoint &p = i == 0 ? s.a : s.b;
                if (isDms(s.raw[i])) points << dms(p.lat, true).leftJustified(14) + " " + dms(p.lon, false).leftJustified(14);
                else points << s.raw[i].toUpper().leftJustified(14) + " " + s.raw[i + 1].toUpper().leftJustified(14);
            }
            QString out = prefix + points.join(' ');
            while (out.endsWith(' ')) out.chop(1);
            if (!s.color.isEmpty()) out += " " + color(s.color);
            output[section] << out + trailing(note);
        }
    }
    void convertRegions() {
        QString pendingColor;
        for (const auto &line : input.value("REGIONS")) {
            const auto [content, note] = splitComment(line);
            const QStringList t = tokens(content);
            if (t.isEmpty()) { comment("REGIONS", note); continue; }
            if (t.first().compare("REGIONNAME", Qt::CaseInsensitive) == 0) { comment("REGIONS", t.mid(1).join(' ')); continue; }
            NavPoint p;
            if (t.size() >= 3) {
                pendingColor = color(t[0]);
                if (!resolve(t[1], t[2], p)) { noteMissing(t); ++dropped; continue; }
            } else if (t.size() != 2 || !resolve(t[0], t[1], p)) { noteMissing(t); ++dropped; continue; }
            if (!pendingColor.isEmpty()) { output["REGIONS"] << pendingColor.leftJustified(9) + " " + text(p); pendingColor.clear(); }
            else output["REGIONS"] << QString(10, ' ') + text(p);
        }
    }
    void convertLabels() {
        static const QRegularExpression label("^\\s*\"([^\"]*)\"\\s*(.*)$");
        for (const auto &line : input.value("LABELS")) {
            const auto [content, note] = splitComment(line);
            if (content.trimmed().isEmpty()) { comment("LABELS", note); continue; }
            const auto m = label.match(content);
            const QStringList t = tokens(m.captured(2));
            NavPoint p;
            if (!m.hasMatch() || t.size() < 2 || !resolve(t[0], t[1], p)) { noteMissing(t); ++dropped; continue; }
            output["LABELS"] << QString("\"%1\" %2 %3").arg(vrcAscii(m.captured(1), true), text(p), color(t.size() >= 3 ? t[2] : "WHITE"))
                                + trailing(note);
        }
    }
    void convert() {
        convertInfo();
        for (const QString &section : {"VOR", "NDB", "AIRPORT", "RUNWAY", "FIXES"}) convertNavigation(section);
        for (const QString &section : {"ARTCC", "ARTCC HIGH", "ARTCC LOW", "LOW AIRWAY", "HIGH AIRWAY", "GEO"}) convertLines(section);
        convertDiagrams("SID"); convertDiagrams("STAR");
        convertRegions(); convertLabels();
    }
    // .ese: FREETEXT becomes LABELS, SECTORLINE/CIRCLE_SECTORLINE become
    // ARTCC LOW boundaries and POSITIONS is written as a VRC .pof file.
    void loadExtension(const QString &source, const VrcOptions &options) {
        QString section, lineId, group;
        QList<NavPoint> line;
        auto flush = [&] {
            const QString name = "ESE_" + vrcAscii(lineId, true).replace(' ', '_');
            for (int i = 1; i < line.size(); ++i)
                output["ARTCC LOW"] << name.leftJustified(13) + " " + text(line.at(i - 1)) + " " + text(line.at(i));
            line.clear();
        };
        for (QString raw : source.split('\n')) {
            raw.remove('\r');
            const QString clean = raw.trimmed();
            if (clean.startsWith('[') && clean.indexOf(']') > 1) {
                flush(); lineId.clear();
                section = clean.mid(1, clean.indexOf(']') - 1).trimmed().toUpper();
                continue;
            }
            if (clean.isEmpty() || clean.startsWith(';')) continue;
            const QStringList parts = clean.split(':');
            NavPoint p;
            if (section == "FREETEXT" && options.freetext) {
                if (parts.size() < 4 || !resolve(parts[0].trimmed(), parts[1].trimmed(), p)) { ++dropped; continue; }
                if (parts[2] != group) { group = parts[2]; comment("LABELS", "FREETEXT " + group); }
                if (!defineName.contains("freetext-c")) addDefine("FREETEXT-C", "12632256");
                const QString textValue = vrcAscii(parts.mid(3).join(':'), true).remove('"');
                output["LABELS"] << QString("\"%1\" %2 FREETEXT-C").arg(textValue, text(p));
            } else if (section == "POSITIONS" && options.positions) {
                positions << vrcAscii(clean, true);
            } else if (section == "AIRSPACE" && options.sectorLines) {
                const QString key = parts[0].trimmed().toUpper();
                if (key == "SECTORLINE") { flush(); lineId = parts.value(1).trimmed(); }
                else if (key == "COORD" && !lineId.isEmpty()) {
                    if (resolve(parts.value(1).trimmed(), parts.value(2).trimmed(), p)) line << p; else ++dropped;
                } else if (key == "CIRCLE_SECTORLINE") {
                    flush(); lineId = parts.value(1).trimmed();
                    NavPoint c; bool ok = false; double radius = 0;
                    if (parts.size() >= 5 && resolve(parts[2].trimmed(), parts[3].trimmed(), c)) radius = parts[4].toDouble(&ok);
                    else if (parts.size() >= 4 && resolve(parts[2].trimmed(), parts[2].trimmed(), c)) radius = parts[3].toDouble(&ok);
                    if (ok && radius > 0) {
                        const double scale = std::max(0.01, std::cos(c.lat * 3.141592653589793 / 180.0));
                        for (int k = 0; k <= 72; ++k) {
                            const double angle = k * 2 * 3.141592653589793 / 72;
                            line << NavPoint{c.lat + radius / 60.0 * std::cos(angle), c.lon + radius / 60.0 * std::sin(angle) / scale};
                        }
                    } else ++dropped;
                    flush(); lineId.clear();
                } else if (key == "SECTOR") { flush(); lineId.clear(); }
            }
        }
        flush();
    }
    QString render(const QString &sourceName) const {
        QStringList out{"; VRC sector converted from EuroScope by ScopeBridge " SCOPEBRIDGE_VERSION,
                        "; Source: " + vrcAscii(sourceName, true)};
        for (const auto &line : header) {
            const QString clean = vrcAscii(line, false).trimmed();
            if (clean.size() > 1) out << clean;
        }
        out << "";
        for (const auto &key : defineOrder) out << "#define " + defineName.value(key) + " " + defineValue.value(key);
        out << "";
        for (const auto &section : vrcSections) out << "[" + section + "]" << output.value(section) << "";
        return out.join("\r\n");
    }
};
VrcResult esToVrc(const QString &sectorPath, const QString &extensionPath, const QString &destination, const VrcOptions &options = {}) {
    if (sectorPath.trimmed().isEmpty()) throw QString("Choose a EuroScope .sct file first.");
    if (!QFileInfo(sectorPath).isFile()) throw QString("EuroScope sector was not found: %1").arg(sectorPath);
    if (!extensionPath.trimmed().isEmpty() && !QFileInfo(extensionPath).isFile())
        throw QString("EuroScope extension (.ese) was not found: %1").arg(extensionPath);
    if (destination.trimmed().isEmpty()) throw QString("Choose an output folder for the VRC sector.");
    EsToVrc converter;
    converter.load(readSectorText(sectorPath));
    converter.convert();
    if (!extensionPath.trimmed().isEmpty()) converter.loadExtension(readSectorText(extensionPath), options);
    if (!QDir().mkpath(destination)) throw QString("Cannot create %1").arg(destination);
    VrcResult result;
    const QString stem = safe(QFileInfo(sectorPath).completeBaseName());
    result.sector = QDir(destination).absoluteFilePath(stem + ".sct2");
    for (const QString &input : {sectorPath, extensionPath})
        if (!input.isEmpty() && QFileInfo(input).absoluteFilePath().compare(result.sector, Qt::CaseInsensitive) == 0)
            throw QString("The output would overwrite the source file. Choose a different output folder.");
    write(result.sector, converter.render(QFileInfo(sectorPath).fileName()));
    if (!converter.positions.isEmpty()) {
        result.pof = QDir(destination).absoluteFilePath(stem + ".pof");
        write(result.pof, "; VRC positions converted from " + vrcAscii(QFileInfo(extensionPath).fileName(), true)
                              + " by ScopeBridge\r\n" + converter.positions.join("\r\n") + "\r\n");
    }
    for (const auto &section : vrcSections) {
        const auto lines = converter.output.value(section);
        const int count = std::count_if(lines.cbegin(), lines.cend(), [](const QString &l) { return !l.startsWith(';'); });
        if (count && section != "INFO") result.summary << QString("[%1] %2").arg(section).arg(count);
    }
    if (!converter.positions.isEmpty()) result.summary << QString("POF positions %1").arg(converter.positions.size());
    result.warnings = converter.warnings;
    if (converter.dropped) result.warnings << QString("%1 line(s) could not be converted and were skipped.").arg(converter.dropped);
    if (!converter.missingNames.isEmpty()) {
        QStringList names(converter.missingNames.cbegin(), converter.missingNames.cend());
        names.sort();
        result.warnings << "Unresolved fix/navaid names: " + vrcAscii(names.mid(0, 15).join(", "), true)
                               + (names.size() > 15 ? QString(" (+%1 more)").arg(names.size() - 15) : QString());
    }
    if (!converter.undefinedColors.isEmpty()) {
        QStringList names(converter.undefinedColors.cbegin(), converter.undefinedColors.cend());
        names.sort();
        result.warnings << "Undefined colors were defined as white: " + vrcAscii(names.join(", "), true);
    }
    if (converter.truncatedNames) result.warnings << "Some SID/STAR names were longer than 25 characters and were truncated for VRC.";
    if (!converter.unknownSections.isEmpty()) {
        converter.unknownSections.removeDuplicates();
        result.warnings << "Sections not supported by VRC were skipped: " + vrcAscii(converter.unknownSections.join(", "), true);
    }
    return result;
}
}

int main(int argc, char **argv) {
    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == "--list-profiles") {
        QCoreApplication cli(argc, argv);
        try {
            QStringList errors;
            const auto entries = scanProfiles(QString::fromLocal8Bit(argv[2]), errors);
            for (const auto &entry : entries) QTextStream(stdout) << entry.name << "\t" << entry.artcc << "\n";
            for (const auto &error : errors) QTextStream(stderr) << error << "\n";
            return 0;
        } catch (const QString &e) { QTextStream(stderr) << e << "\n"; return 1; }
    }
    if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == "--es-to-vrc") {
        QCoreApplication cli(argc, argv);
        try {
            const QString sector = QString::fromLocal8Bit(argv[2]);
            QString extension = argc >= 5 ? QString::fromLocal8Bit(argv[4]) : QString();
            if (extension.isEmpty()) {
                const QFileInfo info(sector);
                const QString sibling = info.dir().filePath(info.completeBaseName() + ".ese");
                if (QFileInfo::exists(sibling)) extension = sibling;
            }
            const auto result = esToVrc(sector, extension, QString::fromLocal8Bit(argv[3]));
            QTextStream(stdout) << result.sector << "\n" << (result.pof.isEmpty() ? QString() : result.pof + "\n")
                                << result.summary.join(", ") << "\n";
            for (const auto &warning : result.warnings) QTextStream(stdout) << "Warning: " << warning << "\n";
            return 0;
        } catch (const QString &error) {
            QTextStream(stderr) << error << "\n";
            return 1;
        }
    }
    if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == "--convert") {
        QCoreApplication cli(argc, argv);
        try {
            const QString file = QString::fromLocal8Bit(argv[2]);
            const QString destination = QString::fromLocal8Bit(argv[3]);
            const QString local = qEnvironmentVariable("LOCALAPPDATA");
            const QString maps = argc >= 5 ? QString::fromLocal8Bit(argv[4]) : local + "/CRC/VideoMaps";
            const QString base = argc >= 6 ? QString::fromLocal8Bit(argv[5]) : QString();
            const QString xplane = argc >= 7 ? QString::fromLocal8Bit(argv[6]) : QString();
            const auto p = inspect(file, maps);
            const auto result = convert(p, destination, base, xplane);
            QTextStream(stdout) << p.name << ": " << result.maps << " maps, " << result.segments << " segments\n";
            for (const auto &warning : result.warnings) QTextStream(stdout) << "Warning: " << warning << "\n";
            return 0;
        } catch (const QString &error) {
            QTextStream(stderr) << error << "\n";
            return 1;
        }
    }
    QApplication app(argc, argv);
    app.setApplicationName("ScopeBridge");
    app.setApplicationVersion(SCOPEBRIDGE_VERSION);
    auto *style = new oclero::qlementine::QlementineStyle(&app);
    QApplication::setStyle(style);
    style->setThemeJsonPath(":/scopebridge/dark.json");
    QFont interfaceFont("SF Pro Display");
    if (!QFontDatabase().families().contains("SF Pro Display")) interfaceFont.setFamily("Segoe UI");
    interfaceFont.setPointSize(10);
    app.setFont(interfaceFont);
    app.setStyleSheet(R"(
        QLabel#title { font-size:28px; font-weight:700; }
        QLabel#eyebrow { font-size:11px; font-weight:700; color:#78baff; }
        QLabel#hint { color:#9baec4; }
        QLabel#version { color:#9baec4; font-size:12px; }
        QFrame#card { border:1px solid #394455; border-radius:14px; }
    )");
    QWidget window;
    window.setWindowTitle("ScopeBridge " SCOPEBRIDGE_VERSION);
    window.setMinimumSize(980, 760);
    window.resize(1240, 900);
    auto *root = new QVBoxLayout(&window); root->setContentsMargins(28, 24, 28, 22); root->setSpacing(16);
    auto *eyebrow = new QLabel("CRC  /  EUROSCOPE"); eyebrow->setObjectName("eyebrow"); root->addWidget(eyebrow);
    auto *heading = new QHBoxLayout;
    auto *title = new QLabel("ScopeBridge"); title->setObjectName("title"); heading->addWidget(title);
    heading->addStretch();
    auto *version = new QLabel("VERSION " SCOPEBRIDGE_VERSION); version->setObjectName("version"); heading->addWidget(version);
    root->addLayout(heading);
    auto *hint = new QLabel("Build a renderer-ready EuroScope package from a saved CRC display."); hint->setObjectName("hint"); root->addWidget(hint);
    auto *mode = new QTabBar;
    mode->addTab("CRC  →  EuroScope");
    mode->addTab("EuroScope  →  VRC");
    mode->setExpanding(false);
    mode->setDrawBase(false);
    root->addWidget(mode);
    auto *pages = new QStackedWidget; root->addWidget(pages, 1);
    auto *crcPage = new QWidget; pages->addWidget(crcPage);
    auto *layout = new QVBoxLayout(crcPage); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(16);
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    auto *sources = new QFrame; sources->setObjectName("card");
    auto *sourceLayout = new QVBoxLayout(sources); sourceLayout->setContentsMargins(20, 16, 20, 18); sourceLayout->setSpacing(10);
    auto *sourceTitle = new QLabel("01  /  Source & destination"); sourceLayout->addWidget(sourceTitle);
    auto *form = new QFormLayout; form->setLabelAlignment(Qt::AlignLeft); form->setSpacing(12); sourceLayout->addLayout(form);
    auto field = [&](const QString &caption, const QString &initial, bool directory, const QString &filter = QString(),
                     QFormLayout *target = nullptr) {
        auto *edit = new QLineEdit(initial); auto *row = new QWidget; auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0); h->addWidget(edit);
        auto *browse = new QPushButton("Browse"); h->addWidget(browse); (target ? target : form)->addRow(caption, row);
        QObject::connect(browse, &QPushButton::clicked, &window, [=, &window] {
            QString path = directory ? QFileDialog::getExistingDirectory(&window, caption, edit->text())
                : QFileDialog::getOpenFileName(&window, caption, edit->text(), filter);
            if (!path.isEmpty()) edit->setText(path);
        });
        return edit;
    };
    auto *profiles = field("CRC Profiles directory", local + "/CRC/Profiles", true);
    auto *maps = field("VideoMaps folder", local + "/CRC/VideoMaps", true);
    auto *sectorDirectory = field("Sector files directory", QDir::homePath(), true);
    auto *xplane = field("X-Plane Custom Data (optional)", "D:/SteamLibrary/steamapps/common/X-Plane 12/Custom Data", true);
    auto *output = field("Save packages to", QDir::homePath() + "/ScopeBridge-Output", true);
    layout->addWidget(sources);
    auto *note = new QLabel("Facility data is detected automatically. Use the VideoMaps root or a single ARTCC folder.");
    note->setObjectName("hint"); layout->addWidget(note);
    auto *navTitle = new QLabel("X-Plane navigation import (all disabled by default)");
    navTitle->setObjectName("hint"); layout->addWidget(navTitle);
    auto *navOptions = new QHBoxLayout;
    auto *importFixes = new QCheckBox("Fixes");
    auto *importNavaids = new QCheckBox("VOR / NDB");
    auto *importAirways = new QCheckBox("Airways");
    auto *importProcedures = new QCheckBox("CIFP summary");
    auto *areaOnly = new QCheckBox("Profile area only"); areaOnly->setChecked(true); areaOnly->setToolTip("Filter fixes and navaids to the selected CRC display center and range.");
    for (auto *box : {importFixes, importNavaids, importAirways, importProcedures}) navOptions->addWidget(box);
    navOptions->addWidget(areaOnly);
    navOptions->addStretch(); layout->addLayout(navOptions);
    auto *bar = new QHBoxLayout;
    auto *chooseTitle = new QLabel("02  /  Choose a profile"); bar->addWidget(chooseTitle); bar->addStretch();
    auto *scan = new QPushButton("Scan profiles"); bar->addWidget(scan); layout->addLayout(bar);
    auto *panels = new QSplitter(Qt::Horizontal);
    panels->setChildrenCollapsible(false);
    auto *profilePanel = new QWidget;
    auto *profileLayout = new QVBoxLayout(profilePanel);
    profileLayout->setContentsMargins(0, 0, 6, 0);
    auto *profileCount = new QLabel("Scan the directory to find saved profiles.");
    profileCount->setObjectName("hint"); profileLayout->addWidget(profileCount);
    auto *profileSearch = new QLineEdit;
    profileSearch->setPlaceholderText("Search profiles...");
    profileLayout->addWidget(profileSearch);
    auto *profileList = new QListWidget;
    profileList->setSelectionMode(QAbstractItemView::SingleSelection);
    profileList->setAlternatingRowColors(true);
    profileLayout->addWidget(profileList, 1);
    auto *sectorLabel = new QLabel("Base sector (optional)");
    profileLayout->addWidget(sectorLabel);
    auto *sectorList = new QListWidget;
    sectorList->setSelectionMode(QAbstractItemView::SingleSelection);
    sectorList->setAlternatingRowColors(true);
    sectorList->setMaximumHeight(130);
    profileLayout->addWidget(sectorList);
    panels->addWidget(profilePanel);
    auto *mapPanel = new QWidget;
    auto *mapLayout = new QVBoxLayout(mapPanel);
    mapLayout->setContentsMargins(6, 0, 0, 0);
    auto *mapBar = new QHBoxLayout;
    mapBar->addWidget(new QLabel("03  /  Video maps")); mapBar->addStretch();
    auto *refresh = new QPushButton("Refresh maps"); mapBar->addWidget(refresh);
    mapLayout->addLayout(mapBar);
    auto *table = new QTableWidget(0, 2); table->setHorizontalHeaderLabels({"Video map / default visibility", "CRC ID"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); mapLayout->addWidget(table, 1);
    panels->addWidget(mapPanel);
    panels->setStretchFactor(0, 1);
    panels->setStretchFactor(1, 2);
    panels->setSizes({470, 740});
    layout->addWidget(panels, 1);
    auto *log = new QPlainTextEdit; log->setReadOnly(true); log->setMaximumHeight(110); layout->addWidget(log);
    auto *actions = new QHBoxLayout; auto *status = new QLabel("Scan a Profiles directory to begin."); status->setObjectName("hint");
    auto *generate = new QPushButton("Generate EuroScope sector"); generate->setObjectName("primary");
    generate->setEnabled(false);
    generate->setDefault(true);
    actions->addWidget(status, 1); actions->addWidget(generate); layout->addLayout(actions);
    auto *licenseNotice = new QLabel("GPLv3 · No warranty · <a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">License</a> · UI powered by Qlementine (MIT)");
    licenseNotice->setObjectName("hint"); licenseNotice->setOpenExternalLinks(true); root->addWidget(licenseNotice);
    // EuroScope -> VRC page.
    auto *vrcPage = new QWidget; pages->addWidget(vrcPage);
    auto *vrcLayout = new QVBoxLayout(vrcPage); vrcLayout->setContentsMargins(0, 0, 0, 0); vrcLayout->setSpacing(16);
    auto *vrcCard = new QFrame; vrcCard->setObjectName("card");
    auto *vrcCardLayout = new QVBoxLayout(vrcCard); vrcCardLayout->setContentsMargins(20, 16, 20, 18); vrcCardLayout->setSpacing(10);
    vrcCardLayout->addWidget(new QLabel("01  /  EuroScope sector & destination"));
    auto *vrcForm = new QFormLayout; vrcForm->setLabelAlignment(Qt::AlignLeft); vrcForm->setSpacing(12); vrcCardLayout->addLayout(vrcForm);
    auto *esSector = field("EuroScope sector (.sct)", QString(), false, "EuroScope sector (*.sct *.sct2);;All files (*)", vrcForm);
    auto *esExtension = field("EuroScope extension (.ese, optional)", QString(), false, "EuroScope extension (*.ese);;All files (*)", vrcForm);
    auto *vrcOutput = field("Save VRC sector to", QDir::homePath() + "/ScopeBridge-VRC", true, QString(), vrcForm);
    vrcLayout->addWidget(vrcCard);
    auto *vrcNote = new QLabel("Writes a VRC .sct2: colors become #define names, GEO names become comments, REGIONNAME lines are removed,\n"
                               "fix/navaid names are resolved to coordinates and non-ASCII text is replaced. Source files are never changed.");
    vrcNote->setObjectName("hint"); vrcLayout->addWidget(vrcNote);
    auto *vrcOptionsTitle = new QLabel("02  /  .ese content"); vrcLayout->addWidget(vrcOptionsTitle);
    auto *vrcOptionRow = new QHBoxLayout;
    auto *useFreetext = new QCheckBox("FREETEXT → labels"); useFreetext->setChecked(true);
    auto *useSectorLines = new QCheckBox("SECTORLINE → ARTCC LOW"); useSectorLines->setChecked(true);
    auto *usePositions = new QCheckBox("POSITIONS → .pof"); usePositions->setChecked(true);
    for (auto *box : {useFreetext, useSectorLines, usePositions}) vrcOptionRow->addWidget(box);
    vrcOptionRow->addStretch(); vrcLayout->addLayout(vrcOptionRow);
    auto *vrcLog = new QPlainTextEdit; vrcLog->setReadOnly(true);
    vrcLog->setPlainText("Choose a EuroScope .sct file. A .ese file with the same name is picked up automatically.");
    vrcLayout->addWidget(vrcLog, 1);
    auto *vrcActions = new QHBoxLayout;
    auto *vrcStatus = new QLabel("No sector selected."); vrcStatus->setObjectName("hint");
    auto *vrcGenerate = new QPushButton("Generate VRC sector"); vrcGenerate->setObjectName("primary");
    vrcActions->addWidget(vrcStatus, 1); vrcActions->addWidget(vrcGenerate); vrcLayout->addLayout(vrcActions);
    QObject::connect(esSector, &QLineEdit::textChanged, &window, [=](const QString &path) {
        const QFileInfo info(path);
        const QString sibling = info.dir().filePath(info.completeBaseName() + ".ese");
        if (info.isFile() && QFileInfo::exists(sibling)) esExtension->setText(QDir::toNativeSeparators(sibling));
        vrcStatus->setText(info.isFile() ? "Ready: " + info.fileName() : "No sector selected.");
    });
    QObject::connect(vrcGenerate, &QPushButton::clicked, &window, [=, &window] {
        try {
            VrcOptions options;
            options.freetext = useFreetext->isChecked();
            options.sectorLines = useSectorLines->isChecked();
            options.positions = usePositions->isChecked();
            const auto result = esToVrc(esSector->text().trimmed(), esExtension->text().trimmed(), vrcOutput->text().trimmed(), options);
            QStringList lines{"Sector: " + QDir::toNativeSeparators(result.sector)};
            if (!result.pof.isEmpty()) lines << "Positions: " + QDir::toNativeSeparators(result.pof);
            lines << "" << result.summary;
            if (!result.warnings.isEmpty()) lines << "" << result.warnings;
            vrcLog->setPlainText(lines.join('\n'));
            vrcStatus->setText(QString("Generated %1").arg(QFileInfo(result.sector).fileName()));
            QMessageBox::information(&window, "VRC sector generated", "VRC files were created in:\n" + QFileInfo(result.sector).absolutePath());
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(result.sector).absolutePath()));
        } catch (const QString &e) { QMessageBox::critical(&window, "Conversion failed", e); vrcLog->setPlainText(e); }
    });
    QObject::connect(mode, &QTabBar::currentChanged, &window, [=](int index) {
        pages->setCurrentIndex(index);
        eyebrow->setText(index == 0 ? "CRC  /  EUROSCOPE" : "EUROSCOPE  /  VRC");
        hint->setText(index == 0 ? "Build a renderer-ready EuroScope package from a saved CRC display."
                                 : "Convert a EuroScope sector (.sct + .ese) into a VRC sector file (.sct2).");
    });
    auto preview = [&]() -> Profile {
        auto *selected = profileList->currentItem();
        if (!selected) throw QString("Select a profile from the list first.");
        table->setRowCount(0);
        const Profile p = inspect(selected->data(Qt::UserRole).toString(), maps->text());
        int active = 0;
        for (const auto &map : p.maps) if (map.selected) ++active;
        status->setText(QString("%1 · %2 · %3 active / %4 available maps").arg(p.artcc, p.facility).arg(active).arg(p.maps.size()));
        log->setPlainText(p.warnings.isEmpty() ? "Profile resolved successfully." : p.warnings.join("\n"));
        for (const auto &map : p.maps) {
            int r = table->rowCount(); table->insertRow(r);
            auto *item = new QTableWidgetItem((map.selected ? "ON   " : "OFF  ") + map.name);
            item->setToolTip(map.source);
            table->setItem(r, 0, item);
            table->setItem(r, 1, new QTableWidgetItem(map.id));
        }
        return p;
    };
    QObject::connect(scan, &QPushButton::clicked, &window, [&] {
        QSignalBlocker blocked(profileList);
        profileList->clear(); sectorList->clear(); table->setRowCount(0);
        generate->setEnabled(false);
        QStringList errors;
        QList<ProfileEntry> entries;
        try { entries = scanProfiles(profiles->text(), errors); }
        catch (const QString &e) {
            profileCount->setText("Directory not found.");
            log->setPlainText(e);
            status->setText("No profiles available.");
            return;
        }
        for (const auto &entry : entries) {
            auto *item = new QListWidgetItem(entry.name);
            item->setData(Qt::UserRole, entry.path);
            item->setToolTip(entry.artcc.isEmpty() ? entry.name : entry.artcc + "  /  " + entry.name);
            profileList->addItem(item);
            item->setHidden(!entry.name.contains(profileSearch->text(), Qt::CaseInsensitive));
        }
        const QDir sectorDir(sectorDirectory->text());
        for (const auto &file : sectorDir.entryInfoList({"*.sct"}, QDir::Files | QDir::Readable, QDir::Name)) {
            auto *item = new QListWidgetItem(file.fileName());
            item->setData(Qt::UserRole, file.absoluteFilePath());
            item->setToolTip(file.absoluteFilePath());
            sectorList->addItem(item);
        }
        profileCount->setText(QString("%1 profile(s) found").arg(entries.size()));
        log->setPlainText(errors.isEmpty() ? "Select a profile to preview its video maps."
                                            : QString("Skipped %1 invalid file(s):\n").arg(errors.size()) + errors.join("\n"));
        status->setText(entries.isEmpty() ? "No named profiles found." : "Select a profile to continue.");
    });
    QObject::connect(profileSearch, &QLineEdit::textChanged, &window, [=](const QString &query) {
        for (int i = 0; i < profileList->count(); ++i)
            profileList->item(i)->setHidden(!profileList->item(i)->text().contains(query, Qt::CaseInsensitive));
    });
    QObject::connect(profiles, &QLineEdit::textChanged, &window, [&] {
        profileList->clear(); sectorList->clear(); table->setRowCount(0);
        profileCount->setText("Scan the directory to find saved profiles.");
        generate->setEnabled(false);
        status->setText("Scan the selected Profiles directory.");
    });
    QObject::connect(sectorDirectory, &QLineEdit::textChanged, &window, [&] {
        sectorList->clear();
    });
    QObject::connect(profileList, &QListWidget::currentItemChanged, &window, [&](QListWidgetItem *current) {
        generate->setEnabled(current != nullptr);
        if (!current) { table->setRowCount(0); return; }
        try { preview(); }
        catch (const QString &e) { table->setRowCount(0); status->setText("Unable to preview maps."); log->setPlainText(e); }
    });
    QObject::connect(refresh, &QPushButton::clicked, &window, [&] {
        try { preview(); }
        catch (const QString &e) { status->setText("Cannot preview maps."); log->setPlainText(e); }
    });
    QObject::connect(generate, &QPushButton::clicked, &window, [&] {
        try {
            const auto p = preview();
            const QString baseSector = sectorList->currentItem()
                ? sectorList->currentItem()->data(Qt::UserRole).toString() : QString();
            XPlaneOptions navOptionsValue{importFixes->isChecked(), importNavaids->isChecked(), importAirways->isChecked(), importProcedures->isChecked()};
            const auto view = p.view;
            auto center = view.value("Center").toObject();
            if (center.isEmpty()) center = view.value("CurrentPrefSet").toObject().value("DisplayCenter").toObject();
            navOptionsValue.areaOnly = areaOnly->isChecked();
            navOptionsValue.centerLat = center.value("Lat").toDouble(35);
            navOptionsValue.centerLon = center.value("Lon").toDouble(-90);
            navOptionsValue.rangeNm = view.value("Range").toDouble(view.value("CurrentPrefSet").toObject().value("Range").toDouble(100));
            const auto result = convert(p, output->text(), baseSector, xplane->text(), navOptionsValue);
            const QString path = QDir(output->text()).filePath(safe(p.artcc + "_" + p.name));
            status->setText(QString("Generated %1 maps · %2 segments").arg(result.maps).arg(result.segments));
            log->setPlainText("Package: " + path + (result.warnings.isEmpty() ? "" : "\n" + result.warnings.join("\n")));
            QMessageBox::information(&window, "Package generated", "EuroScope files were created in:\n" + path);
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        } catch (const QString &e) { QMessageBox::critical(&window, "Conversion failed", e); log->setPlainText(e); }
    });
    auto *updates = new QNetworkAccessManager(&window);
    QTimer::singleShot(1800, &window, [updates, &window] {
        QNetworkRequest request(QUrl("https://api.github.com/repos/lpxlpx7/ScopeBridge/releases/latest"));
        request.setHeader(QNetworkRequest::UserAgentHeader, "ScopeBridge/" SCOPEBRIDGE_VERSION);
        auto *reply = updates->get(request);
        QObject::connect(reply, &QNetworkReply::finished, &window, [reply, &window] {
            const QByteArray payload = reply->readAll();
            const auto doc = QJsonDocument::fromJson(payload);
            const QString latest = doc.object().value("tag_name").toString().remove('v');
            const QString url = doc.object().value("html_url").toString();
            reply->deleteLater();
            if (latest.isEmpty() || url.isEmpty()) return;
            const auto currentParts = QString(SCOPEBRIDGE_VERSION).split('.');
            const auto latestParts = latest.split('.');
            bool newer = false;
            for (int i = 0; i < qMax(currentParts.size(), latestParts.size()); ++i) {
                const int current = i < currentParts.size() ? currentParts.at(i).toInt() : 0;
                const int available = i < latestParts.size() ? latestParts.at(i).toInt() : 0;
                if (available != current) { newer = available > current; break; }
            }
            if (newer && QMessageBox::question(&window, "Update available",
                    "ScopeBridge " + latest + " is available. Open the GitHub release page?") == QMessageBox::Yes)
                QDesktopServices::openUrl(QUrl(url));
        });
    });
    window.show();
    return app.exec();
}
