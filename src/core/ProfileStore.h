#pragma once

#include "core/DeviceProfile.h"

#include <QList>
#include <QObject>
#include <QString>

// Loads and stores device profiles as JSON files in
// <AppDataLocation>/profiles. Built-in profiles from the resources are copied
// there on first start; they can be copied but not changed or deleted.
class ProfileStore : public QObject
{
    Q_OBJECT
public:
    // An empty directory means the default AppDataLocation/profiles.
    explicit ProfileStore(const QString& directory = QString(), QObject* parent = nullptr);

    void load();

    const QList<DeviceProfile>& profiles() const { return m_profiles; }
    int indexOf(const QString& id) const;
    const DeviceProfile* find(const QString& id) const;
    QString directory() const { return m_dir; }

    // Saves (creates or updates) a profile. Built-in profiles are refused.
    bool save(const DeviceProfile& profile, QString* error = nullptr);
    bool remove(const QString& id, QString* error = nullptr);
    // Creates a copy with a new id and the given name. Returns the new id.
    QString copy(const QString& id, const QString& newName, QString* error = nullptr);
    QString create(const QString& name, QString* error = nullptr);

    QString uniqueId(const QString& name) const;

    static QStringList builtinIds();

signals:
    void changed();

private:
    void copyBuiltins();
    QString filePath(const QString& id) const;

    QString m_dir;
    QList<DeviceProfile> m_profiles;
};
