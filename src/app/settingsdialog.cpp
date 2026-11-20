#include "settingsdialog.h"
#include "helpwindow.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QHostAddress>
#include <QInputDialog>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QVBoxLayout>
#include <functional>

#include "core/ntfy.h"
#include "core/webauth.h"
#include "webserver.h"
#include "notificationcontroller.h"

#include "originselector.h"

namespace ld::app {

namespace {

// Vorgaben für "Standard wiederherstellen" (gleich den Anfangswerten in AppSettings).
constexpr int kDefaultAutosaveMinutes = 2;
constexpr int kDefaultWeakMaxPercent = 5;
constexpr int kDefaultWeakAutoOffSeconds = 30;

// Knopf "Standard wiederherstellen" am Ende eines Reiters; setzt nur die Felder dieses Reiters zurück.
QPushButton* addResetButton(QFormLayout* form, QObject* receiver, std::function<void()> reset)
{
    auto* button = new QPushButton(QObject::tr("Standard wiederherstellen"));
    button->setToolTip(QObject::tr("Setzt die Einstellungen dieses Reiters auf die Vorgaben zurück (gilt nach OK)."));
    QObject::connect(button, &QPushButton::clicked, receiver, std::move(reset));
    form->addRow(button);
    return button;
}

// Zeile "Eingabefeld + Wählen …" für einen Ordner. Leer lassen = kein Standardordner.
QWidget* folderRow(QLineEdit*& edit, const QString& current, QWidget* dialog)
{
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    edit = new QLineEdit(current);
    edit->setPlaceholderText(QObject::tr("zuletzt benutzter Ordner"));
    edit->setClearButtonEnabled(true);
    auto* choose = new QPushButton(QObject::tr("Wählen …"));
    layout->addWidget(edit, 1);
    layout->addWidget(choose);
    QLineEdit* field = edit;
    QObject::connect(choose, &QPushButton::clicked, dialog, [field, dialog] {
        const QString start = field->text().isEmpty() ? QString() : field->text();
        const QString folder = QFileDialog::getExistingDirectory(dialog, QObject::tr("Standardordner wählen"), start);
        if (!folder.isEmpty()) field->setText(folder);
    });
    return row;
}

}  // namespace

ColorButton::ColorButton(const QColor& color, QWidget* parent) : QPushButton(parent)
{
    setFixedWidth(64);
    setCursor(Qt::PointingHandCursor);
    setColor(color);
    connect(this, &QPushButton::clicked, this, [this] {
        const QColor chosen = QColorDialog::getColor(color_, this, tr("Farbe wählen"));
        if (chosen.isValid()) {
            setColor(chosen);
            emit colorChanged(chosen);
        }
    });
}

void ColorButton::setColor(const QColor& color)
{
    color_ = color;
    setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid palette(mid); border-radius: 3px; "
                                 "min-height: 20px; }").arg(color.name()));
    setToolTip(color.name());
}

SettingsDialog::SettingsDialog(ld::core::JobOrigin currentOrigin, QWidget* parent)
    : QDialog(parent), initialOrigin_(currentOrigin)
{
    setWindowTitle(tr("Einstellungen"));
    attachHelp(this, ld::core::helptopic::kSettings);   // F1 und Knopf "Hilfe" (Handbuch)
    resize(520, 440);
    const AppSettings& s = AppSettings::instance();
    initialDesign_ = s.design();
    initialLength_ = s.lengthUnit();
    initialSpeed_ = s.speedUnit();
    initialResolution_ = s.resolutionUnit();
    initialColors_ = s.colors();
    initialLanguage_ = s.language();

    auto* main = new QVBoxLayout(this);
    auto* tabs = new QTabWidget;
    main->addWidget(tabs, 1);

    // ------------------------------------------------------------ Allgemein
    {
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);
        designCombo_ = new QComboBox;
        designCombo_->addItem(tr("System"), static_cast<int>(DesignChoice::System));
        designCombo_->addItem(tr("Hell"), static_cast<int>(DesignChoice::Light));
        designCombo_->addItem(tr("Dunkel"), static_cast<int>(DesignChoice::Dark));
        designCombo_->setCurrentIndex(designCombo_->findData(static_cast<int>(s.design())));
        form->addRow(tr("Design"), designCombo_);

        languageCombo_ = new QComboBox;
        languageCombo_->addItem(tr("System"), QStringLiteral("system"));
        languageCombo_->addItem(QStringLiteral("Deutsch"), QStringLiteral("de"));
        languageCombo_->addItem(QStringLiteral("English"), QStringLiteral("en"));
        languageCombo_->setCurrentIndex(qMax(0, languageCombo_->findData(s.language())));
        languageCombo_->setToolTip(tr("Sprache der Oberfläche. Sie wirkt nach einem Neustart."));
        form->addRow(tr("Sprache"), languageCombo_);

        lengthCombo_ = new QComboBox;
        lengthCombo_->addItem(tr("Millimeter (mm)"), static_cast<int>(LengthUnit::Millimeter));
        lengthCombo_->addItem(tr("Zoll (in)"), static_cast<int>(LengthUnit::Inch));
        lengthCombo_->setCurrentIndex(lengthCombo_->findData(static_cast<int>(s.lengthUnit())));
        form->addRow(tr("Längen"), lengthCombo_);

        speedCombo_ = new QComboBox;
        speedCombo_->addItem(tr("Millimeter pro Minute (mm/min)"), static_cast<int>(SpeedUnit::MmPerMin));
        speedCombo_->addItem(tr("Millimeter pro Sekunde (mm/s)"), static_cast<int>(SpeedUnit::MmPerSec));
        speedCombo_->setCurrentIndex(speedCombo_->findData(static_cast<int>(s.speedUnit())));
        form->addRow(tr("Geschwindigkeit"), speedCombo_);

        resolutionCombo_ = new QComboBox;
        resolutionCombo_->addItem(tr("Linien/mm (mit dpi)"), static_cast<int>(ResolutionUnit::LinesPerMm));
        resolutionCombo_->addItem(tr("dpi"), static_cast<int>(ResolutionUnit::Dpi));
        resolutionCombo_->addItem(tr("Pixelgröße (mm)"), static_cast<int>(ResolutionUnit::PixelSize));
        resolutionCombo_->setCurrentIndex(resolutionCombo_->findData(static_cast<int>(s.resolutionUnit())));
        resolutionCombo_->setToolTip(tr("Wie die Auflösung des Bildes angezeigt wird (Rechtes Panel unter Qualität, Bild bearbeiten). "
                                        "Linien/mm = 1 ÷ Pixelgröße; 15,7 Linien/mm sind 400 dpi."));
        form->addRow(tr("Auflösung"), resolutionCombo_);

        startWarningCheck_ = new QCheckBox(tr("Warnhinweis vor dem Start anzeigen"));
        startWarningCheck_->setChecked(s.showStartWarning());
        form->addRow(startWarningCheck_);
        autoConnectCheck_ = new QCheckBox(tr("Beim Start automatisch mit dem Laser verbinden"));
        autoConnectCheck_->setChecked(s.autoConnect());
        autoConnectCheck_->setToolTip(tr("Verbindet mit dem zuletzt benutzten Port, wenn er vorhanden ist."));
        form->addRow(autoConnectCheck_);

        auto* note = new QLabel(tr("Der G-Code enthält immer Millimeter und mm/min, unabhängig von dieser Einstellung."));
        note->setWordWrap(true);
        note->setEnabled(false);
        form->addRow(note);
        addResetButton(form, this, [this] {
            designCombo_->setCurrentIndex(designCombo_->findData(static_cast<int>(DesignChoice::System)));
            languageCombo_->setCurrentIndex(languageCombo_->findData(QStringLiteral("system")));
            lengthCombo_->setCurrentIndex(lengthCombo_->findData(static_cast<int>(LengthUnit::Millimeter)));
            speedCombo_->setCurrentIndex(speedCombo_->findData(static_cast<int>(SpeedUnit::MmPerMin)));
            resolutionCombo_->setCurrentIndex(resolutionCombo_->findData(static_cast<int>(ResolutionUnit::LinesPerMm)));
            startWarningCheck_->setChecked(true);
            autoConnectCheck_->setChecked(true);   // Vorgabe seit 2026-11-18: an (planung.md 3.15)
        });
        tabs->addTab(page, tr("Allgemein"));
    }

    // ------------------------------------------------------------ Anzeige
    {
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);
        const DisplayColors& c = s.colors();
        progressButton_ = new ColorButton(c.progress);
        burnButton_ = new ColorButton(c.burn);
        travelButton_ = new ColorButton(c.travel);
        rapidButton_ = new ColorButton(c.rapid);
        imageBurnButton_ = new ColorButton(c.imageBurn);
        imagePaperButton_ = new ColorButton(c.imagePaper);
        form->addRow(tr("Gelasertes (Simulation)"), progressButton_);
        form->addRow(tr("Laserweg: Gravur"), burnButton_);
        form->addRow(tr("Laserweg: Leerfahrt"), travelButton_);
        form->addRow(tr("Laserweg: Eilgang"), rapidButton_);
        form->addRow(tr("Bild: gebrannt"), imageBurnButton_);
        form->addRow(tr("Bild: Papier"), imagePaperButton_);

        gridCheck_ = new QCheckBox(tr("Raster anzeigen"));
        gridCheck_->setChecked(c.gridVisible);
        form->addRow(gridCheck_);
        gridColorButton_ = new ColorButton(c.grid);
        form->addRow(tr("Rasterfarbe"), gridColorButton_);
        gridStrength_ = new QSlider(Qt::Horizontal);
        gridStrength_->setRange(0, 100);
        gridStrength_->setValue(c.gridStrength);
        gridStrength_->setToolTip(tr("Wie deutlich das Raster zu sehen ist"));
        form->addRow(tr("Rasterstärke"), gridStrength_);
        paperOpacity_ = new QSlider(Qt::Horizontal);
        paperOpacity_->setRange(0, 100);
        paperOpacity_->setValue(c.paperOpacity);
        paperOpacity_->setToolTip(tr("Deckkraft des weißen Grundes hinter dem Bild auf der Arbeitsfläche. Bei weniger "
                                     "scheint das Raster stärker durch; die Farben des Bildes bleiben immer deckend."));
        form->addRow(tr("Deckkraft des Grundes"), paperOpacity_);

        addResetButton(form, this, [this] { setColorControls(DisplayColors{}); });
        tabs->addTab(page, tr("Anzeige"));
    }

    // ------------------------------------------------------------ Dateien
    {
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);
        autosaveSpin_ = new QSpinBox;
        autosaveSpin_->setRange(0, 60);
        autosaveSpin_->setSuffix(tr(" min"));
        autosaveSpin_->setSpecialValueText(tr("aus"));
        autosaveSpin_->setValue(s.autosaveMinutes());
        autosaveSpin_->setToolTip(tr("Alle so viele Minuten wird bei Änderungen eine Sicherung angelegt, "
                                     "damit nach einem Absturz nichts verloren geht."));
        form->addRow(tr("Sicherung alle"), autosaveSpin_);
        form->addRow(new QLabel(tr("<b>Standardordner</b> für die Dateidialoge")));
        form->addRow(tr("Bilder"), folderRow(imageDirEdit_, s.defaultFolder(FolderKind::Images), this));
        form->addRow(tr("Projekte"), folderRow(projectDirEdit_, s.defaultFolder(FolderKind::Projects), this));
        form->addRow(tr("G-Code speichern"), folderRow(gcodeDirEdit_, s.defaultFolder(FolderKind::Gcode), this));
        form->addRow(tr("NC-Dateien laden"), folderRow(ncDirEdit_, s.defaultFolder(FolderKind::Nc), this));
        auto* folderNote = new QLabel(tr("Ein Dateidialog beginnt im Standardordner. Ist keiner eingetragen (oder gibt es ihn nicht "
                                         "mehr), beginnt er im zuletzt benutzten Ordner."));
        folderNote->setWordWrap(true);
        folderNote->setEnabled(false);
        form->addRow(folderNote);
        auto* clearRecent = new QPushButton(tr("Zuletzt geöffnete Projekte leeren"));
        clearRecent->setEnabled(!s.recentProjects().isEmpty());
        connect(clearRecent, &QPushButton::clicked, this, [clearRecent] {
            AppSettings::instance().clearRecentProjects();
            clearRecent->setEnabled(false);
        });
        form->addRow(clearRecent);
        addResetButton(form, this, [this] {
            autosaveSpin_->setValue(kDefaultAutosaveMinutes);
            for (QLineEdit* edit : {imageDirEdit_, projectDirEdit_, gcodeDirEdit_, ncDirEdit_}) edit->clear();
        });
        tabs->addTab(page, tr("Dateien"));
    }

    // ------------------------------------------------------------ Laser
    {
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);
        form->addRow(new QLabel(tr("<b>Job-Ursprung</b>")));
        originSelector_ = new OriginSelector;
        originSelector_->setOrigin(currentOrigin);
        form->addRow(tr("Punkt"), originSelector_);
        auto* originNote = new QLabel(tr("Dieser Punkt des Bildes liegt auf dem Nullpunkt. Von hier aus zählen auch die Lineale "
                                         "und die Maße: Bei einer Ecke wachsen die Zahlen ins Bild hinein, bei der Mitte "
                                         "zählt X nach rechts und Y nach oben, auf der anderen Seite mit Minus. "
                                         "Die Wahl gilt für das aktuelle Bild und als Standard für neu geladene Bilder."));
        originNote->setWordWrap(true);
        originNote->setEnabled(false);
        form->addRow(originNote);
        form->addRow(new QLabel(tr("<b>Schwachlicht</b>")));
        weakMaxSpin_ = new QSpinBox;
        weakMaxSpin_->setRange(1, 20);
        weakMaxSpin_->setSuffix(tr(" %"));
        weakMaxSpin_->setValue(s.weakLightMaxPercent());
        weakMaxSpin_->setToolTip(tr("Das Schwachlicht lässt sich höchstens bis zu dieser Leistung einstellen"));
        form->addRow(tr("Obergrenze"), weakMaxSpin_);
        weakAutoOffCombo_ = new QComboBox;
        for (const int seconds : {10, 30, 60, 120, 0}) {
            weakAutoOffCombo_->addItem(seconds == 0 ? tr("nie") : seconds >= 60 ? tr("%1 min").arg(seconds / 60) : tr("%1 s").arg(seconds), seconds);
        }
        weakAutoOffCombo_->setCurrentIndex(qMax(0, weakAutoOffCombo_->findData(s.weakLightAutoOffSeconds())));
        form->addRow(tr("Auto-Aus"), weakAutoOffCombo_);
        auto* note = new QLabel(tr("Das Schwachlicht schaltet sich nach dieser Zeit selbst aus; sie startet bei jedem Jog neu."));
        note->setWordWrap(true);
        note->setEnabled(false);
        form->addRow(note);
        addResetButton(form, this, [this] {
            originSelector_->setOrigin(ld::core::JobOrigin::TopLeft);
            weakMaxSpin_->setValue(kDefaultWeakMaxPercent);
            weakAutoOffCombo_->setCurrentIndex(qMax(0, weakAutoOffCombo_->findData(kDefaultWeakAutoOffSeconds)));
        });
        tabs->addTab(page, tr("Laser"));
    }

    // ------------------------------------------------------------ Bedienung
    {
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);
        form->addRow(new QLabel(tr("<b>Jog-Schritte</b> (Steuerung im linken Panel)")));
        auto* stepRow = new QWidget;
        auto* stepLayout = new QHBoxLayout(stepRow);
        stepLayout->setContentsMargins(0, 0, 0, 0);
        const QList<double> steps = s.jogSteps();
        for (int i = 0; i < 4; ++i) {
            jogStepSpins_[i] = new QDoubleSpinBox;
            jogStepSpins_[i]->setRange(0.01, 500.0);
            jogStepSpins_[i]->setDecimals(2);
            jogStepSpins_[i]->setValue(steps.value(i));
            jogStepSpins_[i]->setToolTip(tr("Schrittweite in mm. Die vier Werte werden beim Speichern aufsteigend sortiert."));
            stepLayout->addWidget(jogStepSpins_[i]);
        }
        stepLayout->addWidget(new QLabel(tr("mm")));
        form->addRow(tr("Schritte"), stepRow);
        jogDefaultCombo_ = new QComboBox;
        form->addRow(tr("Vorgewählt"), jogDefaultCombo_);
        // Die Auswahl zeigt die aktuellen Werte der vier Felder.
        auto refreshDefaults = [this] {
            const int keep = jogDefaultCombo_->currentIndex();
            jogDefaultCombo_->clear();
            for (QDoubleSpinBox* spin : jogStepSpins_) jogDefaultCombo_->addItem(QLocale().toString(spin->value(), 'g', 4) + tr(" mm"));
            jogDefaultCombo_->setCurrentIndex(qMax(0, keep));
        };
        refreshDefaults();
        jogDefaultCombo_->setCurrentIndex(s.jogDefaultIndex());
        for (QDoubleSpinBox* spin : jogStepSpins_) connect(spin, &QDoubleSpinBox::valueChanged, this, refreshDefaults);

        form->addRow(new QLabel(tr("<b>Konsole</b>")));
        consoleStatusCheck_ = new QCheckBox(tr("Statusabfragen von GRBL standardmäßig anzeigen"));
        consoleStatusCheck_->setChecked(s.consoleShowStatus());
        consoleStatusCheck_->setToolTip(tr("GRBL meldet mehrmals pro Sekunde seinen Status. Aus: Diese Zeilen sind ausgeblendet "
                                           "(der Haken in der Konsole selbst lässt sich jederzeit umschalten)."));
        form->addRow(consoleStatusCheck_);
        consoleLinesSpin_ = new QSpinBox;
        consoleLinesSpin_->setRange(500, 20000);
        consoleLinesSpin_->setSingleStep(500);
        consoleLinesSpin_->setSuffix(tr(" Zeilen"));
        consoleLinesSpin_->setValue(s.consoleMaxLines());
        consoleLinesSpin_->setToolTip(tr("So viele Zeilen behält die Konsole; ältere fallen weg."));
        form->addRow(tr("Verlauf"), consoleLinesSpin_);
        addResetButton(form, this, [this] {
            const double defaults[4] = {0.1, 1.0, 10.0, 100.0};
            for (int i = 0; i < 4; ++i) jogStepSpins_[i]->setValue(defaults[i]);
            jogDefaultCombo_->setCurrentIndex(2);
            consoleStatusCheck_->setChecked(false);
            consoleLinesSpin_->setValue(3000);
        });
        tabs->addTab(page, tr("Bedienung"));
    }

    // ------------------------------------------------------------ Benachrichtigung (ntfy, planung.md 3.40)
    {
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);
        ntfyEnabledCheck_ = new QCheckBox(tr("Benachrichtigung über ntfy aufs Handy senden"));
        ntfyEnabledCheck_->setChecked(s.ntfyEnabled());
        form->addRow(ntfyEnabledCheck_);
        auto* info = new QLabel(tr("Installiere die App <b>ntfy</b> auf dem Handy und abonniere dort das Thema unten. Fehler, Alarme und "
                                   "Verbindungsverlust werden immer gemeldet, \"Auftrag fertig\" nur mit dem Haken. Die Nachrichten "
                                   "enthalten keine Dateinamen. Der Themenname ist wie ein Passwort: Wer ihn kennt, liest mit. Mit dem "
                                   "öffentlichen Server ntfy.sh laufen die Nachrichten über das Internet."));
        info->setWordWrap(true);
        form->addRow(info);
        ntfyServerEdit_ = new QLineEdit(s.ntfyServer());
        ntfyServerEdit_->setToolTip(tr("Vorgabe: https://ntfy.sh. Bei einem eigenen Server dessen Adresse eintragen."));
        form->addRow(tr("Server"), ntfyServerEdit_);
        auto* topicRow = new QWidget;
        auto* topicLayout = new QHBoxLayout(topicRow);
        topicLayout->setContentsMargins(0, 0, 0, 0);
        ntfyTopicEdit_ = new QLineEdit(s.ntfyTopic());
        auto* newTopic = new QPushButton(tr("Neu erzeugen"));
        newTopic->setToolTip(tr("Bildet den Namen \"laser-dilettant-\" mit einer zufälligen sechsstelligen Zahl. Eigene Namen sind erlaubt (Buchstaben, Ziffern, - und _)."));
        topicLayout->addWidget(ntfyTopicEdit_, 1);
        topicLayout->addWidget(newTopic);
        form->addRow(tr("Thema"), topicRow);
        ntfyFinishedCheck_ = new QCheckBox(tr("Auch melden, wenn ein Auftrag fertig ist"));
        ntfyFinishedCheck_->setChecked(s.ntfyNotifyFinished());
        ntfyFinishedCheck_->setToolTip(tr("Aus: Es kommen nur Fehler, Alarme und Verbindungsverlust. Der Rahmen meldet nie \"fertig\"."));
        form->addRow(ntfyFinishedCheck_);
        ntfyTestButton_ = new QPushButton(tr("Testnachricht senden"));
        ntfyTestButton_->setEnabled(false);   // erst mit setNotifier()
        ntfyResultLabel_ = new QLabel;
        ntfyResultLabel_->setWordWrap(true);
        form->addRow(ntfyTestButton_, ntfyResultLabel_);

        // Ohne Haken sind die Felder ausgegraut; beim Einschalten bekommt ein leeres Thema gleich einen zufälligen Namen.
        auto updateFields = [this] {
            const bool on = ntfyEnabledCheck_->isChecked();
            for (QWidget* w : {static_cast<QWidget*>(ntfyServerEdit_), static_cast<QWidget*>(ntfyTopicEdit_),
                               static_cast<QWidget*>(ntfyFinishedCheck_)})
                w->setEnabled(on);
            if (on && ntfyTopicEdit_->text().trimmed().isEmpty()) ntfyTopicEdit_->setText(ld::core::generateNtfyTopic());
        };
        connect(ntfyEnabledCheck_, &QCheckBox::toggled, this, updateFields);
        updateFields();
        connect(newTopic, &QPushButton::clicked, this, [this] { ntfyTopicEdit_->setText(ld::core::generateNtfyTopic()); });
        connect(ntfyTestButton_, &QPushButton::clicked, this, [this] {
            if (!notifier_) return;
            ntfyResultLabel_->setText(tr("Wird gesendet …"));
            notifier_->sendTest(ntfyServerEdit_->text(), ntfyTopicEdit_->text().trimmed());
        });
        addResetButton(form, this, [this] {
            ntfyEnabledCheck_->setChecked(false);
            ntfyServerEdit_->setText(ld::core::defaultNtfyServer());
            ntfyFinishedCheck_->setChecked(true);   // das Thema bleibt (sonst müsste das Handy neu abonnieren)
        });
        tabs->addTab(page, tr("Benachrichtigung"));
    }

    // ------------------------------------------------------------ Webzugriff (Fernansicht, planung.md 3.41)
    {
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);
        webEnabledCheck_ = new QCheckBox(tr("Fernansicht im Heimnetz anbieten (Webseite für Handy und Tablet)"));
        webEnabledCheck_->setChecked(s.webEnabled());
        form->addRow(webEnabledCheck_);
        auto* info = new QLabel(tr("Auf dem Handy im selben WLAN die Adresse unten im Browser öffnen, dann mit dem Passwort anmelden. Die "
                                   "Seite zeigt Fortschritt, Laufzeit und Restzeit. <b>Es ist keine verschlüsselte Verbindung (HTTP):</b> "
                                   "Das Passwort geht im Heimnetz unverschlüsselt über die Leitung. Nur in einem vertrauten WLAN benutzen. "
                                   "Anfragen von außerhalb des Heimnetzes werden abgewiesen. Die Fernansicht ersetzt nicht die Aufsicht."));
        info->setWordWrap(true);
        form->addRow(info);
        webPortSpin_ = new QSpinBox;
        webPortSpin_->setRange(1024, 65535);
        webPortSpin_->setValue(s.webPort());
        form->addRow(tr("Port"), webPortSpin_);
        auto* passwordRow = new QWidget;
        auto* passwordLayout = new QHBoxLayout(passwordRow);
        passwordLayout->setContentsMargins(0, 0, 0, 0);
        webPasswordLabel_ = new QLabel(s.webPasswordRecord().isSet() ? tr("gesetzt") : tr("noch nicht gesetzt"));
        auto* setPassword = new QPushButton(tr("Passwort setzen …"));
        passwordLayout->addWidget(webPasswordLabel_, 1);
        passwordLayout->addWidget(setPassword);
        form->addRow(tr("Passwort"), passwordRow);
        connect(setPassword, &QPushButton::clicked, this, [this] {
            bool ok = false;
            const QString first = QInputDialog::getText(this, tr("Passwort der Fernansicht"),
                                                        tr("Neues Passwort:"),
                                                        QLineEdit::Password, QString(), &ok);
            if (!ok) return;
            if (first.isEmpty()) {   // keine Mindestlänge (Auftraggeber, 2026-11-20), aber ein leeres Passwort ist keins
                QMessageBox::warning(this, tr("Passwort"), tr("Das Passwort darf nicht leer sein."));
                return;
            }
            const QString second = QInputDialog::getText(this, tr("Passwort der Fernansicht"), tr("Zur Kontrolle noch einmal eingeben:"),
                                                         QLineEdit::Password, QString(), &ok);
            if (!ok) return;
            if (first != second) {
                QMessageBox::warning(this, tr("Passwort"), tr("Die beiden Eingaben stimmen nicht überein."));
                return;
            }
            newWebPassword_ = first;
            webPasswordLabel_->setText(tr("neu (gilt nach OK)"));
        });
        webPreviewCombo_ = new QComboBox;
        for (int seconds : AppSettings::webPreviewChoices()) webPreviewCombo_->addItem(tr("%1 s").arg(seconds), seconds);
        webPreviewCombo_->setCurrentIndex(qMax(0, webPreviewCombo_->findData(s.webPreviewSeconds())));
        webPreviewCombo_->setToolTip(tr("So oft holt die Seite das Bild des schon Gebrannten ab, solange sie sichtbar ist. "
                                        "Längere Abstände schonen Handy-Akku und Rechner."));
        form->addRow(tr("Bild aktualisieren alle"), webPreviewCombo_);
        webControlCheck_ = new QCheckBox(tr("Fernsteuerung erlauben (Pause, Weiter, Abbrechen, Speed und Leistung)"));
        webControlCheck_->setChecked(s.webControlAllowed());
        webControlCheck_->setToolTip(tr("Aus: Das Handy kann nur zusehen. Ändert sich nur nach OK."));
        form->addRow(webControlCheck_);
        // Adressen, unter denen der Rechner im Heimnetz erreichbar ist
        QStringList urls;
        for (const QHostAddress& a : QNetworkInterface::allAddresses()) {
            if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback() && ld::app::isPrivateAddress(a))
                urls << QStringLiteral("http://%1:%2").arg(a.toString()).arg(s.webPort());
        }
        auto* urlLabel = new QLabel(urls.isEmpty() ? tr("(keine Heimnetz-Adresse gefunden)") : urls.join(QStringLiteral("\n")));
        urlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(tr("Adresse"), urlLabel);
        addResetButton(form, this, [this] {
            webEnabledCheck_->setChecked(false);
            webPortSpin_->setValue(8765);
            webControlCheck_->setChecked(false);   // das Passwort bleibt
            webPreviewCombo_->setCurrentIndex(qMax(0, webPreviewCombo_->findData(3)));
        });
        tabs->addTab(page, tr("Webzugriff"));
    }

    // Design, Einheiten und Anzeige wirken sofort (man sieht, was man wählt); "Abbrechen" nimmt sie zurück.
    for (QComboBox* combo : {designCombo_, lengthCombo_, speedCombo_, resolutionCombo_}) {
        connect(combo, &QComboBox::currentIndexChanged, this, [this] { applyLive(); });
    }
    for (ColorButton* button : {progressButton_, burnButton_, travelButton_, rapidButton_, imageBurnButton_, imagePaperButton_,
                                gridColorButton_}) {
        connect(button, &ColorButton::colorChanged, this, [this] { applyLive(); });
    }
    connect(gridCheck_, &QCheckBox::toggled, this, [this] { applyLive(); });
    connect(gridStrength_, &QSlider::valueChanged, this, [this] { applyLive(); });
    connect(paperOpacity_, &QSlider::valueChanged, this, [this] { applyLive(); });
    live_ = true;

    auto* buttons = new QDialogButtonBox;
    buttons->addButton(tr("OK"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Abbrechen"), QDialogButtonBox::RejectRole);
    main->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ld::core::JobOrigin SettingsDialog::jobOrigin() const
{
    return originSelector_->origin();
}

bool SettingsDialog::jobOriginChanged() const
{
    return originSelector_->origin() != initialOrigin_;
}

bool SettingsDialog::languageChanged() const
{
    return languageCombo_->currentData().toString() != initialLanguage_;
}

void SettingsDialog::setColorControls(const DisplayColors& c)
{
    progressButton_->setColor(c.progress);
    burnButton_->setColor(c.burn);
    travelButton_->setColor(c.travel);
    rapidButton_->setColor(c.rapid);
    imageBurnButton_->setColor(c.imageBurn);
    imagePaperButton_->setColor(c.imagePaper);
    gridCheck_->setChecked(c.gridVisible);
    gridColorButton_->setColor(c.grid);
    gridStrength_->setValue(c.gridStrength);
    paperOpacity_->setValue(c.paperOpacity);
    applyLive();   // das Setzen der Knopffarben im Code meldet nichts: Darum hier selbst anwenden
}

DisplayColors SettingsDialog::colorsFromControls() const
{
    DisplayColors c;
    c.progress = progressButton_->color();
    c.burn = burnButton_->color();
    c.travel = travelButton_->color();
    c.rapid = rapidButton_->color();
    c.imageBurn = imageBurnButton_->color();
    c.imagePaper = imagePaperButton_->color();
    c.grid = gridColorButton_->color();
    c.gridStrength = gridStrength_->value();
    c.gridVisible = gridCheck_->isChecked();
    c.paperOpacity = paperOpacity_->value();
    return c;
}

void SettingsDialog::applyLive()
{
    if (!live_) return;
    AppSettings& s = AppSettings::instance();
    s.setDesign(static_cast<DesignChoice>(designCombo_->currentData().toInt()));
    s.setLengthUnit(static_cast<LengthUnit>(lengthCombo_->currentData().toInt()));
    s.setSpeedUnit(static_cast<SpeedUnit>(speedCombo_->currentData().toInt()));
    s.setResolutionUnit(static_cast<ResolutionUnit>(resolutionCombo_->currentData().toInt()));
    s.setColors(colorsFromControls());
}

void SettingsDialog::reject()
{
    // Abbrechen: alles, was schon sichtbar wirkte, wieder auf den Stand beim Öffnen setzen.
    AppSettings& s = AppSettings::instance();
    s.setDesign(initialDesign_);
    s.setLengthUnit(initialLength_);
    s.setSpeedUnit(initialSpeed_);
    s.setResolutionUnit(initialResolution_);
    s.setColors(initialColors_);
    QDialog::reject();
}

void SettingsDialog::setNotifier(NotificationController* notifier)
{
    notifier_ = notifier;
    ntfyTestButton_->setEnabled(notifier_ != nullptr);
    if (notifier_) {
        connect(notifier_, &NotificationController::testFinished, this, [this](bool ok, const QString& text) {
            ntfyResultLabel_->setText(ok ? tr("Gesendet. Kommt die Nachricht auf dem Handy an?") : tr("Fehlgeschlagen: %1").arg(text));
        });
    }
}

void SettingsDialog::accept()
{
    if (webEnabledCheck_->isChecked() && newWebPassword_.isEmpty() && !AppSettings::instance().webPasswordRecord().isSet()) {
        QMessageBox::warning(this, tr("Webzugriff"), tr("Bitte zuerst ein Passwort setzen. Ohne Passwort bleibt die Fernansicht aus."));
        return;
    }
    if (ntfyEnabledCheck_->isChecked() && !ld::core::isValidNtfyTopic(ntfyTopicEdit_->text().trimmed())) {
        QMessageBox::warning(this, tr("Benachrichtigung"),
                             tr("Der Themenname ist ungültig. Erlaubt sind Buchstaben, Ziffern, - und _ (höchstens 64 Zeichen)."));
        return;
    }
    applyLive();   // (meist schon geschehen)
    AppSettings& s = AppSettings::instance();
    s.setLanguage(languageCombo_->currentData().toString());
    QList<double> jogSteps;
    for (QDoubleSpinBox* spin : jogStepSpins_) jogSteps.append(spin->value());
    s.setJog(jogSteps, jogDefaultCombo_->currentIndex());
    s.setConsole(consoleStatusCheck_->isChecked(), consoleLinesSpin_->value());
    s.setDefaultFolder(FolderKind::Images, imageDirEdit_->text().trimmed());
    s.setDefaultFolder(FolderKind::Projects, projectDirEdit_->text().trimmed());
    s.setDefaultFolder(FolderKind::Gcode, gcodeDirEdit_->text().trimmed());
    s.setDefaultFolder(FolderKind::Nc, ncDirEdit_->text().trimmed());
    s.setAutosaveMinutes(autosaveSpin_->value());
    s.setShowStartWarning(startWarningCheck_->isChecked());
    s.setAutoConnect(autoConnectCheck_->isChecked());
    s.setWebEnabled(webEnabledCheck_->isChecked());
    s.setWebPort(webPortSpin_->value());
    s.setWebControlAllowed(webControlCheck_->isChecked());
    s.setWebPreviewSeconds(webPreviewCombo_->currentData().toInt());
    if (!newWebPassword_.isEmpty()) s.setWebPassword(newWebPassword_);
    s.setNtfyEnabled(ntfyEnabledCheck_->isChecked());
    s.setNtfyServer(ld::core::normalizeNtfyServer(ntfyServerEdit_->text()));
    s.setNtfyTopic(ntfyTopicEdit_->text().trimmed());
    s.setNtfyNotifyFinished(ntfyFinishedCheck_->isChecked());
    s.setWeakLightMaxPercent(weakMaxSpin_->value());
    s.setWeakLightAutoOffSeconds(weakAutoOffCombo_->currentData().toInt());
    QDialog::accept();
}

}  // namespace ld::app
