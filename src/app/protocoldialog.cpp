#include "protocoldialog.h"
#include "helpwindow.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "protocolcontroller.h"

namespace ld::app {

using ld::core::JobResult;
using ld::core::LogLevel;

namespace {

QTableWidget* makeTable(const QStringList& headers)
{
    auto* table = new QTableWidget(0, static_cast<int>(headers.size()));
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
}

QString shortTime(const QDateTime& time)
{
    return time.isValid() ? time.toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")) : QString();
}

QString duration(int seconds)
{
    return seconds > 0 ? ld::core::formatHms(seconds) : QStringLiteral("–");
}

void addRow(QTableWidget* table, const QStringList& cells, const QColor& color = QColor())
{
    const int row = table->rowCount();
    table->insertRow(row);
    for (int c = 0; c < cells.size(); ++c) {
        auto* item = new QTableWidgetItem(cells[c]);
        item->setToolTip(cells[c]);
        if (color.isValid()) item->setForeground(color);
        table->setItem(row, c, item);
    }
}

}  // namespace

ProtocolDialog::ProtocolDialog(ProtocolController* protocol, QWidget* parent) : QDialog(parent), protocol_(protocol)
{
    setWindowTitle(tr("Protokoll"));
    attachHelp(this, ld::core::helptopic::kProtocol);   // F1 und Knopf "Hilfe" (Handbuch)
    resize(1000, 560);
    auto* main = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Zeitraum")));
    periodCombo_ = new QComboBox;
    periodCombo_->addItem(tr("Heute"), 1);
    periodCombo_->addItem(tr("Letzte 7 Tage"), 7);
    periodCombo_->addItem(tr("Letzte 30 Tage"), 30);
    periodCombo_->addItem(tr("Dieses Jahr"), -1);
    periodCombo_->addItem(tr("Alles"), 0);
    periodCombo_->setCurrentIndex(2);
    top->addWidget(periodCombo_);
    top->addStretch(1);
    main->addLayout(top);

    tabs_ = new QTabWidget;
    main->addWidget(tabs_, 1);

    // ---- Aufträge
    {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        auto* filter = new QHBoxLayout;
        filter->addWidget(new QLabel(tr("Ergebnis")));
        resultFilter_ = new QComboBox;
        resultFilter_->addItem(tr("Alle"), 0);
        resultFilter_->addItem(tr("Nur fertige"), 1);
        resultFilter_->addItem(tr("Nicht fertige (abgebrochen, Fehler …)"), 2);
        filter->addWidget(resultFilter_);
        filter->addStretch(1);
        layout->addLayout(filter);
        jobsTable_ = makeTable({tr("Start"), tr("Ende"), tr("Name"), tr("Art"), tr("Modus"), tr("Ergebnis"), tr("Laufzeit"),
                                tr("Arbeitszeit"), tr("Geschätzt"), tr("Material"), tr("Hinweis")});
        layout->addWidget(jobsTable_, 1);
        tabs_->addTab(page, tr("Aufträge"));
    }
    // ---- System
    {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        auto* filter = new QHBoxLayout;
        filter->addWidget(new QLabel(tr("Stufe")));
        levelFilter_ = new QComboBox;
        levelFilter_->addItem(tr("Alle"), 0);
        levelFilter_->addItem(tr("Warnungen und Fehler"), 1);
        levelFilter_->addItem(tr("Nur Fehler"), 2);
        filter->addWidget(levelFilter_);
        filter->addWidget(new QLabel(tr("Suche")));
        search_ = new QLineEdit;
        search_->setClearButtonEnabled(true);
        search_->setPlaceholderText(tr("Text in Kategorie, Meldung oder Details"));
        filter->addWidget(search_, 1);
        layout->addLayout(filter);
        systemTable_ = makeTable({tr("Zeit"), tr("Stufe"), tr("Kategorie"), tr("Meldung"), tr("Details")});
        layout->addWidget(systemTable_, 1);
        tabs_->addTab(page, tr("System"));
    }
    // ---- Statistik
    {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        summary_ = new QLabel;
        summary_->setWordWrap(true);
        summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(summary_);
        monthTable_ = makeTable({tr("Monat"), tr("Aufträge"), tr("Arbeitszeit"), tr("Laufzeit")});
        layout->addWidget(monthTable_, 1);
        tabs_->addTab(page, tr("Statistik"));
    }

    auto* buttons = new QHBoxLayout;
    auto* openFolder = new QPushButton(tr("Ordner öffnen"));
    openFolder->setToolTip(tr("Zeigt die CSV-Dateien der Protokolle im Dateimanager (je Monat eine Datei, nie automatisch gelöscht)."));
    exportButton_ = new QPushButton(tr("Exportieren …"));
    exportButton_->setToolTip(tr("Schreibt die gerade angezeigten (gefilterten) Zeilen als CSV-Datei für ein Tabellenprogramm."));
    auto* refresh = new QPushButton(tr("Aktualisieren"));
    auto* close = new QPushButton(tr("Schließen"));
    buttons->addWidget(openFolder);
    buttons->addWidget(exportButton_);
    buttons->addWidget(refresh);
    buttons->addStretch(1);
    buttons->addWidget(close);
    main->addLayout(buttons);

    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(refresh, &QPushButton::clicked, this, &ProtocolDialog::reload);
    connect(openFolder, &QPushButton::clicked, this, [this] {
        QDir().mkpath(protocol_->folder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(protocol_->folder()));
    });
    connect(exportButton_, &QPushButton::clicked, this, &ProtocolDialog::exportCsv);
    connect(periodCombo_, &QComboBox::currentIndexChanged, this, &ProtocolDialog::reload);
    connect(resultFilter_, &QComboBox::currentIndexChanged, this, &ProtocolDialog::reload);
    connect(levelFilter_, &QComboBox::currentIndexChanged, this, &ProtocolDialog::reload);
    connect(search_, &QLineEdit::textChanged, this, &ProtocolDialog::reload);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) { exportButton_->setEnabled(index < 2); });
    // Wird etwas protokolliert, frischt sich ein offenes Fenster nach einer kurzen Pause auf (nicht bei jeder Zeile).
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(400);
    connect(&refreshTimer_, &QTimer::timeout, this, &ProtocolDialog::reload);
    connect(protocol_, &ProtocolController::changed, this, [this] {
        if (isVisible()) refreshTimer_.start();
    });
}

void ProtocolDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    reload();
}

QPair<QDate, QDate> ProtocolDialog::period() const
{
    const int days = periodCombo_->currentData().toInt();
    const QDate today = QDate::currentDate();
    if (days == 0) return {QDate(), QDate()};
    if (days < 0) return {QDate(today.year(), 1, 1), QDate()};   // dieses Jahr
    return {today.addDays(-(days - 1)), QDate()};
}

void ProtocolDialog::reload()
{
    fillJobs();
    fillSystem();
    fillStats();
}

void ProtocolDialog::fillJobs()
{
    const auto range = period();
    const int filter = resultFilter_->currentData().toInt();
    shownJobs_.clear();
    jobsTable_->setRowCount(0);
    QList<ld::core::JobRecord> records = protocol_->jobLog().read(range.first, range.second);
    for (auto it = records.rbegin(); it != records.rend(); ++it) {   // neueste zuerst
        const ld::core::JobRecord& r = *it;
        if (filter == 1 && r.result != JobResult::Finished) continue;
        if (filter == 2 && (r.result == JobResult::Finished || r.result == JobResult::Running)) continue;
        shownJobs_.append(r);
        // Rot: Problem (Alarm, Fehler, Verbindung verloren, unterbrochen ...); Orange: vom Bediener beendet (abgebrochen, NOT-AUS).
        const bool operatorStop = r.result == JobResult::CancelledByUser || r.result == JobResult::EmergencyStop;
        const bool problem = r.result != JobResult::Finished && r.result != JobResult::Running && !operatorStop;
        addRow(jobsTable_, {shortTime(r.start), shortTime(r.end), r.name, r.kind, r.mode, ld::core::jobResultText(r.result),
                            duration(r.runSeconds), duration(r.activeSeconds), duration(r.estimateSeconds), r.material, r.note},
               problem ? QColor(0xc0, 0x30, 0x30) : operatorStop ? QColor(0xb0, 0x70, 0x00) : QColor());
    }
    jobsTable_->resizeColumnsToContents();
    tabs_->setTabText(0, tr("Aufträge (%1)").arg(shownJobs_.size()));
}

void ProtocolDialog::fillSystem()
{
    const auto range = period();
    const int level = levelFilter_->currentData().toInt();
    const QString needle = search_->text().trimmed();
    shownSystem_.clear();
    systemTable_->setRowCount(0);
    QList<ld::core::SystemRecord> records = protocol_->systemLog().read(range.first, range.second);
    for (auto it = records.rbegin(); it != records.rend(); ++it) {   // neueste zuerst
        const ld::core::SystemRecord& r = *it;
        if (level == 1 && r.level == LogLevel::Info) continue;
        if (level == 2 && r.level != LogLevel::Error) continue;
        if (!needle.isEmpty() && !r.category.contains(needle, Qt::CaseInsensitive) && !r.message.contains(needle, Qt::CaseInsensitive) &&
            !r.details.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        shownSystem_.append(r);
        addRow(systemTable_, {shortTime(r.time), ld::core::logLevelText(r.level), r.category, r.message, r.details},
               r.level == LogLevel::Error ? QColor(0xc0, 0x30, 0x30) : r.level == LogLevel::Warning ? QColor(0xb0, 0x70, 0x00) : QColor());
    }
    systemTable_->resizeColumnsToContents();
    tabs_->setTabText(1, tr("System (%1)").arg(shownSystem_.size()));
}

void ProtocolDialog::fillStats()
{
    // Die Statistik gilt immer für alle Aufträge (Gesamtbetriebszeit), nicht für den gewählten Zeitraum.
    const ld::core::JobStats s = ld::core::computeJobStats(protocol_->jobLog().read());
    summary_->setText(tr("<b>Aufträge insgesamt:</b> %1 &nbsp; (fertig %2, abgebrochen %3, mit Problem %4, unterbrochen %5)<br>"
                         "<b>Gesamtbetriebszeit (Arbeitszeit):</b> %6 &nbsp; <b>Gesamtlaufzeit:</b> %7")
                          .arg(s.jobs).arg(s.finished).arg(s.cancelled).arg(s.failed).arg(s.interrupted)
                          .arg(ld::core::formatHms(s.activeSeconds), ld::core::formatHms(s.runSeconds)));
    monthTable_->setRowCount(0);
    const QStringList months = s.months.keys();
    for (auto it = months.rbegin(); it != months.rend(); ++it) {   // neuester Monat zuerst
        const ld::core::MonthStats& m = s.months[*it];
        addRow(monthTable_, {*it, QString::number(m.jobs), ld::core::formatHms(m.activeSeconds), ld::core::formatHms(m.runSeconds)});
    }
    monthTable_->resizeColumnsToContents();
}

void ProtocolDialog::exportCsv()
{
    const bool jobs = tabs_->currentIndex() == 0;
    const QString suggestion = jobs ? QStringLiteral("auftraege.csv") : QStringLiteral("system.csv");
    const QString path = QFileDialog::getSaveFileName(this, tr("Protokoll exportieren"), QDir::homePath() + QLatin1Char('/') + suggestion,
                                                      tr("CSV-Datei (*.csv);;Alle Dateien (*)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Protokoll exportieren"), tr("Die Datei kann nicht geschrieben werden: %1").arg(path));
        return;
    }
    file.write("\xEF\xBB\xBF");
    file.write((ld::core::csvLine(jobs ? ld::core::jobCsvHeader() : ld::core::systemCsvHeader()) + QLatin1Char('\n')).toUtf8());
    // Älteste zuerst, wie in den Dateien (die Tabelle zeigt die neuesten oben).
    if (jobs) {
        for (auto it = shownJobs_.rbegin(); it != shownJobs_.rend(); ++it)
            file.write((ld::core::csvLine(ld::core::jobCsvFields(*it)) + QLatin1Char('\n')).toUtf8());
    } else {
        for (auto it = shownSystem_.rbegin(); it != shownSystem_.rend(); ++it)
            file.write((ld::core::csvLine(ld::core::systemCsvFields(*it)) + QLatin1Char('\n')).toUtf8());
    }
}

}  // namespace ld::app
