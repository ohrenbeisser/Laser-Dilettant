// Verwaltet das Projekt: Neu, Öffnen, Speichern, Rückfrage bei Änderungen, Sicherung gegen Abstürze
// (siehe docs/planung.md 3.13). Die Fachlogik (Dateiformat, Bildpfade) steht in core/project.h.
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>

class QWidget;

namespace ld::core {
class ImageDocument;
struct ProjectData;
}

namespace ld::app {

class ProjectController : public QObject {
    Q_OBJECT

public:
    // dialogParent: Fenster für Meldungen und Dateidialoge. materialName liefert den Namen des
    // gewählten Materials (für die Info im Projekt).
    ProjectController(ld::core::ImageDocument* document, QWidget* dialogParent,
                      std::function<QString()> materialName, QObject* parent = nullptr);

    QString path() const { return path_; }
    bool hasPath() const { return !path_.isEmpty(); }

    // Neu: Das Bild verschwindet; false, wenn der Bediener abbricht.
    bool newProject();
    // Öffnet ein Projekt; ohne Pfad fragt ein Dateidialog. false bei Abbruch oder Fehler.
    bool openProject(const QString& path = QString());
    bool save();
    bool saveAs();
    // Fragt bei ungespeicherten Änderungen "Speichern / Verwerfen / Abbrechen". true: weitermachen.
    bool maybeSave();

    // ---- Sicherung gegen Abstürze ----
    static QString recoveryFolder();
    bool recoveryAvailable() const;
    // Stellt die Sicherung wieder her (als ungespeicherte Änderung). false bei Fehler.
    bool restoreRecovery();
    void discardRecovery();
    // Intervall in Minuten, 0 = aus. Wird aus den Einstellungen gesetzt.
    void setAutosaveMinutes(int minutes);
    // Sichert jetzt (nur bei Änderungen); der Zeitgeber ruft das auf.
    void autosaveNow() { autosave(); }

    // ---- Letzte Sitzung (beim Start wieder laden) ----
    // Beim normalen Beenden: merkt Bild und Einstellungen (oder löscht die Merkung, wenn kein Bild offen ist).
    void saveSession();
    // Beim Start: lädt die gemerkte Sitzung still. Fehlt das Bild, bleibt die Seite leer (keine Meldung).
    bool restoreSession();

signals:
    void pathChanged();   // anderer Projektpfad (Titelleiste)

private:
    bool saveTo(const QString& path);
    enum class Mode { Open, Recovery, Session };
    // silent: keine Meldungen und keine Suche nach fehlenden Bildern (für den automatischen Start).
    bool applyProject(const QString& projectFile, Mode mode, bool silent = false);
    // Schreibt den ganzen Zustand (auch ein eingefügtes Bild als PNG) in den Sicherungsordner.
    bool writeSnapshot(const QString& fileName, const QString& imageName);
    void autosave();
    ld::core::ProjectData currentData() const;

    ld::core::ImageDocument* document_;
    QWidget* parentWidget_;
    std::function<QString()> materialName_;
    QString path_;
    QTimer autosaveTimer_;
};

}  // namespace ld::app
