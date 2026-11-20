// Ein Ordner mit JSON-Dateien, eine pro Profil (Maschinenprofile, Materialien).
// Der Name steht im JSON (Feld "name"); der Dateiname wird daraus abgeleitet. Namen sind
// ohne Rücksicht auf Groß-/Kleinschreibung eindeutig.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>

namespace ld::core {

class ProfileStore {
public:
    explicit ProfileStore(const QString& folder) : folder_(folder) {}

    const QString& folder() const { return folder_; }

    // Namen aller lesbaren Profile, nach Name sortiert.
    QStringList names() const;
    bool exists(const QString& name) const;
    std::optional<QJsonObject> read(const QString& name) const;
    // Schreibt (oder überschreibt) das Profil mit dem Namen aus dem Feld "name". Legt den Ordner an.
    bool write(const QJsonObject& json, QString* error = nullptr);
    bool remove(const QString& name);
    // Benennt um: schreibt die Datei mit neuem Namen und löscht die alte. Scheitert, wenn der neue Name belegt ist.
    bool rename(const QString& oldName, const QString& newName, QString* error = nullptr);

    // Dateiname (ohne Ordner, mit .json) für einen Namen: Sonderzeichen werden ersetzt.
    static QString fileNameFor(const QString& name);

private:
    QString pathOfName(const QString& name) const;   // vorhandene Datei dieses Namens, sonst leer

    QString folder_;
};

}  // namespace ld::core
