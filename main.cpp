#include <QApplication>
#include <QBoxLayout>
#include <QComboBox>
#include <QCoreApplication>
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
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextStream>
#include <QWidget>
#include <cmath>

namespace {
struct Map { QString id, name, source; };
struct Profile { QString name, artcc, path, display, facility; QList<Map> maps; QStringList warnings; QJsonObject view; };

QJsonObject json(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw QString("Cannot open %1: %2").arg(path, file.errorString());
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        throw QString("Invalid JSON in %1: %2").arg(path, error.errorString());
    return doc.object();
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
void facilities(const QJsonObject &f, QHash<QString, QJsonObject> &byId,
                QHash<QString, QString> &positions) {
    const QString id = f.value("id").toString();
    if (!id.isEmpty()) byId.insert(id, f);
    for (const auto &p : f.value("positions").toArray()) {
        const QString position = p.toObject().value("id").toString();
        if (!position.isEmpty()) positions.insert(position, id);
    }
    for (const auto &child : f.value("childFacilities").toArray()) facilities(child.toObject(), byId, positions);
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
    return QDir(root).filePath(artcc + "/" + id + ".geojson");
}
Profile inspect(const QString &file, const QString &artccRoot, const QString &mapRoot) {
    const auto data = json(file);
    Profile p;
    p.path = file; p.name = data.value("Name").toString(QFileInfo(file).baseName());
    p.artcc = data.value("ArtccId").toString();
    if (p.artcc.isEmpty()) throw QString("Profile has no ArtccId: %1").arg(file);
    const auto root = json(QDir(artccRoot).filePath(p.artcc + ".json")).value("facility").toObject();
    QHash<QString, QJsonObject> byId;
    QHash<QString, QString> positions;
    facilities(root, byId, positions);
    p.view = activeDisplay(data);
    if (p.view.isEmpty()) p.warnings << "No visible CRC display was found.";
    p.facility = positions.value(p.view.value("PositionId").toString(), p.view.value("FacilityId").toString());
    p.display = p.view.value("$type").toString().section('.', -1).section(',', 0, 0);
    QStringList ids;
    const QString geo = p.view.value("ActiveGeoMap").toString();
    if (!geo.isEmpty()) {
        bool matched = false;
        for (const auto &entry : root.value("eramConfiguration").toObject().value("geoMaps").toArray()) {
            const auto group = entry.toObject();
            if (group.value("name").toString() == geo) {
                matched = true;
                for (const auto &id : group.value("videoMapIds").toArray()) ids << id.toString();
                break;
            }
        }
        if (!matched || ids.isEmpty()) p.warnings << "Active ERAM geo group was not found or is empty: " + geo;
    } else if (p.view.value("$type").toString().contains("Stars", Qt::CaseInsensitive)) {
        const auto list = byId.value(p.facility).value("starsConfiguration").toObject().value("videoMapIds").toArray();
        const auto selected = p.view.value("CurrentPrefSet").toObject().value("SelectedVideoMapIds").toArray();
        for (const auto &number : selected) {
            const int index = number.toInt(-1) - 1;
            if (index >= 0 && index < list.size()) ids << list.at(index).toString();
            else p.warnings << QString("STARS map number %1 is out of range.").arg(number.toInt());
        }
    } else {
        const auto f = byId.value(p.facility);
        for (const QString &key : {"towerCabConfiguration", "asdexConfiguration"}) {
            const auto id = f.value(key).toObject().value("videoMapId").toString();
            if (!id.isEmpty()) ids << id;
        }
        if (ids.isEmpty()) p.warnings << "No tower cab / ASDEX map configured for this display.";
    }
    ids.removeDuplicates();
    for (const auto &id : ids) {
        if (id.isEmpty()) continue;
        const QString path = mapPath(mapRoot, p.artcc, id);
        if (!QFileInfo::exists(path)) { p.warnings << "Missing map: " + path; continue; }
        const auto data = json(path);
        p.maps << Map{id, data.value("name").toString(id), path};
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
    const double lat = center.value("Lat").toDouble(35), lon = center.value("Lon").toDouble(-90);
    return QString("; Generated by CRC to EuroScope\n[INFO]\n%1\n%1_CTR\n%1\n%2\n%3\n60\n49\n-1\n1\n\n")
        .arg(artcc, dms(lat, true), dms(lon, false));
}
struct Result { int maps = 0, segments = 0; QStringList warnings; };
Result convert(const Profile &p, const QString &dest, const QString &sector) {
    if (p.maps.isEmpty()) throw QString("No maps are available for %1. Check the selected display and CRC data.").arg(p.name);
    const QString folder = QDir(dest).filePath(safe(p.artcc + "_" + p.name));
    if (!QDir().mkpath(folder)) throw QString("Cannot create %1").arg(folder);
    const QString stem = safe(p.artcc + "_" + p.name);
    QStringList geo;
    Result result; result.warnings = p.warnings;
    if (sector.isEmpty()) result.warnings << "No base sector supplied: navigation, airports and runways are not included.";
    for (int i = 0; i < p.maps.size(); ++i) {
        const auto &m = p.maps.at(i);
        const auto data = json(m.source);
        int count = 0;
        const QString label = safe(m.name).left(36) + "_" + QString::number(i + 1);
        geo << "; " + m.name + " (" + m.id + ")";
        if (data.value("type").toString() == "FeatureCollection") {
            for (const auto &feature : data.value("features").toArray())
                geometry(feature.toObject().value("geometry").toObject(), label, geo, count);
        } else if (data.value("type").toString() == "Feature") {
            geometry(data.value("geometry").toObject(), label, geo, count);
        } else geometry(data, label, geo, count);
        if (count == 0) result.warnings << "No drawable lines in " + m.name + " (" + m.id + ")";
        else ++result.maps;
        result.segments += count;
    }
    if (result.segments == 0) throw QString("None of the selected maps contains drawable line geometry.");
    QString foundation = baseSector(sector, p.artcc, p.view);
    if (!foundation.contains(QRegularExpression("(?im)^#define\\s+CRCMap\\s+")))
        foundation.prepend("#define CRCMap 12632256\n");
    write(QDir(folder).filePath(stem + ".sct"), foundation + "[GEO]\n" + geo.join("\n") + "\n");
    const bool ground = p.display.contains("Cab", Qt::CaseInsensitive) || p.display.contains("Asdex", Qt::CaseInsensitive);
    auto center = p.view.value("Center").toObject();
    if (center.isEmpty()) center = p.view.value("CurrentPrefSet").toObject().value("DisplayCenter").toObject();
    const double lat = center.value("Lat").toDouble(35), lon = center.value("Lon").toDouble(-90);
    double range = p.view.value("Range").toDouble(p.view.value("CurrentPrefSet").toObject().value("Range").toDouble(100));
    if (!std::isfinite(range) || range <= 0) range = 100;
    const double delta = qBound(0.02, range / 60.0, 60.0), lonDelta = delta / qMax(0.25, std::cos(lat * 3.141592653589793 / 180));
    const QString asr = QString("DisplayTypeName:%1\nDisplayTypeNeedRadarContent:%2\nDisplayTypeGeoReferenced:1\nSECTORFILE:\nSECTORTITLE:%3.sct\nSHOWC:1\nSHOWSB:1\nWINDOWAREA:%4:%5:%6:%7\n")
        .arg(ground ? "Ground Radar display" : "Standard ES radar screen").arg(ground ? 0 : 1).arg(stem)
        .arg(lat - delta, 0, 'f', 6).arg(lon - lonDelta, 0, 'f', 6)
        .arg(lat + delta, 0, 'f', 6).arg(lon + lonDelta, 0, 'f', 6);
    write(QDir(folder).filePath(stem + ".asr"), asr);
    write(QDir(folder).filePath(stem + ".prf"),
          "Settings\tsector\t\\" + stem + ".sct\nASRFastKeys\t1\t\\" + stem + ".asr\nRecentFiles\tRecent1\t\\" + stem + ".asr\n");
    return result;
}
}

int main(int argc, char **argv) {
    if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == "--convert") {
        QCoreApplication cli(argc, argv);
        try {
            const QString file = QString::fromLocal8Bit(argv[2]);
            const QString destination = QString::fromLocal8Bit(argv[3]);
            const QString base = argc >= 5 ? QString::fromLocal8Bit(argv[4]) : QString();
            const QString local = qEnvironmentVariable("LOCALAPPDATA");
            const auto p = inspect(file, local + "/CRC/ARTCCs", local + "/CRC/VideoMaps");
            const auto result = convert(p, destination, base);
            QTextStream(stdout) << p.name << ": " << result.maps << " maps, " << result.segments << " segments\n";
            for (const auto &warning : result.warnings) QTextStream(stdout) << "Warning: " << warning << "\n";
            return 0;
        } catch (const QString &error) {
            QTextStream(stderr) << error << "\n";
            return 1;
        }
    }
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    app.setStyleSheet(R"(
        QWidget { background:#111827; color:#e5eaf3; font-family:'Segoe UI'; font-size:13px; }
        QLabel#title { font-size:28px; font-weight:700; color:#f5f8ff; }
        QLabel#hint { color:#94a3b8; font-size:12px; }
        QGroupBox { border:1px solid #344256; border-radius:12px; margin-top:16px; padding:14px; font-weight:600; }
        QGroupBox::title { subcontrol-origin:margin; left:16px; padding:0 6px; color:#9ac6ff; }
        QLineEdit,QComboBox,QPlainTextEdit,QTableWidget { background:#1c293b; border:1px solid #3a4b62; border-radius:7px; padding:7px; selection-background-color:#2967a6; }
        QTableWidget { gridline-color:#344256; } QHeaderView::section { background:#233349; padding:7px; border:0; }
        QPushButton { background:#294466; border:1px solid #456489; border-radius:8px; padding:9px 16px; font-weight:600; }
        QPushButton:hover { background:#38628c; } QPushButton#primary { background:#1676ca; border:0; color:white; padding:12px 24px; }
        QPushButton#primary:hover { background:#278be0; }
    )");
    QWidget window;
    window.setWindowTitle("ScopeBridge | CRC to EuroScope");
    window.resize(1050, 810);
    auto *layout = new QVBoxLayout(&window); layout->setContentsMargins(25, 22, 25, 22); layout->setSpacing(12);
    auto *title = new QLabel("ScopeBridge"); title->setObjectName("title"); layout->addWidget(title);
    auto *hint = new QLabel("Generate a EuroScope sector package from your active CRC profile and video maps."); hint->setObjectName("hint"); layout->addWidget(hint);
    const QString local = qEnvironmentVariable("LOCALAPPDATA", QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/..");
    auto *sources = new QGroupBox("SOURCE FILES"); auto *form = new QFormLayout(sources);
    auto field = [&](const QString &caption, const QString &initial, bool directory) {
        auto *edit = new QLineEdit(initial); auto *row = new QWidget; auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0); h->addWidget(edit);
        auto *browse = new QPushButton("Browse"); h->addWidget(browse); form->addRow(caption, row);
        QObject::connect(browse, &QPushButton::clicked, &window, [=, &window] {
            QString path = directory ? QFileDialog::getExistingDirectory(&window, caption, edit->text())
                : QFileDialog::getOpenFileName(&window, caption, edit->text(), "Sector files (*.sct);;All files (*)");
            if (!path.isEmpty()) edit->setText(path);
        });
        return edit;
    };
    auto *profiles = field("Profiles", local + "/CRC/Profiles", true);
    auto *artccs = field("ARTCC files", local + "/CRC/ARTCCs", true);
    auto *maps = field("Video maps", local + "/CRC/VideoMaps", true);
    auto *sector = field("Base sector (optional)", "", false);
    auto *output = field("Output folder", QDir::homePath() + "/CRC-ES-Output", true);
    layout->addWidget(sources);
    auto *bar = new QHBoxLayout;
    auto *choose = new QComboBox; choose->setMinimumWidth(330);
    auto *refresh = new QPushButton("Scan profiles"); bar->addWidget(choose, 1); bar->addWidget(refresh); layout->addLayout(bar);
    auto *table = new QTableWidget(0, 3); table->setHorizontalHeaderLabels({"Selected video map", "CRC ID", "Source"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(table, 1);
    auto *log = new QPlainTextEdit; log->setReadOnly(true); log->setMaximumHeight(110); layout->addWidget(log);
    auto *actions = new QHBoxLayout; auto *status = new QLabel("Ready to scan."); status->setObjectName("hint");
    auto *generate = new QPushButton("Generate package"); generate->setObjectName("primary");
    actions->addWidget(status, 1); actions->addWidget(generate); layout->addLayout(actions);
    QList<Profile> found;
    auto preview = [&] {
        table->setRowCount(0);
        const int index = choose->currentIndex(); if (index < 0 || index >= found.size()) return;
        const auto &p = found.at(index);
        status->setText(QString("%1 · %2 · %3 map(s)").arg(p.artcc, p.facility).arg(p.maps.size()));
        log->setPlainText(p.warnings.isEmpty() ? "Profile resolved successfully." : p.warnings.join("\n"));
        for (const auto &map : p.maps) {
            int r = table->rowCount(); table->insertRow(r);
            table->setItem(r, 0, new QTableWidgetItem(map.name));
            table->setItem(r, 1, new QTableWidgetItem(map.id));
            table->setItem(r, 2, new QTableWidgetItem(map.source));
        }
    };
    QObject::connect(choose, QOverload<int>::of(&QComboBox::currentIndexChanged), &window, [&](int){ preview(); });
    QObject::connect(refresh, &QPushButton::clicked, &window, [&] {
        found.clear(); choose->clear(); QStringList errors;
        for (const auto &file : QDir(profiles->text()).entryInfoList({"*.json"}, QDir::Files, QDir::Name)) {
            try { found << inspect(file.absoluteFilePath(), artccs->text(), maps->text()); }
            catch (const QString &e) { errors << e; }
        }
        for (const auto &p : found) choose->addItem(p.artcc + "  /  " + p.name + "  ·  " + QString::number(p.maps.size()) + " maps");
        preview();
        if (!errors.isEmpty()) log->appendPlainText(QString("\n%1 profile(s) could not be read:\n").arg(errors.size()) + errors.join("\n"));
        if (found.isEmpty()) status->setText("No profiles found. Check source folders.");
    });
    QObject::connect(generate, &QPushButton::clicked, &window, [&] {
        const int i = choose->currentIndex(); if (i < 0 || i >= found.size()) return;
        try {
            const auto result = convert(found.at(i), output->text(), sector->text());
            const QString path = QDir(output->text()).filePath(safe(found.at(i).artcc + "_" + found.at(i).name));
            status->setText(QString("Generated %1 maps · %2 segments").arg(result.maps).arg(result.segments));
            log->setPlainText("Package: " + path + (result.warnings.isEmpty() ? "" : "\n" + result.warnings.join("\n")));
            QMessageBox::information(&window, "Package generated", "Open the .prf file in EuroScope:\n" + path);
        } catch (const QString &e) { QMessageBox::critical(&window, "Conversion failed", e); log->setPlainText(e); }
    });
    window.show();
    QMetaObject::invokeMethod(refresh, "click", Qt::QueuedConnection);
    return app.exec();
}
