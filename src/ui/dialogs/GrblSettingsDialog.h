#pragma once

#include "device/GrblSettings.h"

#include <QDialog>

class QTableWidget;

// Compares the current GRBL settings with values from a backup and lets the
// user choose which to write (section 9.4, "Aus Datei wiederherstellen").
class GrblSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    GrblSettingsDialog(const GrblSettings& current, const GrblSettings& backup, QWidget* parent = nullptr);

    // Selected settings that differ from the device.
    QList<QPair<int, QString>> selectedChanges() const;
    // Number of backup entries ignored because the device does not know them.
    int ignoredCount() const { return m_ignored; }
    bool hasDifferences() const { return !m_rows.isEmpty(); }

private:
    QTableWidget* m_table;
    QList<QPair<int, QString>> m_rows;
    int m_ignored = 0;
};
