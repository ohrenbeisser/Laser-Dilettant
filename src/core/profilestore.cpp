#include "core/profilestore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>

namespace ld::core {

QString ProfileStore::fileNameFor(const QString& name)
{
    QString out;
    for (const QChar c : name.trimmed()) {
        if (c.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(c)) {
            out += QLatin1Char('_');
        } else if (c.isSpace()) {
            out += QLatin1Char('-');
        } else {
            out += c;
        }
    }
    while (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char('-'))) {
        out.chop(1);
    }
    out = out.left(80);
    if (out.isEmpty()) {
        out = QStringLiteral("unbenannt");
    }
    // Unter Windows reservierte Gerätenamen vermeiden.
    static const QStringList reserved = {QStringLiteral("con"), QStringLiteral("prn"), QStringLiteral("aux"),
                                         QStringLiteral("nul"), QStringLiteral("com1"), QStringLiteral("lpt1")};
    if (reserved.contains(out.toLower())) {
        out.prepend(QLatin1Char('_'));
    }
    return out + QStringLiteral(".json");
}

namespace {
std::optional<QJsonObject> readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    return doc.object();
}
}  // namespace

QStringList ProfileStore::names() const
{
    QStringList result;
    const QDir dir(folder_);
    for (const QString& file : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        const auto json = readFile(dir.filePath(file));
        const QString name = json ? json->value(QStringLiteral("name")).toString().trimmed() : QString();
        if (!name.isEmpty()) {
            result.append(name);
        }
    }
    std::sort(result.begin(), result.end(), [](const QString& a, const QString& b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QString ProfileStore::pathOfName(const QString& name) const
{
    const QDir dir(folder_);
    for (const QString& file : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        const auto json = readFile(dir.filePath(file));
        if (json && QString::compare(json->value(QStringLiteral("name")).toString().trimmed(), name.trimmed(),
                                     Qt::CaseInsensitive) == 0) {
            return dir.filePath(file);
        }
    }
    return {};
}

bool ProfileStore::exists(const QString& name) const
{
    return !pathOfName(name).isEmpty();
}

std::optional<QJsonObject> ProfileStore::read(const QString& name) const
{
    const QString path = pathOfName(name);
    return path.isEmpty() ? std::nullopt : readFile(path);
}

bool ProfileStore::write(const QJsonObject& json, QString* error)
{
    const QString name = json.value(QStringLiteral("name")).toString().trimmed();
    auto fail = [&](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    if (name.isEmpty()) {
        return fail(QCoreApplication::translate("ld::core", "Der Name darf nicht leer sein."));
    }
    if (!QDir().mkpath(folder_)) {
        return fail(QCoreApplication::translate("ld::core", "Der Ordner kann nicht angelegt werden: %1").arg(folder_));
    }
    // Gibt es das Profil schon, wird dessen Datei überschrieben; sonst eine neue, eindeutige Datei.
    QString path = pathOfName(name);
    if (path.isEmpty()) {
        const QDir dir(folder_);
        const QString base = fileNameFor(name).chopped(5);
        path = dir.filePath(base + QStringLiteral(".json"));
        for (int i = 2; QFile::exists(path); ++i) {
            path = dir.filePath(QStringLiteral("%1-%2.json").arg(base).arg(i));
        }
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return fail(file.errorString());
    }
    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return fail(file.errorString());
    }
    return true;
}

bool ProfileStore::remove(const QString& name)
{
    const QString path = pathOfName(name);
    return !path.isEmpty() && QFile::remove(path);
}

bool ProfileStore::rename(const QString& oldName, const QString& newName, QString* error)
{
    auto fail = [&](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) {
        return fail(QCoreApplication::translate("ld::core", "Der Name darf nicht leer sein."));
    }
    auto json = read(oldName);
    if (!json) {
        return fail(QCoreApplication::translate("ld::core", "Das Profil gibt es nicht."));
    }
    const bool onlyCase = QString::compare(oldName.trimmed(), trimmed, Qt::CaseInsensitive) == 0;
    if (!onlyCase && exists(trimmed)) {
        return fail(QCoreApplication::translate("ld::core", "Es gibt schon ein Profil mit diesem Namen."));
    }
    const QString oldPath = pathOfName(oldName);
    (*json)[QStringLiteral("name")] = trimmed;
    if (!write(*json, error)) {
        return false;
    }
    // Bei reiner Groß-/Kleinschreibung wurde dieselbe Datei überschrieben; sonst die alte löschen.
    if (!onlyCase) {
        QFile::remove(oldPath);
    }
    return true;
}

}  // namespace ld::core
