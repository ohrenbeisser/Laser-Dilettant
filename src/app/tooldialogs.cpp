#include "tooldialogs.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>

#include "appsettings.h"
#include "calibrationdialog.h"
#include "contentcontroller.h"
#include "core/imagedocument.h"
#include "jobcontroller.h"
#include "machineprofiles.h"
#include "materialtestdialog.h"

namespace ld::app {

ToolDialogs::ToolDialogs(ld::core::ImageDocument* document, const ld::core::MachineSetup& machine, MachineProfiles* profiles,
                         JobController* jobs, ContentController* content, std::function<double()> pixelSize,
                         std::function<std::pair<double, double>()> frameValues, QWidget* dialogParent, QObject* parent)
    : QObject(parent),
      document_(document),
      machine_(machine),
      profiles_(profiles),
      jobs_(jobs),
      content_(content),
      pixelSize_(std::move(pixelSize)),
      frameValues_(std::move(frameValues)),
      dialogParent_(dialogParent)
{
}

// Materialtest (siehe planung.md 3.20)
void ToolDialogs::openMaterialTest()
{
    if (materialTest_) {
        materialTest_->show();
        materialTest_->raise();
        materialTest_->activateWindow();
        return;
    }
    const double pixel = document_->hasImage() ? document_->settings().pixelSizeMm : pixelSize_();
    auto* dialog = new MaterialTestDialog(machine_, document_->operation(), document_->jobOrigin(), pixel, dialogParent_);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    materialTest_ = dialog;
    connect(dialog, &MaterialTestDialog::startRequested, jobs_, &JobController::startMaterialTest);
    connect(dialog, &MaterialTestDialog::frameRequested, this, [this](const ld::core::MaterialTestSpec& spec) {
        const auto values = frameValues_();
        jobs_->startMaterialTestFrame(spec, values.first, values.second);
    });
    connect(dialog, &MaterialTestDialog::saveGcodeRequested, this, &ToolDialogs::onSaveGcode);
    connect(dialog, &MaterialTestDialog::saveMaterialRequested, this, &ToolDialogs::onSaveMaterial);
    dialog->show();
}

// Kalibrierung (siehe planung.md 3.25)
void ToolDialogs::openCalibration()
{
    if (calibration_) {
        calibration_->show();
        calibration_->raise();
        calibration_->activateWindow();
        return;
    }
    auto* dialog = new CalibrationDialog(machine_, document_->calibrationSpec(), dialogParent_);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    calibration_ = dialog;
    connect(dialog, &CalibrationDialog::insertRequested, this, [this](const ld::core::CalibrationSpec& spec) {
        content_->insertCalibration(spec, calibration_);
    });
    connect(dialog, &CalibrationDialog::saveFactorsRequested, this, &ToolDialogs::onSaveFactors);
    dialog->show();
}

void ToolDialogs::setRunState(bool connected, bool idle)
{
    if (materialTest_) materialTest_->setRunState(connected, idle);
}

void ToolDialogs::onMachineChanged()
{
    if (materialTest_) materialTest_->close();   // der Dialog kennt das alte Maschinenprofil
    if (calibration_) calibration_->setMachine(machine_);
}

void ToolDialogs::onSaveGcode(const ld::core::MaterialTestSpec& spec)
{
    const QString startDir = AppSettings::instance().startFolder(FolderKind::Gcode);
    QString path = QFileDialog::getSaveFileName(
        dialogParent_, tr("G-Code speichern"),
        (startDir.isEmpty() ? QString() : startDir + QLatin1Char('/')) + QStringLiteral("materialtest.nc"),
        tr("G-Code (*.nc *.gcode);;Alle Dateien (*)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".nc");
    AppSettings::instance().setLastFolder(FolderKind::Gcode, path);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(dialogParent_, tr("G-Code speichern"), tr("Die Datei kann nicht geschrieben werden: %1").arg(path));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const ld::core::GcodeResult result = ld::core::generateMaterialTestGcode(
        spec, machine_, [&](const QByteArray& chunk) { return file.write(chunk) == chunk.size(); });
    QApplication::restoreOverrideCursor();
    if (!result.ok || !file.commit()) {
        QMessageBox::warning(dialogParent_, tr("G-Code speichern"), result.error.isEmpty() ? tr("Der G-Code konnte nicht gespeichert werden.") : result.error);
        return;
    }
    emit message(tr("G-Code gespeichert: %1").arg(path), 6000);
}

void ToolDialogs::onSaveMaterial(double speedMmPerMin, double powerPercent, ld::core::EngraveMode mode)
{
    // Die Werte kommen in die Gruppe des Test-Modus (Gravur oder Schnitt) einer Kopie der aktuellen Gravur-Werte.
    ld::core::Operation op = document_->operation();
    if (ld::core::isCutMode(mode) == ld::core::isCutMode(op.mode)) {
        op.speedMmPerMin = speedMmPerMin;
        op.powerPercent = powerPercent;
    } else {
        op.otherValues.speedMmPerMin = speedMmPerMin;
        op.otherValues.powerPercent = powerPercent;
    }
    emit materialRequested(
        op, tr("Materialtest: Speed %1 mm/min, Power %2 %").arg(speedMmPerMin, 0, 'f', 0).arg(powerPercent, 0, 'f', 0));
}

void ToolDialogs::onSaveFactors(std::optional<double> machineX, std::optional<double> machineY)
{
    QWidget* parent = calibration_ ? static_cast<QWidget*>(calibration_.data()) : dialogParent_;
    if (!machineX && !machineY) return;
    ld::core::MachineSetup setup = machine_;
    const QString old = tr("X %1, Y %2").arg(QLocale().toString(setup.correctionX, 'f', 4), QLocale().toString(setup.correctionY, 'f', 4));
    if (machineX) setup.correctionX = *machineX;
    if (machineY) setup.correctionY = *machineY;
    const QString neu = tr("X %1, Y %2").arg(QLocale().toString(setup.correctionX, 'f', 4), QLocale().toString(setup.correctionY, 'f', 4));
    if (QMessageBox::question(parent, tr("Kalibrierung"),
                              tr("Diese Korrekturfaktoren im Maschinenprofil \"%1\" hinterlegen?\n\nbisher: %2\nneu: %3")
                                  .arg(setup.name, old, neu),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes) != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!profiles_->save(setup, &error)) {
        QMessageBox::warning(parent, tr("Kalibrierung"), tr("Das Profil kann nicht gespeichert werden: %1").arg(error));
        return;
    }
    emit machineSaved(setup);
    emit message(tr("Korrektur im Profil \"%1\" gespeichert: %2").arg(setup.name, neu), 8000);
}

}  // namespace ld::app
