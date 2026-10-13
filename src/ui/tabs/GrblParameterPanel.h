#pragma once

#include <QWidget>

class GrblDriver;
class QLabel;
class QPushButton;
class QTableWidget;

// Table of all $ settings of the connected device (section 9.4).
class GrblParameterPanel : public QWidget
{
    Q_OBJECT
public:
    explicit GrblParameterPanel(GrblDriver* driver, QWidget* parent = nullptr);

    // Saves the current settings to a file chosen by the user. Returns the
    // file name or an empty string. Used by the firmware assistant too.
    static QString backupToFile(GrblDriver* driver, QWidget* parent);

private:
    void rebuild();
    void updateEnabled();
    void writeChanges();
    void restoreFromFile();
    QList<QPair<int, QString>> changedValues() const;

    GrblDriver* m_driver;
    QTableWidget* m_table;
    QLabel* m_info;
    QPushButton* m_read;
    QPushButton* m_write;
    QPushButton* m_backup;
    QPushButton* m_restore;
    bool m_rebuilding = false;
    bool m_busy = false;
};
