// Protokollfenster (siehe docs/planung.md 3.38): zeigt Auftragsprotokoll, Systemprotokoll und die Statistik. Nicht
// modal; es frischt sich selbst auf, wenn etwas protokolliert wird. "Exportieren" schreibt die gerade gezeigten (gefilterten)
// Zeilen als CSV, "Ordner öffnen" zeigt die Dateien im Dateimanager.
#pragma once

#include <QDialog>
#include <QList>
#include <QTimer>

#include "core/protocol.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTabWidget;

namespace ld::app {

class ProtocolController;

class ProtocolDialog : public QDialog {
    Q_OBJECT

public:
    ProtocolDialog(ProtocolController* protocol, QWidget* parent = nullptr);

    void reload();   // liest die Dateien neu und zeigt sie mit den gewählten Filtern

protected:
    void showEvent(QShowEvent* event) override;

private:
    QPair<QDate, QDate> period() const;
    void fillJobs();
    void fillSystem();
    void fillStats();
    void exportCsv();

    ProtocolController* protocol_;
    QTabWidget* tabs_ = nullptr;
    QComboBox* periodCombo_ = nullptr;
    QComboBox* resultFilter_ = nullptr;
    QComboBox* levelFilter_ = nullptr;
    QLineEdit* search_ = nullptr;
    QTableWidget* jobsTable_ = nullptr;
    QTableWidget* systemTable_ = nullptr;
    QLabel* summary_ = nullptr;
    QTableWidget* monthTable_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QList<ld::core::JobRecord> shownJobs_;
    QList<ld::core::SystemRecord> shownSystem_;
    QTimer refreshTimer_;
};

}  // namespace ld::app
