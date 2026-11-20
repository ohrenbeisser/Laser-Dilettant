// ContentController: Was auf die Arbeitsfläche kommt (zerlegt aus dem MainWindow, siehe planung.md 3.33).
//
// Er bringt Bilder, Text (Schrift) und die Kalibriergrafik ins Dokument: Bilddatei laden (Dialog, Drag & Drop,
// Zwischenablage), den Importdialog "Bild bearbeiten" öffnen, den Schrift-Dialog zeigen und die Messgrafik der
// Kalibrierung einfügen. Vorher fragt er, ob ein verändertes Bild ersetzt werden darf. Das MainWindow reagiert nur
// auf seine Signale (Statusleiste, Titelzeile).
#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <functional>
#include <memory>

#include "core/calibration.h"
#include "core/machinesetup.h"
#include "core/svgmodel.h"

class QWidget;

namespace ld::core {
class ImageDocument;
}

namespace ld::app {

class ProjectController;

class ContentController : public QObject {
    Q_OBJECT

public:
    // machine: das Maschinenprofil des Hauptfensters (Referenz). pixelSize: Auflösung aus dem rechten Panel (für neue
    // Bilder). jobActive: läuft gerade ein Auftrag am Laser (dann gibt es kein neues Bild). ncMode: ist gerade eine
    // NC-Datei statt eines Bildes geladen.
    ContentController(ld::core::ImageDocument* document, const ld::core::MachineSetup& machine, ProjectController* project,
                      std::function<double()> pixelSize, std::function<bool()> jobActive, std::function<bool()> ncMode,
                      QWidget* dialogParent, QObject* parent = nullptr);

    // Fragt bei verändertem Bild nach, ob es ersetzt werden darf (true = ja, auch wenn nichts zu verlieren ist).
    bool confirmReplaceImage();

    void openImageDialog();                      // Datei wählen und laden
    void loadFile(const QString& path);          // Drag & Drop (eine .ldproj wird als Projekt geöffnet)
    void pasteImage();                           // aus der Zwischenablage
    void editImage();                            // Importdialog "Bild bearbeiten"
    void openTextDialog();                       // Schrift: Text einfügen bzw. bearbeiten
    // Setzt die Messgrafik der Kalibrierung als Bild auf die Arbeitsfläche. messageParent: Fenster für Meldungen
    // (der Kalibrierdialog, falls er die Anfrage stellt).
    void insertCalibration(const ld::core::CalibrationSpec& spec, QWidget* messageParent = nullptr);

signals:
    void message(const QString& text, int milliseconds);   // Statusleiste
    void contentChanged();                                  // Text oder Kalibriergrafik eingefügt (Titelzeile auffrischen)

private:
    void setNewImage(const QImage& image, const QString& path, std::shared_ptr<const ld::core::SvgModel> svg = nullptr);

    ld::core::ImageDocument* document_;
    const ld::core::MachineSetup& machine_;
    ProjectController* project_;
    std::function<double()> pixelSize_;
    std::function<bool()> jobActive_;
    std::function<bool()> ncMode_;
    QWidget* dialogParent_;
};

}  // namespace ld::app
