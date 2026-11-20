// Die Maschinenprofile des Benutzers: eine JSON-Datei pro Profil im Benutzerverzeichnis
// (siehe docs/planung.md 3.12) und die Wahl des aktiven Profils.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>

#include "core/machinesetup.h"
#include "core/profilestore.h"

namespace ld::app {

class MachineProfiles : public QObject {
    Q_OBJECT

public:
    // Standardordner: <Konfigurationsverzeichnis>/Laser-Dilettant/machines
    static QString defaultFolder();
    static QString defaultMaterialFolder();

    explicit MachineProfiles(const QString& folder, QObject* parent = nullptr);

    QStringList names() const { return store_.names(); }
    std::optional<ld::core::MachineSetup> load(const QString& name) const;
    bool save(const ld::core::MachineSetup& setup, QString* error = nullptr);
    bool remove(const QString& name);
    bool rename(const QString& oldName, const QString& newName, QString* error = nullptr);

    // Aktives Profil (gemerkt in den Programmeinstellungen). Gibt es das gemerkte nicht mehr,
    // gilt das erste vorhandene.
    QString activeName() const;
    void setActive(const QString& name);
    ld::core::MachineSetup active() const;

    // Legt beim ersten Start "DIY8000-Desktop1720" an, falls es noch kein Profil gibt. Dabei werden
    // die vorläufigen G-Code-Einstellungen aus der Zeit vor M7 übernommen (falls vorhanden).
    void ensureDefault();

signals:
    void listChanged();

private:
    ld::core::ProfileStore store_;
};

}  // namespace ld::app
