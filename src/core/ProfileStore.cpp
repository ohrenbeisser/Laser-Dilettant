#include "core/ProfileStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace {
const char* const kBuiltinIds[] = { "diy8000_4050", "diy8000_1720", "grbl_diode_generic", "k40_grbl" };

DeviceProfile readProfile(const QString& path, bool* ok)
{
    QFile f(path);
    *ok = false;
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    DeviceProfile p = DeviceProfile::fromJson(doc.object());
    if (p.id.isEmpty())
        p.id = QFileInfo(path).completeBaseName();
    *ok = true;
    return p;
}
} // namespace

ProfileStore::ProfileStore(const QString& directory, QObject* parent)
    : QObject(parent)
    , m_dir(directory)
{
    if (m_dir.isEmpty())
        m_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/profiles");
}

QStringList ProfileStore::builtinIds()
{
    QStringList ids;
    for (const char* id : kBuiltinIds)
        ids << QString::fromLatin1(id);
    return ids;
}

QString ProfileStore::filePath(const QString& id) const
{
    return m_dir + QLatin1Char('/') + id + QStringLiteral(".json");
}

void ProfileStore::copyBuiltins()
{
    QDir().mkpath(m_dir);
    for (const QString& id : builtinIds()) {
        const QString target = filePath(id);
        // Built-ins are always refreshed from the resources, so fixes in new
        // program versions reach the user. They are read-only anyway.
        QFile::remove(target);
        QFile::copy(QStringLiteral(":/profiles/%1.json").arg(id), target);
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    }
}

void ProfileStore::load()
{
    copyBuiltins();
    m_profiles.clear();
    const QStringList builtins = builtinIds();
    const QDir dir(m_dir);
    const auto files = dir.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name);
    for (const QString& file : files) {
        bool ok = false;
        DeviceProfile p = readProfile(dir.filePath(file), &ok);
        if (!ok)
            continue;
        p.id = QFileInfo(file).completeBaseName();
        p.builtin = builtins.contains(p.id);
        m_profiles.append(p);
    }
    // Built-ins first in their defined order, then user profiles by name.
    std::stable_sort(m_profiles.begin(), m_profiles.end(), [&](const DeviceProfile& a, const DeviceProfile& b) {
        const int ia = builtins.indexOf(a.id);
        const int ib = builtins.indexOf(b.id);
        if (ia >= 0 || ib >= 0) {
            if (ia < 0)
                return false;
            if (ib < 0)
                return true;
            return ia < ib;
        }
        return a.name.localeAwareCompare(b.name) < 0;
    });
    emit changed();
}

int ProfileStore::indexOf(const QString& id) const
{
    for (int i = 0; i < m_profiles.size(); ++i)
        if (m_profiles[i].id == id)
            return i;
    return -1;
}

const DeviceProfile* ProfileStore::find(const QString& id) const
{
    const int i = indexOf(id);
    return i >= 0 ? &m_profiles[i] : nullptr;
}

QString ProfileStore::uniqueId(const QString& name) const
{
    QString base = name.toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("_"));
    base.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
    if (base.isEmpty())
        base = QStringLiteral("profile");
    QString id = base;
    int n = 2;
    while (indexOf(id) >= 0 || QFile::exists(filePath(id)))
        id = QStringLiteral("%1_%2").arg(base).arg(n++);
    return id;
}

bool ProfileStore::save(const DeviceProfile& profile, QString* error)
{
    if (builtinIds().contains(profile.id)) {
        if (error)
            *error = tr("Built-in profiles cannot be changed. Make a copy first.");
        return false;
    }
    if (profile.id.isEmpty()) {
        if (error)
            *error = tr("The profile has no id.");
        return false;
    }
    QDir().mkpath(m_dir);
    DeviceProfile p = profile;
    p.sanitize();
    QSaveFile f(filePath(p.id));
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(p.toJson()).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    p.builtin = false;
    const int i = indexOf(p.id);
    if (i >= 0)
        m_profiles[i] = p;
    else
        m_profiles.append(p);
    emit changed();
    return true;
}

bool ProfileStore::remove(const QString& id, QString* error)
{
    if (builtinIds().contains(id)) {
        if (error)
            *error = tr("Built-in profiles cannot be deleted.");
        return false;
    }
    const int i = indexOf(id);
    if (i < 0)
        return false;
    if (!QFile::remove(filePath(id))) {
        if (error)
            *error = tr("The file could not be deleted.");
        return false;
    }
    m_profiles.removeAt(i);
    emit changed();
    return true;
}

QString ProfileStore::copy(const QString& id, const QString& newName, QString* error)
{
    const DeviceProfile* src = find(id);
    if (!src)
        return {};
    DeviceProfile p = *src;
    p.name = newName;
    p.id = uniqueId(newName);
    p.builtin = false;
    return save(p, error) ? p.id : QString();
}

QString ProfileStore::create(const QString& name, QString* error)
{
    DeviceProfile p;
    p.name = name;
    p.id = uniqueId(name);
    return save(p, error) ? p.id : QString();
}
