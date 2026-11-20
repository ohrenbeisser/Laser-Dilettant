// GRBL-Parameter lesen und schreiben ($$ und $n=Wert), siehe docs/planung.md 3.15.
#pragma once

#include <QDialog>
#include <QMap>

class QLabel;
class QTableWidget;

namespace ld::grbl {
class GrblConnection;
}

namespace ld::app {

class GrblSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit GrblSettingsDialog(ld::grbl::GrblConnection* grbl, QWidget* parent = nullptr);

private:
    void reload();
    void fill(const QMap<int, QString>& values);
    void writeChanges();

    ld::grbl::GrblConnection* grbl_;
    QTableWidget* table_;
    QLabel* status_;
    QMap<int, QString> original_;   // zuletzt gelesene Werte, zum Erkennen von Änderungen
};

// Deutsche Beschreibung einer GRBL-Einstellung ($n).
QString grblSettingDescription(int number);

}  // namespace ld::app
