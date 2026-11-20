#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDockWidget>
#include <QTemporaryFile>
#include <QTextEdit>
#include <QSlider>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QKeyEvent>
#include <QComboBox>
#include <QCheckBox>
#include <QAbstractSpinBox>
#include <QAbstractButton>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QSaveFile>
#include <QHeaderView>
#include <QTableWidget>
#include <QSettings>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QStyleHints>

#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
#include <QUndoStack>

#include "canvasview.h"
#include "imagedialog.h"
#include <QProgressBar>
#include <QProgressDialog>

#include "core/gcoderunner.h"
#include "core/imagedocument.h"
#include "core/pathpreview.h"
#include "core/simulation.h"
#include "aboutdialog.h"
#include "appsettings.h"
#include "consolewidget.h"
#include "firmwaredialog.h"
#include "grblsettingsdialog.h"
#include "helpwindow.h"
#include "grbl/baudtester.h"
#include "connectioncontroller.h"
#include "protocolcontroller.h"
#include "notificationcontroller.h"
#include "remotecontroller.h"
#include "protocoldialog.h"
#include "contentcontroller.h"
#include "tooldialogs.h"
#include "jogcontroller.h"
#include "portfactory.h"
#include "machineprofiles.h"
#include "machineprofilesdialog.h"
#include "projectcontroller.h"
#include "settingsdialog.h"
#include "core/imageloader.h"
#include "core/objecttransform.h"
#include "leftpanel.h"
#include "materialtestdialog.h"
#include "textdialog.h"
#include "calibrationdialog.h"
#include "core/calibration.h"
#include "core/textmodel.h"
#include "previewbar.h"
#include "theme.h"
#include "rightpanel.h"

namespace ld::app {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Laser-Dilettant"));
    setMinimumSize(1000, 600);
    resize(1280, 720);  // Vorgabe; der gemerkte Zustand (unten) überschreibt sie

    // Das Dokument (Bild + Größe/Drehung/Spiegelung + Undo) wird von Arbeitsfläche und rechtem
    // Panel gemeinsam benutzt; es muss deshalb zuerst existieren.
    document_ = new ld::core::ImageDocument(this);
    // Maschinenprofile: Beim ersten Start entsteht das Standardprofil (aus den bisherigen Einstellungen).
    profiles_ = new MachineProfiles(MachineProfiles::defaultFolder(), this);
    profiles_->ensureDefault();
    machine_ = profiles_->active();
    saver_ = new ld::core::GcodeRunner(0, this);
    pathPreview_ = new ld::core::PathPreview(this);
    simulation_ = new ld::core::Simulation(this);
    ncLoader_ = new ld::core::NcLoader(this);
    connect(ncLoader_, &ld::core::NcLoader::finished, this,
            [this](const ld::core::NcAnalysis& result, const QString& src, const QString& dst) { onNcChecked(result, src, dst, ncSilent_); });
    connect(ncLoader_, &ld::core::NcLoader::progress, this, [this](double fraction) {
        if (ncProgress_) ncProgress_->setValue(static_cast<int>(fraction * 100.0));
    });

    // Reihenfolge: Die Docks müssen vor den Menüs existieren, weil das Menü "Ansicht"
    // die Ein-/Ausblende-Aktionen der Docks benutzt.
    createCentralArea();
    createDocks();
    // Projekt: Neu, Öffnen, Speichern, Rückfrage, Sicherung (siehe planung.md 3.13).
    projectController_ = new ProjectController(document_, this, [this] { return leftPanel_->currentMaterialName(); }, this);
    projectController_->setAutosaveMinutes(AppSettings::instance().autosaveMinutes());
    connect(&AppSettings::instance(), &AppSettings::autosaveChanged, this,
            [this] { projectController_->setAutosaveMinutes(AppSettings::instance().autosaveMinutes()); });
    connect(projectController_, &ProjectController::pathChanged, this, &MainWindow::updateWindowTitle);
    connect(document_, &ld::core::ImageDocument::modifiedChanged, this, &MainWindow::updateWindowTitle);
    createMenus();
    createCornerWidget();
    createStatusBar();

    connect(document_, &ld::core::ImageDocument::transformChanged, this, &MainWindow::updateSizeWarning);
    connect(document_, &ld::core::ImageDocument::imageReplaced, this, &MainWindow::updateWindowTitle);
    connect(document_, &ld::core::ImageDocument::imageReplaced, this, [this] { if (ncMode_) leaveNcMode(); });   // ein neues Bild beendet den NC-Auftrag
    updateSizeWarning();

    connect(saver_, &ld::core::GcodeRunner::finished, this, &MainWindow::onSaveFinished);

    // Vorschau und Simulation (M5): Jede Änderung am Auftrag verwirft die temporäre Datei und die
    // Wege; sie werden bei Bedarf neu erzeugt.
    connect(document_, &ld::core::ImageDocument::rasterChanged, this, &MainWindow::onJobChanged);
    connect(document_, &ld::core::ImageDocument::transformChanged, this, &MainWindow::onJobChanged);
    connect(document_, &ld::core::ImageDocument::engravingChanged, this, &MainWindow::onJobChanged);
    // Die Messgrafik besteht nur aus Linien: In Full würde nichts gebrannt, in Linie als Raster. Einmal hinweisen.
    connect(document_, &ld::core::ImageDocument::engravingChanged, this, [this] {
        const bool bad = document_->calibrationSpec() && document_->operation().mode != ld::core::EngraveMode::Cutting;
        if (bad && !calibrationModeWarned_) {
            statusBar()->showMessage(
                document_->operation().mode == ld::core::EngraveMode::Full
                    ? tr("Die Messgrafik besteht nur aus Linien: In Full wird nichts gebrannt. Bitte Cutting wählen.")
                    : tr("Die Messgrafik besteht aus Linien: Für saubere Linien bitte Cutting wählen."),
                10000);
        }
        calibrationModeWarned_ = bad;
    });
    connect(pathPreview_, &ld::core::PathPreview::stateChanged, this, &MainWindow::onPreviewStateChanged);
    connect(previewBar_, &PreviewBar::infoRequested, this, &MainWindow::showPreviewInfo);
    connect(previewBar_, &PreviewBar::zoomInRequested, canvas_, &CanvasView::zoomIn);
    connect(previewBar_, &PreviewBar::zoomOutRequested, canvas_, &CanvasView::zoomOut);
    connect(previewBar_, &PreviewBar::fitRequested, canvas_, &CanvasView::fitToImage);
    connect(previewBar_, &PreviewBar::simulationClicked, this, &MainWindow::onSimulationClicked);
    connect(previewBar_, &PreviewBar::simulationPauseClicked, this, &MainWindow::onSimulationPauseClicked);
    connect(previewBar_, &PreviewBar::pathLayersChanged, this, [this] {
        canvas_->setPathLayers(previewBar_->showBurn(), previewBar_->showTravel(), previewBar_->showRapid());
    });
    connect(previewBar_, &PreviewBar::simulationSpeedChanged, simulation_, &ld::core::Simulation::setSpeed);
    connect(rightPanel_->startButton(), &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(rightPanel_->pauseButton(), &QPushButton::clicked, this, &MainWindow::onPauseClicked);
    connect(simulation_, &ld::core::Simulation::advanced, this, &MainWindow::onSimulationAdvanced);
    connect(simulation_, &ld::core::Simulation::finished, this, &MainWindow::onSimulationFinished);
    connect(simulation_, &ld::core::Simulation::paused, this, [this] { updateRunButtons(); });
    connect(canvas_, &CanvasView::mouseMoved, this, [this](bool inside, const QPointF& mm) {
        const int decimals = AppSettings::instance().lengthUnit() == LengthUnit::Inch ? 3 : 1;
        mouseLabel_->setText(inside ? tr("Maus X %1  Y %2%3")
                                          .arg(QLocale().toString(mmToDisplay(mm.x()), 'f', decimals),
                                               QLocale().toString(mmToDisplay(mm.y()), 'f', decimals), lengthSuffix())
                                    : QString());
    });
    setupConnection();
    applyMachine(machine_);
    QTimer::singleShot(0, this, [this] { connection_->autoConnect(); });
    canvas_->setPathLayers(previewBar_->showBurn(), previewBar_->showTravel(), previewBar_->showRapid());
    // Anfasser am blauen Rahmen folgen dem Haken "Seitenverhältnis sperren".
    canvas_->setKeepAspect(rightPanel_->keepAspect());
    connect(rightPanel_, &RightPanel::keepAspectChanged, canvas_, &CanvasView::setKeepAspect);
    onJobChanged();

    // Design (System/Hell/Dunkel) aus den Einstellungen; Änderungen wirken sofort.
    connect(&AppSettings::instance(), &AppSettings::designChanged, this, &MainWindow::applyDesign);
    applyDesign();
    // Bei "System" folgt die App dem Wechsel des Systemdesigns.
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (AppSettings::instance().design() == DesignChoice::System) applyDesign();
    });

    // Gemerkte Fenstergröße und Lage der Leisten wiederherstellen (falls vorhanden).
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
}

void MainWindow::createCentralArea()
{
    canvas_ = new CanvasView(document_);
    connect(canvas_, &CanvasView::imageFileDropped, this, [this](const QString& path) { content_->loadFile(path); });
    // Mitte: Vorschau-Leiste (über Menü Ansicht ein-/ausblendbar) über der Arbeitsfläche.
    previewBar_ = new PreviewBar;
    auto* center = new QWidget;
    auto* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);
    centerLayout->addWidget(previewBar_);
    centerLayout->addWidget(canvas_, 1);
    setCentralWidget(center);
    previewBar_->setVisible(QSettings().value(QStringLiteral("view/previewBar"), true).toBool());
}

void MainWindow::createDocks()
{
    // Links: Maschine / Steuerung. Feste Leiste, nur schließbar (Ein-/Ausblenden über das
    // Menü "Ansicht"). Abtrennen und Verschieben kann später als Option dazukommen.
    leftDock_ = new QDockWidget(tr("Maschine"), this);
    leftDock_->setObjectName(QStringLiteral("leftDock"));  // nötig für saveState()
    leftDock_->setFeatures(QDockWidget::DockWidgetClosable);
    leftPanel_ = new LeftPanel(document_, MachineProfiles::defaultMaterialFolder());
    leftPanel_->setProfiles(profiles_->names(), profiles_->activeName());
    leftPanel_->setHasZAxis(machine_.hasZAxis);
    if (grbl_) {
        leftPanel_->setBaudRate(machine_.baudRate);
        refreshPortList();
    }
    connect(leftPanel_, &LeftPanel::profileSelected, this, &MainWindow::switchProfile);
    leftDock_->setWidget(leftPanel_);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock_);

    // Rechts: Auftrag (Datei, Gravur, Größe ...).
    rightDock_ = new QDockWidget(tr("Auftrag"), this);
    rightDock_->setObjectName(QStringLiteral("rightDock"));
    rightDock_->setFeatures(QDockWidget::DockWidgetClosable);
    rightPanel_ = new RightPanel(document_);
    connect(rightPanel_, &RightPanel::loadImageRequested, this, [this] { content_->openImageDialog(); });
    connect(rightPanel_, &RightPanel::editImageRequested, this, [this] { content_->editImage(); });
    connect(rightPanel_, &RightPanel::textRequested, this, [this] { content_->openTextDialog(); });
    connect(rightPanel_, &RightPanel::speedLimited, this, [this](double maxMmPerMin) {
        statusBar()->showMessage(tr("Die Speed wurde auf die Grenze des Maschinenprofils gesetzt: %1").arg(formatSpeed(maxMmPerMin)), 6000);
    });
    connect(rightPanel_, &RightPanel::clearRequested, this, &MainWindow::clearWorkArea);
    rightDock_->setWidget(rightPanel_);
    addDockWidget(Qt::RightDockWidgetArea, rightDock_);

    // Startbreite = Mindestbreite des Inhalts (die Panels setzen sie selbst) plus Dock-Rand.
    resizeDocks({leftDock_, rightDock_},
                {leftDock_->minimumSizeHint().width(), rightDock_->minimumSizeHint().width()},
                Qt::Horizontal);

    // Unten: Konsole für gesendete Zeilen und Antworten von GRBL (zunächst ausgeblendet).
    consoleDock_ = new QDockWidget(tr("Konsole"), this);
    consoleDock_->setObjectName(QStringLiteral("consoleDock"));
    consoleDock_->setFeatures(QDockWidget::DockWidgetClosable);
    console_ = new ConsoleWidget;
    consoleDock_->setWidget(console_);
    addDockWidget(Qt::BottomDockWidgetArea, consoleDock_);
    consoleDock_->hide();
}

void MainWindow::createMenus()
{
    // --- Datei ---
    QMenu* fileMenu = menuBar()->addMenu(tr("&Datei"));
    QAction* newAction = fileMenu->addAction(tr("&Neu"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, projectController_, [this] { projectController_->newProject(); });
    QAction* openProject = fileMenu->addAction(tr("&Projekt öffnen …"));
    openProject->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(openProject, &QAction::triggered, projectController_, [this] { projectController_->openProject(); });
    recentMenu_ = fileMenu->addMenu(tr("&Zuletzt geöffnet"));
    connect(recentMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    QAction* saveProject = fileMenu->addAction(tr("Projekt &speichern"));
    saveProject->setShortcut(QKeySequence::Save);
    connect(saveProject, &QAction::triggered, projectController_, [this] { projectController_->save(); });
    QAction* saveProjectAs = fileMenu->addAction(tr("Projekt speichern &unter …"));
    saveProjectAs->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    connect(saveProjectAs, &QAction::triggered, projectController_, [this] { projectController_->saveAs(); });
    fileMenu->addSeparator();
    QAction* openImage = fileMenu->addAction(tr("&Bild laden …"));
    openImage->setShortcut(QKeySequence::Open);
    connect(openImage, &QAction::triggered, this, [this] { content_->openImageDialog(); });
    QAction* saveGcodeAction = fileMenu->addAction(tr("&G-Code speichern …"));
    saveGcodeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
    connect(saveGcodeAction, &QAction::triggered, this, &MainWindow::saveGcode);
    connect(fileMenu->addAction(tr("&NC-Datei laden …")), &QAction::triggered, this, [this] { loadNcFile(); });
    closeNcAction_ = fileMenu->addAction(tr("NC-Datei &schließen"));
    closeNcAction_->setEnabled(false);
    connect(closeNcAction_, &QAction::triggered, this, &MainWindow::leaveNcMode);
    fileMenu->addSeparator();
    QAction* quit = fileMenu->addAction(tr("&Beenden"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    // --- Bearbeiten ---
    QMenu* editMenu = menuBar()->addMenu(tr("&Bearbeiten"));
    QAction* undo = document_->undoStack()->createUndoAction(this, tr("&Rückgängig"));
    undo->setShortcut(QKeySequence::Undo);
    editMenu->addAction(undo);
    QAction* redo = document_->undoStack()->createRedoAction(this, tr("&Wiederholen"));
    redo->setShortcut(QKeySequence::Redo);
    editMenu->addAction(redo);
    editMenu->addSeparator();
    QAction* editImage = editMenu->addAction(tr("&Bild bearbeiten …"));
    editImage->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(editImage, &QAction::triggered, this, [this] { content_->editImage(); });
    QAction* textAction = editMenu->addAction(tr("&Schrift …"));
    textAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    textAction->setToolTip(tr("Text eingeben, Schriftart wählen und als Bild einfügen (oder den vorhandenen Text bearbeiten)"));
    connect(textAction, &QAction::triggered, this, [this] { content_->openTextDialog(); });
    QAction* paste = editMenu->addAction(tr("Bild aus Zwischenablage &einfügen"));
    paste->setShortcut(QKeySequence::Paste);
    connect(paste, &QAction::triggered, this, [this] { content_->pasteImage(); });
    editMenu->addSeparator();
    QAction* settingsAction = editMenu->addAction(tr("&Einstellungen …"));
    settingsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    settingsAction->setMenuRole(QAction::PreferencesRole);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettings);

    // --- Ansicht: Leisten ein-/ausblenden ---
    QMenu* viewMenu = menuBar()->addMenu(tr("&Ansicht"));
    QAction* leftAction = leftDock_->toggleViewAction();
    leftAction->setText(tr("Seitenleiste links"));
    viewMenu->addAction(leftAction);
    QAction* rightAction = rightDock_->toggleViewAction();
    rightAction->setText(tr("Panel rechts"));
    viewMenu->addAction(rightAction);
    QAction* previewBarAction = viewMenu->addAction(tr("Vorschau-Leiste"));
    previewBarAction->setCheckable(true);
    previewBarAction->setChecked(previewBar_->isVisibleTo(this));
    connect(previewBarAction, &QAction::toggled, this, [this](bool on) {
        previewBar_->setVisible(on);
        QSettings().setValue(QStringLiteral("view/previewBar"), on);
    });
    QAction* consoleAction = consoleDock_->toggleViewAction();
    consoleAction->setText(tr("Konsole"));
    viewMenu->addAction(consoleAction);
    viewMenu->addSeparator();
    QAction* gridAction = viewMenu->addAction(tr("&Raster anzeigen"));
    gridAction->setCheckable(true);
    gridAction->setChecked(AppSettings::instance().colors().gridVisible);
    connect(gridAction, &QAction::toggled, this, [](bool on) { AppSettings::instance().setGridVisible(on); });
    connect(&AppSettings::instance(), &AppSettings::colorsChanged, gridAction,
            [gridAction] { gridAction->setChecked(AppSettings::instance().colors().gridVisible); });
    viewMenu->addSeparator();
    QAction* zoomInAction = viewMenu->addAction(tr("Zoom &+"));
    zoomInAction->setShortcuts({QKeySequence(QKeySequence::ZoomIn), QKeySequence(Qt::CTRL | Qt::Key_Equal)});   // Strg++ und Strg+=
    connect(zoomInAction, &QAction::triggered, canvas_, &CanvasView::zoomIn);
    QAction* zoomOutAction = viewMenu->addAction(tr("Zoom &-"));
    zoomOutAction->setShortcut(QKeySequence(QKeySequence::ZoomOut));   // Strg+-
    connect(zoomOutAction, &QAction::triggered, canvas_, &CanvasView::zoomOut);
    QAction* fitAction = viewMenu->addAction(tr("Bild &einpassen"));
    fitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(fitAction, &QAction::triggered, canvas_, &CanvasView::fitToImage);

    // --- Maschine (Funktionen folgen in späteren Meilensteinen) ---
    QMenu* machineMenu = menuBar()->addMenu(tr("&Maschine"));
    connect(machineMenu->addAction(tr("Maschinen&profile …")), &QAction::triggered, this,
            &MainWindow::openMachineProfiles);
    grblParamsAction_ = machineMenu->addAction(tr("GRBL-Parameter …"));
    grblParamsAction_->setEnabled(false);   // erst mit Verbindung
    connect(grblParamsAction_, &QAction::triggered, this, &MainWindow::openGrblSettings);
    firmwareAction_ = machineMenu->addAction(tr("&Firmware …"));
    firmwareAction_->setEnabled(false);   // erst mit Verbindung
    connect(firmwareAction_, &QAction::triggered, this, [this] {
        if (!grbl_->isConnected()) return;
        // Für das Firmware-Update: Port, Systemprotokoll und Neuverbinden (der Laser startet nach dem Update neu)
        FirmwareUpdateContext context;
        context.portKey = QSettings().value(QStringLiteral("connection/lastPort/") + machine_.name).toString();
        context.log = [this](ld::core::LogLevel level, const QString& message, const QString& details) {
            protocol_->log(level, tr("Firmware"), message, details);
        };
        context.reconnect = [this, key = context.portKey] { connection_->connectTo(key, machine_.baudRate); };
        FirmwareDialog dialog(grbl_, context, this);
        dialog.exec();
    });
    unlockAction_ = machineMenu->addAction(tr("Alarm aufheben ($X) …"));
    unlockAction_->setEnabled(false);       // nur bei einem Alarm
    connect(unlockAction_, &QAction::triggered, this, [this] { connection_->confirmUnlock(); });
    connect(machineMenu->addAction(tr("&Materialtest …")), &QAction::triggered, this, &MainWindow::openMaterialTest);
    connect(machineMenu->addAction(tr("&Kalibrierung …")), &QAction::triggered, this, [this] { tools_->openCalibration(); });
    connect(machineMenu->addAction(tr("&Protokoll …")), &QAction::triggered, this, &MainWindow::openProtocol);

    // --- Hilfe ---
    QMenu* helpMenu = menuBar()->addMenu(tr("&Hilfe"));
    QAction* manualAction = helpMenu->addAction(tr("&Handbuch"));
    manualAction->setShortcut(QKeySequence(QKeySequence::HelpContents));   // F1
    connect(manualAction, &QAction::triggered, this, [this] { HelpWindow::open(this); });
    helpMenu->addSeparator();
    connect(helpMenu->addAction(tr("Über Laser-Dilettant …")), &QAction::triggered, this,
            &MainWindow::showAbout);
    connect(helpMenu->addAction(tr("Über &Qt …")), &QAction::triggered, qApp,
            &QApplication::aboutQt);
}

void MainWindow::createCornerWidget()
{
    // Rechts neben dem Menü: Hell/Dunkel-Umschalter und der rote NOT-AUS-Button.
    // Der Not-Aus ist immer sichtbar, auch wenn die Seitenleisten ausgeblendet sind.
    auto* corner = new QWidget;
    auto* layout = new QHBoxLayout(corner);
    layout->setContentsMargins(0, 2, 6, 2);
    layout->setSpacing(8);

    themeButton_ = new QPushButton;
    themeButton_->setIconSize(QSize(20, 20));
    connect(themeButton_, &QPushButton::clicked, this, &MainWindow::toggleColorScheme);
    layout->addWidget(themeButton_);

    emergencyButton_ = new QPushButton(tr("NOT-AUS"));
    emergencyButton_->setToolTip(tr("Software-Not-Aus (Esc)"));
    emergencyButton_->setShortcut(Qt::Key_Escape);
    emergencyButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #d42a29; color: white; font-weight: 700;"
        " border: 2px solid #8f1514; border-radius: 3px; padding: 4px 16px; }"
        "QPushButton:pressed { background: #8f1514; }"));
    connect(emergencyButton_, &QPushButton::clicked, this, &MainWindow::onEmergencyStop);
    layout->addWidget(emergencyButton_);

    menuBar()->setCornerWidget(corner, Qt::TopRightCorner);
}

void MainWindow::createStatusBar()
{
    // Links: Verbindungsanzeige (Punkt + Text).
    connLed_ = new QLabel(QStringLiteral("●"));
    connLed_->setStyleSheet(QStringLiteral("color: #8a8a8e;"));  // grau = nicht verbunden
    statusBar()->addWidget(connLed_);
    connText_ = new QLabel(tr("Nicht verbunden"));
    statusBar()->addWidget(connText_);

    // Rote Warnung, solange das Schwachlicht an ist (M8).
    laserOnLabel_ = new QLabel;
    laserOnLabel_->setStyleSheet(QStringLiteral("color: white; background: #d42a29; font-weight: 700; padding: 1px 8px; margin-left: 12px;"));
    laserOnLabel_->hide();
    statusBar()->addWidget(laserOnLabel_);

    // Warnung, solange der Auftrag größer als der Arbeitsbereich ist (siehe planung.md 3.8).
    sizeWarning_ = new QLabel;
    sizeWarning_->setStyleSheet(QStringLiteral("color: #d42a29; font-weight: 600; margin-left: 16px;"));
    statusBar()->addWidget(sizeWarning_);

    // Rechts: GRBL-Zustand, Position, Dauer, Fortschritt, Konsole-Knopf.
    mouseLabel_ = new QLabel;
    mouseLabel_->setMinimumWidth(130);
    mouseLabel_->setToolTip(tr("Mausposition in mm ab dem Job-Ursprung (Y nach oben, vom Bediener weg)"));
    statusBar()->addPermanentWidget(mouseLabel_);
    grblLabel_ = new QLabel(tr("GRBL: –"));
    statusBar()->addPermanentWidget(grblLabel_);
    posLabel_ = new QLabel(tr("X – · Y –"));
    posLabel_->setToolTip(tr("Position des Lasers ab dem Nullpunkt (Arbeitskoordinaten)"));
    statusBar()->addPermanentWidget(posLabel_);
    durationLabel_ = new QLabel(tr("Dauer –"));
    statusBar()->addPermanentWidget(durationLabel_);

    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setFixedWidth(160);
    statusBar()->addPermanentWidget(progressBar_);

    auto* consoleButton = new QPushButton(tr("Konsole"));
    connect(consoleButton, &QPushButton::clicked, consoleDock_->toggleViewAction(),
            &QAction::trigger);
    statusBar()->addPermanentWidget(consoleButton);
}

void MainWindow::updateSizeWarning()
{
    if (!document_->hasImage()) {
        sizeWarning_->hide();
        return;
    }
    const QSizeF size = ld::core::boundingSize(document_->transform());
    if (ld::core::fitsWorkArea(size, machine_)) {
        sizeWarning_->hide();
        return;
    }
    const QSizeF area = ld::core::workAreaInOperatorView(machine_);
    sizeWarning_->setText(tr("⚠ Auftrag %1 × %2 ist größer als der Arbeitsbereich (%3 × %4)")
                              .arg(formatLength(size.width()), formatLength(size.height()),
                                   formatLength(area.width(), 0), formatLength(area.height(), 0)));
    sizeWarning_->show();
}

// ---------------------------------------------------------------------------------------------
// Vorschau und Simulation (M5, siehe planung.md 3.11)
// ---------------------------------------------------------------------------------------------
bool MainWindow::checkWorkArea()
{
    QSizeF size = ld::core::boundingSize(document_->transform());
    if (ncMode_) {
        const auto data = pathPreview_->data();
        size = data ? data->bounds.size() : QSizeF();
    }
    if (ld::core::fitsWorkArea(size, machine_)) {
        return true;
    }
    const QSizeF area = ld::core::workAreaInOperatorView(machine_);
    QMessageBox::warning(
        this, tr("Simulation"),
        tr("Der Auftrag (%1 × %2) ist größer als der Arbeitsbereich (%3 × %4). Bitte verkleinern.")
            .arg(formatLength(size.width()), formatLength(size.height()), formatLength(area.width(), 0),
                 formatLength(area.height(), 0)));
    return false;
}

// Jede Änderung am Auftrag: laufende Simulation abbrechen, grünen Fortschritt löschen und die
// temporäre Datei samt Wegen verwerfen. Neu erzeugt wird erst bei Bedarf.
void MainWindow::onJobChanged()
{
    if (ncMode_) return;   // im NC-Auftrag bestimmt die geladene Datei die Wege, nicht das Bild
    if (simulation_->state() != ld::core::Simulation::State::Stopped) {
        stopSimulation(tr("Simulation abgebrochen: Der Auftrag wurde geändert."));
    }
    canvas_->clearProgress();
    simStartPending_ = false;
    if (jobs_) jobs_->cancelPendingStart();
    progressBar_->setValue(0);

    if (document_->hasImage() && !document_->raster().isNull() && !document_->isRasterBusy()) {
        pathPreview_->setJob(document_->makeGcodeJob(machine_));
        pathPreview_->request();   // die Vorschau zeigt immer auch die Laserwege
    } else {
        pathPreview_->clearJob();
    }
    updateRunButtons();
}

void MainWindow::onPreviewStateChanged()
{
    using State = ld::core::PathPreview::State;
    switch (pathPreview_->state()) {
    case State::Ready:
        canvas_->setPaths(pathPreview_->data());
        canvas_->setPathMessage(QString());
        if (ncMode_ && ncFitPending_ && pathPreview_->data()) {
            ncFitPending_ = false;
            canvas_->setNcMode(true, pathPreview_->data()->bounds);   // auf die Wege der Datei einpassen
        }
        if (jobs_ && jobs_->onPreviewReady()) {
            // der wartende Start am Laser läuft jetzt
        } else if (simStartPending_) {
            simStartPending_ = false;
            beginSimulation();
        }
        break;
    case State::Generating:
    case State::Loading:
        // Die alten Wege bleiben stehen, bis die neuen fertig sind (kein Flackern beim Ziehen der Regler).
        // Gibt es noch keine, steht ein Hinweis auf der Arbeitsfläche.
        if (!canvas_->hasPaths()) canvas_->setPathMessage(tr("Laserwege werden erzeugt …"));
        break;
    case State::Failed:
        canvas_->setPaths(nullptr);
        canvas_->setPathMessage(tr("Laserwege nicht verfügbar: %1").arg(pathPreview_->error()));
        if (jobs_ && jobs_->onPreviewFailed(pathPreview_->error())) {
            // der wartende Start am Laser ist verfallen (Meldung steht)
        } else if (simStartPending_) {
            simStartPending_ = false;
            QMessageBox::warning(this, tr("Simulation"), pathPreview_->error());
        }
        break;
    case State::Stale:
        break;   // die alten Wege bleiben stehen, die neuen kommen gleich
    case State::Idle:
        canvas_->setPaths(nullptr);
        canvas_->setPathMessage(QString());
        break;
    }
    updateDurationLabel();
    updateRunButtons();
}

static QString lengthText(double mm)
{
    return mm >= 1000.0 ? QObject::tr("%L1 m").arg(mm / 1000.0, 0, 'f', 1) : QObject::tr("%L1 mm").arg(mm, 0, 'f', 0);
}

// Dialog zum Knopf "Info" der Vorschau-Leiste: die Werte stehen als Tabelle untereinander.
void MainWindow::showPreviewInfo()
{
    using State = ld::core::PathPreview::State;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Vorschau-Info"));
    auto* layout = new QVBoxLayout(&dialog);
    QString message;
    switch (pathPreview_->state()) {
    case State::Idle:
        message = document_->hasImage() ? tr("Das Bild wird noch berechnet.") : tr("Noch kein Bild geladen.");
        break;
    case State::Stale:
        message = tr("Die Wege werden gleich aus dem G-Code erzeugt.");
        break;
    case State::Generating:
        message = tr("G-Code wird erzeugt …");
        break;
    case State::Loading:
        message = tr("Wege werden gelesen …");
        break;
    case State::Failed:
        message = pathPreview_->error();
        break;
    case State::Ready:
        break;
    }
    if (!message.isEmpty()) {
        layout->addWidget(new QLabel(message));
    } else {
        const auto data = pathPreview_->data();
        const double speed = std::max(1.0, previewBar_->simulationSpeed());
        const QList<QPair<QString, QString>> rows = {
            {tr("G-Code-Zeilen"), QStringLiteral("%1 (%2)").arg(QLocale().toString(static_cast<qlonglong>(pathPreview_->stats().lines)),
                                                              QLocale().formattedDataSize(QFileInfo(pathPreview_->filePath()).size(), 1, QLocale::DataSizeTraditionalFormat))},
            {tr("Gravur (Länge)"), lengthText(data->burnLengthMm)},
            {tr("Leerfahrt (Länge)"), lengthText(data->travelLengthMm)},
            {tr("Eilgang (Länge)"), lengthText(data->rapidLengthMm)},
            {tr("Dauer des Brennens"), ld::core::formatDuration(data->totalSeconds)},
            {tr("Dauer der Simulation (%1×)").arg(speed, 0, 'f', 0), ld::core::formatDuration(data->totalSeconds / speed)},
        };
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(24);
        int row = 0;
        for (const auto& entry : rows) {
            auto* value = new QLabel(entry.second);
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            grid->addWidget(new QLabel(entry.first), row, 0);
            grid->addWidget(value, row, 1);
            ++row;
        }
        layout->addLayout(grid);
        if (data->coarse) {
            auto* warning = new QLabel(tr("⚠ Sehr großer Auftrag: Es wird nur jede %1. Zeile gezeigt.").arg(data->lineStride));
            warning->setWordWrap(true);
            layout->addWidget(warning);
        }
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::updateRunButtons()
{
    using ld::grbl::MachineState;
    const bool simRunning = simulation_->state() != ld::core::Simulation::State::Stopped;
    const bool ready = jobAvailable();
    const bool connected = grbl_ && grbl_->isConnected();
    if (tools_) {
        tools_->setRunState(connected, connected && !grbl_->isJobActive() && grbl_->status().state == MachineState::Idle);
    }
    // Simulation: möglich, solange kein echter Auftrag läuft; zum Stoppen immer bedienbar.
    previewBar_->setSimulationState(simRunning || (ready && !(connected && grbl_->isJobActive())), simRunning,
                                    simulation_->state() == ld::core::Simulation::State::Paused);
    if (connected) {
        // Am Laser: Start/Abbrechen, Pause/Weiter und Rahmen.
        const bool jobActive = grbl_->isJobActive();
        const MachineState state = grbl_->status().state;
        const bool idle = state == MachineState::Idle && !jobActive;
        rightPanel_->startButton()->setEnabled(jobActive ? (!jobs_->isFrameLike() || jobs_->isMaterialTest()) : (ready && idle));   // der Materialtest lässt sich abbrechen
        rightPanel_->startButton()->setText(jobActive ? tr("Abbrechen") : tr("Start"));
        rightPanel_->pauseButton()->setEnabled(jobActive || state == MachineState::Hold);
        rightPanel_->pauseButton()->setText(state == MachineState::Hold ? tr("Weiter") : tr("Pause"));
        rightPanel_->testSizeButton()->setEnabled(document_->hasImage() && idle);
        return;
    }
    // Ohne Verbindung gibt es am Laser nichts zu starten; die Simulation läuft über die Vorschau-Leiste.
    rightPanel_->testSizeButton()->setEnabled(false);
    rightPanel_->startButton()->setEnabled(false);
    rightPanel_->startButton()->setText(tr("Start"));
    rightPanel_->pauseButton()->setEnabled(false);
    rightPanel_->pauseButton()->setText(tr("Pause"));
}

void MainWindow::onStartClicked()
{
    if (!grbl_->isConnected()) return;
    if (grbl_->isJobActive()) {
        jobs_->confirmCancel();
        return;
    }
    if (simulation_->state() != ld::core::Simulation::State::Stopped) stopSimulation(tr("Simulation abgebrochen."));
    startLaserJob();
}

// Knopf "Simulation" in der Vorschau-Leiste: Simulation starten oder stoppen.
void MainWindow::onSimulationClicked()
{
    using State = ld::core::PathPreview::State;
    if (simulation_->state() != ld::core::Simulation::State::Stopped) {
        stopSimulation(tr("Simulation abgebrochen."));
        return;
    }
    if ((!ncMode_ && !document_->hasImage()) || !checkWorkArea()) {
        return;
    }
    switch (pathPreview_->state()) {
    case State::Ready:
        beginSimulation();
        break;
    case State::Stale:
        simStartPending_ = true;
        pathPreview_->request();
        onPreviewStateChanged();
        break;
    case State::Generating:
    case State::Loading:
        simStartPending_ = true;
        break;
    case State::Failed:
        QMessageBox::warning(this, tr("Simulation"), pathPreview_->error());
        break;
    case State::Idle:
        statusBar()->showMessage(tr("Das Bild wird noch berechnet. Bitte einen Moment warten."), 4000);
        break;
    }
}

void MainWindow::onPauseClicked()
{
    if (grbl_->isConnected()) {
        // Feed Hold (!) bzw. Fortsetzen (~); GRBL schaltet den Laser bei der Pause aus.
        if (grbl_->status().state == ld::grbl::MachineState::Hold) {
            grbl_->resume();
        } else if (grbl_->isJobActive()) {
            grbl_->feedHold();
        }
        return;
    }
}

void MainWindow::onSimulationPauseClicked()
{
    if (simulation_->state() == ld::core::Simulation::State::Running) {
        simulation_->pause();
    } else if (simulation_->state() == ld::core::Simulation::State::Paused) {
        simulation_->resume();
    }
}

void MainWindow::beginSimulation()
{
    const auto data = pathPreview_->data();
    if (!data || data->segments.empty()) {
        statusBar()->showMessage(tr("Es gibt nichts zu simulieren: Der Auftrag enthält keine Gravur."), 5000);
        return;
    }
    canvas_->clearProgress();
    canvas_->beginProgress();
    statusBar()->showMessage(grbl_ && grbl_->isConnected() ? tr("Simulation läuft") : tr("Nicht verbunden – Simulation läuft"), 4000);
    simulation_->start(data, previewBar_->simulationSpeed());
    updateGrblLabel();
    updateRunButtons();
}

void MainWindow::stopSimulation(const QString& reason)
{
    simulation_->stop();
    canvas_->clearProgress();   // nach der Simulation wieder die Standard-Ansicht (ohne Grün)
    updateGrblLabel();
    statusBar()->showMessage(reason, 6000);
    updateRunButtons();
    updateDurationLabel();   // Dauer-Anzeige wieder auf die Schätzung stellen
}

void MainWindow::onSimulationAdvanced(size_t first, size_t last, const QPointF& head, double elapsedSeconds)
{
    const auto data = pathPreview_->data();
    if (!data) {
        return;
    }
    canvas_->advanceProgress(*data, first, last, head);
    const double total = std::max(simulation_->totalSeconds(), 1e-9);
    // Der Fortschrittsbalken der Statusleiste läuft bei der Simulation nicht mit; den Fortschritt zeigt der Simulations-Knopf.
    previewBar_->setSimulationProgress(elapsedSeconds / total);
    durationLabel_->setText(tr("Simulation %1 von %2")
                                .arg(ld::core::formatDuration(elapsedSeconds), ld::core::formatDuration(total)));
}

void MainWindow::onSimulationFinished()
{
    canvas_->clearProgress();   // wieder die Standard-Ansicht
    updateGrblLabel();
    statusBar()->showMessage(tr("Simulation beendet"), 6000);
    updateRunButtons();
    updateDurationLabel();
}

// ---------------------------------------------------------------------------------------------
// G-Code: Zeitschätzung, Einstellungen, Speichern
// ---------------------------------------------------------------------------------------------
// Dauer-Anzeige in der Statusleiste: Richtwert aus dem G-Code der Vorschau (der G-Code wird nur einmal erzeugt).
// Während einer Simulation oder eines Auftrags zeigen andere Stellen die Zeit.
void MainWindow::updateDurationLabel()
{
    using State = ld::core::PathPreview::State;
    if (simulation_->state() != ld::core::Simulation::State::Stopped) return;
    if (grbl_ && grbl_->isJobActive() && !(jobs_ && jobs_->isFrameLike())) return;
    durationLabel_->setToolTip(QString());
    switch (pathPreview_->state()) {
    case State::Ready:
        durationLabel_->setText(tr("Dauer ≈ %1").arg(ld::core::formatDuration(pathPreview_->stats().seconds)));
        durationLabel_->setToolTip(tr("Richtwert aus Strecke und Vorschub, ohne Beschleunigung: in Wirklichkeit etwas länger."));
        break;
    case State::Stale:
    case State::Generating:
    case State::Loading:
        durationLabel_->setText(tr("Dauer …"));
        break;
    case State::Failed:
        durationLabel_->setText(tr("Dauer –"));
        durationLabel_->setToolTip(pathPreview_->error());
        break;
    case State::Idle:
        durationLabel_->setText(tr("Dauer –"));
        break;
    }
}

void MainWindow::openMachineProfiles()
{
    MachineProfilesDialog dialog(profiles_, this);
    if (dialog.exec() == QDialog::Accepted) {
        profiles_->setActive(dialog.activeNameAfter());
        leftPanel_->setProfiles(profiles_->names(), profiles_->activeName());
        applyMachine(profiles_->active());
    }
}

void MainWindow::openSettings()
{
    // Der Dialog zeigt den Job-Ursprung des geladenen Bildes (sonst den Standard).
    const ld::core::JobOrigin shown =
        document_->hasImage() ? document_->jobOrigin() : AppSettings::instance().defaultJobOrigin();
    SettingsDialog dialog(shown, this);
    dialog.setNotifier(notifier_);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (accepted) remote_->applySettings();   // Fernansicht ein-, ausschalten oder auf einen anderen Port
    if (accepted && dialog.languageChanged()) {
        QMessageBox::information(this, tr("Einstellungen"), tr("Die neue Sprache gilt nach einem Neustart von Laser-Dilettant."));
    }
    if (accepted && dialog.jobOriginChanged()) {
        // Gilt für das aktuelle Bild und als Standard für neue (planung.md 3.12).
        AppSettings::instance().setDefaultJobOrigin(dialog.jobOrigin());
        document_->setJobOrigin(dialog.jobOrigin());
        document_->setOffsetMm(QPointF());   // die Grafik springt wieder mit ihrem Job-Ursprung auf den Nullpunkt
    }
}

void MainWindow::switchProfile(const QString& name)
{
    if (const auto setup = profiles_->load(name)) {
        profiles_->setActive(name);
        applyMachine(*setup);
        if (protocol_) protocol_->log(ld::core::LogLevel::Info, tr("Profil"), tr("Maschinenprofil gewählt: %1").arg(name));
    }
}

// Ein anderes (oder geändertes) Maschinenprofil gilt: G-Code, Prüfung des Arbeitsbereichs, Anzeige.
void MainWindow::applyMachine(const ld::core::MachineSetup& machine)
{
    machine_ = machine;
    if (tools_) tools_->onMachineChanged();
    if (jog_) jog_->setMachine(machine_);
    if (weak_) weak_->setMaxPowerS(machine_.maxPower);
    canvas_->viewport()->update();   // die Anzeige hängt vom Profil ab (Wege, Markierungen)
    rightPanel_->setMachine(machine_);
    rightPanel_->setDefaultPixelSizeMm(ld::core::defaultPixelSizeMm(machine_));
    leftPanel_->setHasZAxis(machine_.hasZAxis);
    updateSizeWarning();
    onJobChanged();
    if (ncMode_ && !ncSourcePath_.isEmpty()) loadNcFile(ncSourcePath_, true);   // die Wege hängen vom Maschinenprofil ab
}

void MainWindow::saveGcode()
{
    if (ncMode_) {
        QMessageBox::information(this, tr("G-Code speichern"),
                                 tr("Der NC-Auftrag ist eine geladene Datei, sie liegt schon vor:\n%1").arg(ncSourcePath_));
        return;
    }
    if (!document_->hasImage()) {
        QMessageBox::information(this, tr("G-Code speichern"), tr("Es ist noch kein Bild geladen."));
        return;
    }
    // Zu große Aufträge werden nicht erzeugt (planung.md 3.10): Ohne Endschalter würde der Laser
    // anstoßen und der Nullpunkt verrutschen.
    const QSizeF size = ld::core::boundingSize(document_->transform());
    if (!ld::core::fitsWorkArea(size, machine_)) {
        const QSizeF area = ld::core::workAreaInOperatorView(machine_);
        QMessageBox::warning(
            this, tr("G-Code speichern"),
            tr("Der Auftrag (%1 × %2) ist größer als der Arbeitsbereich (%3 × %4). "
               "Bitte verkleinern, dann lässt sich der G-Code speichern.")
                .arg(formatLength(size.width()), formatLength(size.height()), formatLength(area.width(), 0),
                     formatLength(area.height(), 0)));
        return;
    }
    if (document_->raster().isNull() || document_->isRasterBusy()) {
        QMessageBox::information(this, tr("G-Code speichern"),
                                 tr("Das Bild wird noch berechnet. Bitte einen Moment warten und es dann erneut versuchen."));
        return;
    }

    const QString baseName = document_->sourcePath().isEmpty()
                                 ? tr("gravur")
                                 : QFileInfo(document_->sourcePath()).completeBaseName();
    const QString startDir = AppSettings::instance().startFolder(FolderKind::Gcode);
    QString path = QFileDialog::getSaveFileName(
        this, tr("G-Code speichern"),
        (startDir.isEmpty() ? QString() : startDir + QLatin1Char('/')) + baseName + QStringLiteral(".nc"),
        tr("G-Code (*.nc *.gcode);;Alle Dateien (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".nc");
    }
    AppSettings::instance().setLastFolder(FolderKind::Gcode, path);

    // Liegt die temporäre Datei der Vorschau schon vor, wird sie nur kopiert (planung.md 3.11).
    if (pathPreview_->state() == ld::core::PathPreview::State::Ready) {
        if (QFile::exists(path)) {
            QFile::remove(path);
        }
        if (QFile::copy(pathPreview_->filePath(), path)) {
            const ld::core::GcodeStats& stats = pathPreview_->stats();
            const QString message = tr("G-Code gespeichert: %1 (%2 KB, %3 Zeilen, Dauer ≈ %4)")
                                        .arg(QFileInfo(path).fileName())
                                        .arg((stats.bytes + 512) / 1024)
                                        .arg(stats.lines)
                                        .arg(ld::core::formatDuration(stats.seconds));
            statusBar()->showMessage(message, 10000);
            console_->appendInfo(message);
            return;
        }
    }

    // Fortschrittsanzeige mit Abbrechen; der Dialog erscheint nur, wenn es länger dauert.
    delete saveProgress_;
    saveProgress_ = new QProgressDialog(tr("G-Code wird erzeugt …"), tr("Abbrechen"), 0, 100, this);
    saveProgress_->setWindowTitle(tr("G-Code speichern"));
    saveProgress_->setWindowModality(Qt::WindowModal);
    saveProgress_->setMinimumDuration(400);
    saveProgress_->setAutoClose(false);
    saveProgress_->setAutoReset(false);
    connect(saveProgress_, &QProgressDialog::canceled, saver_, &ld::core::GcodeRunner::cancel);
    connect(saver_, &ld::core::GcodeRunner::progress, saveProgress_,
            [this](double fraction) { if (saveProgress_) saveProgress_->setValue(static_cast<int>(fraction * 100.0)); });

    saver_->request(document_->makeGcodeJob(machine_), path);
}

void MainWindow::onSaveFinished(const ld::core::GcodeResult& result, const QString& path)
{
    if (saveProgress_) {
        saveProgress_->hide();
        saveProgress_->deleteLater();
        saveProgress_ = nullptr;
    }
    if (!result.ok) {
        QMessageBox::warning(this, tr("G-Code speichern"), result.error.isEmpty() ? tr("Unbekannter Fehler.") : result.error);
        return;
    }
    const QString message = tr("G-Code gespeichert: %1 (%2 KB, %3 Zeilen, Dauer ≈ %4)")
                                .arg(QFileInfo(path).fileName())
                                .arg((result.stats.bytes + 512) / 1024)
                                .arg(result.stats.lines)
                                .arg(ld::core::formatDuration(result.stats.seconds));
    statusBar()->showMessage(message, 10000);
    console_->appendInfo(message);
}

void MainWindow::updateWindowTitle()
{
    // Projektname, sonst Bildname; "*" bei ungespeicherten Änderungen (planung.md 3.13).
    QString name;
    if (ncMode_) {
        setWindowTitle(tr("%1 (NC) – Laser-Dilettant").arg(QFileInfo(ncSourcePath_).fileName()));
        return;
    }
    if (projectController_ && projectController_->hasPath()) {
        name = QFileInfo(projectController_->path()).fileName();
    } else if (document_->hasImage()) {
        name = document_->calibrationSpec() ? tr("Kalibrierung") : document_->textSpec() ? tr("Text") : document_->sourcePath().isEmpty() ? tr("Unbenannt") : QFileInfo(document_->sourcePath()).fileName();
    }
    if (name.isEmpty()) {
        setWindowTitle(QStringLiteral("Laser-Dilettant"));
        return;
    }
    setWindowTitle(QStringLiteral("%1%2 – Laser-Dilettant").arg(name, document_->hasUnsavedChanges() ? QStringLiteral("*") : QString()));
}

void MainWindow::rebuildRecentMenu()
{
    recentMenu_->clear();
    QStringList recent = AppSettings::instance().recentProjects();
    int shown = 0;
    for (const QString& path : recent) {
        if (!QFileInfo::exists(path)) {
            AppSettings::instance().removeRecentProject(path);   // gibt es nicht mehr
            continue;
        }
        QAction* action = recentMenu_->addAction(QStringLiteral("&%1 %2").arg(++shown).arg(QFileInfo(path).fileName()));
        action->setToolTip(path);
        connect(action, &QAction::triggered, projectController_, [this, path] { projectController_->openProject(path); });
    }
    if (shown == 0) {
        recentMenu_->addAction(tr("(leer)"))->setEnabled(false);
        return;
    }
    recentMenu_->addSeparator();
    connect(recentMenu_->addAction(tr("Liste leeren")), &QAction::triggered, this,
            [] { AppSettings::instance().clearRecentProjects(); });
}

void MainWindow::handleStartup(const QStringList& arguments)
{
    // Erst im Ereignisloop, damit das Fenster schon steht, wenn Fragen erscheinen.
    QTimer::singleShot(0, this, [this, arguments] {
        if (projectController_->recoveryAvailable()) {
            protocol_->log(ld::core::LogLevel::Warning, tr("Programm"), tr("Letzte Sitzung wurde nicht normal beendet"),
                           tr("Sicherung zum Wiederherstellen gefunden"));
            QMessageBox box(QMessageBox::Question, tr("Wiederherstellen"),
                            tr("Die letzte Sitzung wurde nicht normal beendet.\nUngespeicherte Änderungen wiederherstellen?"),
                            QMessageBox::NoButton, this);
            QPushButton* restore = box.addButton(tr("Wiederherstellen"), QMessageBox::AcceptRole);
            box.addButton(tr("Verwerfen"), QMessageBox::DestructiveRole);
            box.setDefaultButton(restore);
            box.exec();
            if (box.clickedButton() == restore) {
                projectController_->restoreRecovery();
            } else {
                projectController_->discardRecovery();
            }
        }
        // Die zuletzt benutzten Gravur-Einstellungen sind der Ausgangspunkt; ein Projekt oder die letzte Sitzung überschreibt sie.
        if (!document_->hasImage()) {
            if (const auto op = AppSettings::instance().lastOperation()) {
                document_->setOperation(*op);
                document_->markSaved();   // das ist keine Änderung des Bedieners
            }
        }
        if (const auto frame = AppSettings::instance().lastFrame()) rightPanel_->setFrameValues(frame->first, frame->second);
        bool opened = document_->hasImage();   // eine Wiederherstellung hat schon ein Bild geladen
        for (const QString& argument : arguments) {
            if (argument.endsWith(QStringLiteral(".ldproj"), Qt::CaseInsensitive) && QFileInfo::exists(argument)) {
                opened = projectController_->openProject(QFileInfo(argument).absoluteFilePath()) || opened;
                break;
            }
        }
        // Eine NC-Datei im Aufruf wird als NC-Auftrag geladen.
        for (const QString& argument : arguments) {
            const QString suffix = QFileInfo(argument).suffix().toLower();
            if ((suffix == QLatin1String("nc") || suffix == QLatin1String("gcode") || suffix == QLatin1String("ngc")) &&
                QFileInfo::exists(argument)) {
                loadNcFile(QFileInfo(argument).absoluteFilePath());
                opened = true;
                break;
            }
        }
        // Sonst: die letzte Sitzung (Bild mit Einstellungen). Ist das Bild nicht mehr da, bleibt die Seite leer.
        if (!opened) {
            projectController_->restoreSession();
        }
    });
}

// Design aus den Einstellungen anwenden (ab Qt 6.8 kann die Anwendung das Farbschema selbst festlegen).
void MainWindow::applyDesign()
{
    darkActive_ = applyTheme(AppSettings::instance().design());
    updateThemeButtonText();
}

void MainWindow::updateThemeButtonText()
{
    // Der Knopf zeigt, wohin umgeschaltet wird: im dunklen Design die Sonne, im hellen der Mond.
    themeButton_->setIcon(themeIcon(darkActive_, QApplication::palette().color(QPalette::ButtonText)));
    themeButton_->setToolTip(darkActive_ ? tr("Zum hellen Design wechseln") : tr("Zum dunklen Design wechseln"));
}

void MainWindow::toggleColorScheme()
{
    AppSettings::instance().setDesign(darkActive_ ? DesignChoice::Light : DesignChoice::Dark);
}

void MainWindow::onEmergencyStop()
{
    // Soft-Reset an GRBL und danach M5 (Laser aus), Auftrag und Warteschlange verwerfen.
    if (simulation_->state() != ld::core::Simulation::State::Stopped) {
        stopSimulation(tr("Simulation durch NOT-AUS gestoppt."));
    }
    QString text;
    const bool jobWasRunning = jobs_->kind() != JobController::Kind::None;   // vor dem Reset: danach ist der Auftrag schon beendet
    if (grbl_->state() != ld::grbl::GrblConnection::State::Disconnected) {
        jobs_->noteEmergencyStop();   // ein dadurch beendeter Auftrag erscheint im Protokoll als NOT-AUS
        grbl_->emergencyStop();
        text = tr("NOT-AUS: Reset gesendet, Laser aus. Die Position kann ungenau sein; bitte Nullpunkt prüfen.");
    } else {
        text = tr("NOT-AUS gedrückt (keine Verbindung zum Laser)");
    }
    statusBar()->showMessage(text, 8000);
    console_->appendError(text);
    protocol_->log(ld::core::LogLevel::Warning, tr("Sicherheit"), tr("NOT-AUS ausgelöst"),
                   grbl_->isConnected() ? (jobWasRunning ? tr("während eines Auftrags") : tr("kein Auftrag aktiv"))
                                        : tr("keine Verbindung zum Laser"));
}

// ---------------------------------------------------------------------------------------------
// Verbindung zum Laser (M4 Teil 1, siehe planung.md 3.15)
// ---------------------------------------------------------------------------------------------
void MainWindow::setupConnection()
{
    using ld::grbl::GrblConnection;
    // Die Verbindung gehört dem ConnectionController (Anschluss, Selbsttest, Alarme, Nullpunkt); wir zeigen den Zustand.
    connection_ = new ConnectionController(machine_, this, this);
    grbl_ = connection_->grbl();
    connect(connection_, &ConnectionController::info, this, [this](const QString& text) { console_->appendInfo(text); });
    connect(connection_, &ConnectionController::error, this, [this](const QString& text) { console_->appendError(text); });
    connect(connection_, &ConnectionController::message, this, [this](const QString& text, int ms) { statusBar()->showMessage(text, ms); });
    connect(connection_, &ConnectionController::versionChanged, this, &MainWindow::updateConnectionUi);
    connect(connection_, &ConnectionController::alarmStateChanged, this, [this](bool possible) { unlockAction_->setEnabled(possible); });
    connect(connection_, &ConnectionController::grblSettingsRequested, this, &MainWindow::openGrblSettings);
    connect(connection_, &ConnectionController::profileMaxPowerRequested, this, [this](int s30) {
        machine_.maxPower = s30;
        profiles_->save(machine_);
        applyMachine(machine_);
        console_->appendInfo(tr("Maximalleistung im Profil auf %1 gesetzt.").arg(s30));
    });
    connect(connection_, &ConnectionController::baudRateFound, this, [this](int baud) {
        machine_.baudRate = baud;
        profiles_->save(machine_);
        leftPanel_->setBaudRate(baud);
    });

    refreshPortList();
    leftPanel_->setBaudRate(machine_.baudRate);
    connect(leftPanel_, &LeftPanel::portListRequested, this, &MainWindow::refreshPortList);
    connect(leftPanel_, &LeftPanel::connectRequested, connection_, &ConnectionController::connectTo);
    connect(leftPanel_, &LeftPanel::disconnectRequested, grbl_, &GrblConnection::disconnectDevice);
    connect(leftPanel_, &LeftPanel::baudChanged, this, [this](int baud) {
        if (machine_.baudRate == baud) return;
        machine_.baudRate = baud;
        profiles_->save(machine_);   // die Baudrate gehört zum Maschinenprofil
    });

    connect(connection_, &ConnectionController::stateChanged, this, [this] {
        updateConnectionUi();
        updateMotionEnabled();
        updateRunButtons();
        grblParamsAction_->setEnabled(grbl_->isConnected());
        firmwareAction_->setEnabled(grbl_->isConnected());
        if (!grbl_->isConnected()) {
            canvas_->hideHead();
            endLive();   // Regler und Sperren zurücknehmen (ein laufender Auftrag ist damit zu Ende)
        }
    });
    connect(leftPanel_, &LeftPanel::baudTestRequested, connection_, &ConnectionController::runBaudTest);
    connect(grbl_, &GrblConnection::statusChanged, this, [this](const ld::grbl::GrblStatus& status) {
        updateGrblLabel();
        if (status.hasPosition) {
            const QString text = tr("X %1 · Y %2").arg(formatLength(status.wpos.x(), 1), formatLength(status.wpos.y(), 1));
            posLabel_->setText(text);
            leftPanel_->setPositionText(tr("Position: ") + text);
        }
        jobs_->trackLaser(status);
        updateMotionEnabled();
        updateRunButtons();
        updateWeakLightUi();
    });

    // Jog, Nullpunkt, Rahmen, Pause (M4 Teil 2)
    jog_ = new JogController(grbl_, this);
    jog_->setMachine(machine_);
    connect(leftPanel_, &LeftPanel::jogPressed, jog_, &JogController::press);
    connect(leftPanel_, &LeftPanel::jogReleased, jog_, &JogController::release);
    connect(leftPanel_, &LeftPanel::stepChanged, jog_, &JogController::setStepMm);
    jog_->setStepMm(AppSettings::instance().jogSteps().value(AppSettings::instance().jogDefaultIndex(), 10.0));
    connect(&AppSettings::instance(), &AppSettings::jogChanged, this, [this] {
        leftPanel_->setJogSteps(AppSettings::instance().jogSteps(), AppSettings::instance().jogDefaultIndex());
    });
    connect(leftPanel_, &LeftPanel::materialTestRequested, this, &MainWindow::openMaterialTest);
    connect(leftPanel_, &LeftPanel::setZeroRequested, connection_, &ConnectionController::setZero);
    connect(leftPanel_, &LeftPanel::goZeroRequested, connection_, &ConnectionController::goZero);
    connect(rightPanel_->testSizeButton(), &QPushButton::clicked, this, &MainWindow::onTestSizeClicked);
    qApp->installEventFilter(this);

    // Schwachlicht und Live-Regler (M8 Teil 1)
    weak_ = new ld::grbl::WeakLight(grbl_, this);
    jobs_ = new JobController(grbl_, weak_, pathPreview_, document_, machine_, [this] { return ncMode_; }, this, this);
    connect(jobs_, &JobController::started, this, &MainWindow::onJobStarted);
    connect(jobs_, &JobController::finished, this, &MainWindow::onJobFinished);
    connect(jobs_, &JobController::message, this, [this](const QString& text, int ms) { statusBar()->showMessage(text, ms); });
    connect(jobs_, &JobController::info, this, [this](const QString& text) { console_->appendInfo(text); });
    connect(jobs_, &JobController::error, this, [this](const QString& text) { console_->appendError(text); });
    connect(jobs_, &JobController::progress, this, [this](int percent) { progressBar_->setValue(percent); });
    connect(jobs_, &JobController::durationText, this, [this](const QString& text) { durationLabel_->setText(text); });
    connect(jobs_, &JobController::previewRequested, this, &MainWindow::onPreviewStateChanged);
    connect(jobs_, &JobController::emergencyStopRequested, this, &MainWindow::onEmergencyStop);
    connect(jobs_, &JobController::pathAdvanced, this, [this](size_t first, size_t last, const QPointF& head) {
        if (const auto data = pathPreview_->data()) canvas_->advanceProgress(*data, first, last, head);
    });
    connect(jobs_, &JobController::headMoved, this, [this](const QPointF& head) { canvas_->showHead(head); });
    connect(jobs_, &JobController::headIdle, this, [this](const QPointF& head) {
        if (simulation_->state() == ld::core::Simulation::State::Stopped) canvas_->showHead(head);   // Fadenkreuz am echten Laser
    });
    // Inhalte der Arbeitsfläche (Bild, Text, Messgrafik) und die Werkzeug-Dialoge (Materialtest, Kalibrierung)
    content_ = new ContentController(document_, machine_, projectController_, [this] { return rightPanel_->pixelSizeMm(); },
                                     [this] { return grbl_ && grbl_->isJobActive(); }, [this] { return ncMode_; }, this, this);
    connect(content_, &ContentController::message, this, [this](const QString& text, int ms) { statusBar()->showMessage(text, ms); });
    connect(content_, &ContentController::contentChanged, this, &MainWindow::updateWindowTitle);
    tools_ = new ToolDialogs(document_, machine_, profiles_, jobs_, content_, [this] { return rightPanel_->pixelSizeMm(); },
                             [this] { return std::make_pair(rightPanel_->frameSpeedMmPerMin(), rightPanel_->framePowerPercent()); },
                             this, this);
    connect(tools_, &ToolDialogs::message, this, [this](const QString& text, int ms) { statusBar()->showMessage(text, ms); });
    connect(tools_, &ToolDialogs::materialRequested, this, [this](const ld::core::Operation& op, const QString& note) {
        leftPanel_->saveMaterialFromOperation(op, note);
    });
    connect(tools_, &ToolDialogs::machineSaved, this, &MainWindow::applyMachine);

    // Protokolle: Aufträge und Systemereignisse als CSV (planung.md 3.38)
    protocol_ = new ProtocolController(ProtocolController::defaultFolder(), jobs_, connection_, document_, machine_,
                                       [this] { return leftPanel_->currentMaterialName(); },
                                       [this] { return QFileInfo(ncSourcePath_).fileName(); }, [this] { return ncMode_; }, this);
    protocol_->recoverAtStart();
    notifier_ = new NotificationController(jobs_, connection_, protocol_, this);   // ntfy (planung.md 3.40)
    remote_ = new RemoteController(jobs_, connection_, protocol_, this);          // Fernansicht (planung.md 3.41)
    connect(remote_, &RemoteController::activity, this, [this](const QString& text) { statusBar()->showMessage(text, 6000); });
    remote_->setPreviewSource({[this] { return canvas_->hasProgress(); }, [this] { return canvas_->progressRevision(); },
                               [this](int px) { return canvas_->workSnapshot(px); }});
    remote_->setLiveBaseSource([this] {
        RemoteController::LiveBase base;
        if (rightPanel_->liveActive()) {
            base.valid = true;
            base.speedMmPerMin = rightPanel_->liveBaseSpeedMmPerMin();
            base.powerPercent = rightPanel_->liveBasePowerPercent();
        } else if (ncMode_) {
            base.valid = true;
            base.percentOnly = true;
        }
        if (base.valid) {
            base.speedValue = rightPanel_->speedSliderValue();
            base.speedMin = rightPanel_->speedSliderMin();
            base.speedMax = rightPanel_->speedSliderMax();
            base.powerValue = rightPanel_->powerSliderValue();
            base.powerMin = rightPanel_->powerSliderMin();
            base.powerMax = rightPanel_->powerSliderMax();
        }
        return base;
    });
    remote_->applySettings();
    protocol_->log(ld::core::LogLevel::Info, tr("Programm"), tr("Programm gestartet"),
                   tr("Version %1 · %2 · Qt %3").arg(QCoreApplication::applicationVersion(), QSysInfo::prettyProductName(),
                                                    QString::fromLatin1(qVersion())));
    connect(tools_, &ToolDialogs::machineSaved, this, [this](const ld::core::MachineSetup& saved) {
        protocol_->log(ld::core::LogLevel::Info, tr("Profil"), tr("Korrekturfaktoren im Profil \"%1\" gespeichert").arg(saved.name),
                       tr("X %1, Y %2").arg(saved.correctionX, 0, 'f', 4).arg(saved.correctionY, 0, 'f', 4));
    });
    applyLaserSettings();
    leftPanel_->setWeakLightValues(weak_->percent(), weak_->autoOffSeconds());
    connect(&AppSettings::instance(), &AppSettings::laserChanged, this, &MainWindow::applyLaserSettings);
    connect(leftPanel_, &LeftPanel::weakLightToggled, this, [this](bool on) {
        if (on) {
            if (!weak_->turnOn()) {
                statusBar()->showMessage(tr("Das Schwachlicht geht jetzt nicht (nicht verbunden, GRBL nicht bereit oder ein Auftrag läuft)."), 5000);
            }
        } else {
            weak_->turnOff(true);
        }
        updateWeakLightUi();
    });
    connect(leftPanel_, &LeftPanel::weakLightPowerChanged, weak_, &ld::grbl::WeakLight::setPercent);
    connect(leftPanel_, &LeftPanel::weakLightAutoOffChanged, this, [this](int seconds) {
        weak_->setAutoOffSeconds(seconds);
        weak_->touch();
    });
    connect(leftPanel_, &LeftPanel::jogPressed, weak_, &ld::grbl::WeakLight::touch);   // Bedienen verlängert die Zeit
    connect(weak_, &ld::grbl::WeakLight::changed, this, &MainWindow::updateWeakLightUi);
    connect(weak_, &ld::grbl::WeakLight::autoOffTriggered, this, [this] {
        statusBar()->showMessage(tr("Schwachlicht automatisch ausgeschaltet."), 5000);
        console_->appendInfo(tr("Schwachlicht automatisch ausgeschaltet."));
    });
    connect(rightPanel_, &RightPanel::liveSpeedChanged, grbl_, &GrblConnection::setFeedOverride);
    connect(rightPanel_, &RightPanel::livePowerChanged, grbl_, &GrblConnection::setSpindleOverride);
    connect(grbl_, &GrblConnection::overridesChanged, this, [this](int feed, int spindle) { rightPanel_->setLiveValues(feed, spindle); });
    updateWeakLightUi();
    // Das Schwachlicht sendet etwa 20 Fahrzeilen pro Sekunde; sie und ihre "ok" würden die Konsole fluten.
    connect(grbl_, &GrblConnection::lineReceived, console_, [this](const QString& line) {
        if (weak_->isOn() && line == QStringLiteral("ok")) return;
        console_->appendReceived(line);
    });
    connect(grbl_, &GrblConnection::lineSent, console_, [this](const QString& line) {
        if (weak_->isOn() && line.startsWith(QStringLiteral("G1 X")) && line.contains(QStringLiteral("0.1 S"))) return;
        console_->appendSent(line);
    });
    connect(console_, &ConsoleWidget::commandEntered, this, [this](const QString& line) {
        if (!grbl_->sendLine(line)) {
            console_->appendError(grbl_->isConnected() ? tr("Während eines Auftrags sind Befehle gesperrt.")
                                                       : tr("Nicht verbunden."));
        }
    });
    connect(grbl_, &GrblConnection::jobAborted, this, [this](const QString& reason) {
        console_->setInputEnabled(true);
        console_->appendError(reason);
        statusBar()->showMessage(reason, 8000);
        endLive();
        updateRunButtons();
        updateMotionEnabled();
    });
    updateConnectionUi();
    updateMotionEnabled();
}

void MainWindow::refreshPortList()
{
    const QString last = QSettings().value(QStringLiteral("connection/lastPort/") + machine_.name).toString();
    leftPanel_->setPortChoices(listPortChoices(), last);
}

void MainWindow::updateConnectionUi()
{
    using State = ld::grbl::GrblConnection::State;
    const State state = grbl_->state();
    leftPanel_->setConnectionState(state);
    switch (state) {
    case State::Disconnected:
        connLed_->setStyleSheet(QStringLiteral("color: #8a8a8e;"));
        connText_->setText(tr("Nicht verbunden"));
        posLabel_->setText(tr("X – · Y –"));
        break;
    case State::Connecting:
        connLed_->setStyleSheet(QStringLiteral("color: #d9a400;"));
        connText_->setText(tr("Verbinde …"));
        break;
    case State::Connected:
        connLed_->setStyleSheet(QStringLiteral("color: #2e9e4f;"));
        connText_->setText(tr("Verbunden (%1)%2")
                               .arg(grbl_->port() ? grbl_->port()->description() : QString(),
                                    connection_->grblVersion().isEmpty() ? QString() : tr(" · GRBL %1").arg(connection_->grblVersion().section(QLatin1Char('.'), 0, 1))));
        break;
    }
    updateGrblLabel();
}

void MainWindow::updateGrblLabel()
{
    if (simulation_->state() != ld::core::Simulation::State::Stopped) {
        grblLabel_->setText(tr("GRBL: Simulation"));
    } else if (grbl_ && grbl_->isConnected()) {
        grblLabel_->setText(tr("GRBL: %1").arg(ld::grbl::stateName(grbl_->status().state)));
    } else {
        grblLabel_->setText(tr("GRBL: –"));
    }
}

void MainWindow::applyLaserSettings()
{
    const AppSettings& settings = AppSettings::instance();
    weak_->setMaxPercent(settings.weakLightMaxPercent());
    weak_->setAutoOffSeconds(settings.weakLightAutoOffSeconds());
    weak_->setMaxPowerS(machine_.maxPower);
    leftPanel_->setWeakLightRange(weak_->maxPercent());
    leftPanel_->setWeakLightValues(weak_->percent(), weak_->autoOffSeconds());
}

void MainWindow::updateWeakLightUi()
{
    if (!weak_) return;
    leftPanel_->setWeakLightState(weak_->isOn(), weak_->canTurnOn());
    if (weak_->isOn()) {
        const int left = weak_->remainingSeconds();
        laserOnLabel_->setText(left >= 0 ? tr("LASER AN (noch %1 s)").arg(left) : tr("LASER AN"));
        laserOnLabel_->show();
    } else {
        laserOnLabel_->hide();
    }
}

void MainWindow::beginLive()
{
    weak_->turnOff(true);   // das Schwachlicht geht vor jedem Lauf aus
    grbl_->resetOverrides();
    // Beim Rahmen (Rahmen) gelten eigene Werte, dort bleiben die Gravur-Regler unberührt.
    if (!jobs_->isFrameLike()) {
        rightPanel_->beginLiveControl();
        rightPanel_->setEditingLocked(true);
        canvas_->setResizeEnabled(false);
        leftPanel_->setMaterialLocked(true);
    }
}

void MainWindow::endLive()
{
    rightPanel_->endLiveControl();
    rightPanel_->setEditingLocked(false);
    canvas_->setResizeEnabled(true);
    leftPanel_->setMaterialLocked(false);
    if (grbl_->isConnected()) grbl_->resetOverrides();
}

void MainWindow::openGrblSettings()
{
    if (!grbl_->isConnected()) return;
    GrblSettingsDialog dialog(grbl_, this);
    dialog.exec();
}

// ---------------------------------------------------------------------------------------------
// Steuerung und Lauf am Laser (M4 Teil 2, siehe planung.md 3.15)
// ---------------------------------------------------------------------------------------------
void MainWindow::updateMotionEnabled()
{
    using ld::grbl::MachineState;
    const bool connected = grbl_->isConnected();
    const bool jobActive = grbl_->isJobActive();
    const bool idle = connected && !jobActive && grbl_->status().state == MachineState::Idle;
    leftPanel_->setMotionEnabled(jog_->canJog(), idle);
}

void MainWindow::startLaserJob()
{
    if ((!ncMode_ && !document_->hasImage()) || !checkWorkArea()) return;
    jobs_->requestStart();   // Fragen, Start und Warten auf die Laserwege übernimmt der JobController
}

void MainWindow::onTestSizeClicked()
{
    jobs_->startFrame(rightPanel_->frameSpeedMmPerMin(), rightPanel_->framePowerPercent());
}

// ---------------------------------------------------------------------------------------------
// NC-Datei laden (siehe planung.md 3.21)
// ---------------------------------------------------------------------------------------------
bool MainWindow::jobAvailable() const
{
    if (ncMode_) return pathPreview_->state() == ld::core::PathPreview::State::Ready;
    return document_->hasImage() && !document_->raster().isNull() && !document_->isRasterBusy();
}

void MainWindow::loadNcFile(const QString& pathArg, bool silent)
{
    if (grbl_ && grbl_->isJobActive()) {
        QMessageBox::information(this, tr("NC laden"), tr("Es läuft gerade ein Auftrag."));
        return;
    }
    QString path = pathArg;
    if (path.isEmpty()) {
        const QString dir = AppSettings::instance().startFolder(FolderKind::Nc);
        path = QFileDialog::getOpenFileName(this, tr("NC-Datei laden"), dir,
                                            tr("NC-Dateien (*.nc *.gcode *.ngc *.tap *.gc *.txt);;Alle Dateien (*)"));
        if (path.isEmpty()) return;
        AppSettings::instance().setLastFolder(FolderKind::Nc, path);
    }
    if (simulation_->state() != ld::core::Simulation::State::Stopped) stopSimulation(tr("Simulation abgebrochen."));
    ncSilent_ = silent;
    const QString dst = ncDir_.filePath(QStringLiteral("nc-%1.nc").arg(++ncCounter_));
    if (!silent) {
        if (!ncProgress_) {
            ncProgress_ = new QProgressDialog(tr("NC-Datei wird geprüft …"), tr("Abbrechen"), 0, 100, this);
            ncProgress_->setWindowTitle(tr("NC laden"));
            ncProgress_->setWindowModality(Qt::WindowModal);
            ncProgress_->setMinimumDuration(400);
            connect(ncProgress_, &QProgressDialog::canceled, ncLoader_, &ld::core::NcLoader::cancel);
        }
        ncProgress_->setValue(0);
    }
    ncLoader_->load(path, dst, machine_);
}

// Der Meldungsdialog: welche Zeilen nicht gesendet oder geändert werden. true = trotzdem laden.
bool MainWindow::confirmNcIssues(const ld::core::NcAnalysis& result)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("NC-Datei: Hinweise"));
    auto* layout = new QVBoxLayout(&dialog);
    QString summary = tr("Die Datei wurde geprüft. ");
    if (result.blockedLines > 0) summary += tr("%L1 Zeile(n) werden NICHT gesendet. ").arg(result.blockedLines);
    if (result.clampedLines > 0) summary += tr("%L1 Zeile(n) hatten ein zu großes S (Maximum der Maschine) und werden begrenzt. ").arg(result.clampedLines);
    summary += tr("Die Originaldatei bleibt unverändert; gesendet wird eine bereinigte Kopie.");
    auto* label = new QLabel(summary);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    layout->addWidget(label);
    auto* table = new QTableWidget(static_cast<int>(result.issues.size()), 3);
    table->setHorizontalHeaderLabels({tr("Zeile"), tr("Hinweis"), tr("Inhalt")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->hide();
    for (int row = 0; row < static_cast<int>(result.issues.size()); ++row) {
        const ld::core::NcIssue& issue = result.issues[static_cast<size_t>(row)];
        table->setItem(row, 0, new QTableWidgetItem(QString::number(issue.line)));
        table->setItem(row, 1, new QTableWidgetItem(issue.text));
        table->setItem(row, 2, new QTableWidgetItem(issue.content));
    }
    table->resizeColumnsToContents();
    layout->addWidget(table, 1);
    if (result.moreIssues > 0) layout->addWidget(new QLabel(tr("… und %L1 weitere Hinweise.").arg(result.moreIssues)));
    auto* buttons = new QDialogButtonBox;
    buttons->addButton(tr("Trotzdem laden"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Abbrechen"), QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(760, 420);
    return dialog.exec() == QDialog::Accepted;
}

void MainWindow::onNcChecked(const ld::core::NcAnalysis& result, const QString& src, const QString& dst, bool silent)
{
    if (ncProgress_) ncProgress_->reset();
    if (result.cancelled) return;
    if (!result.ok) {
        QMessageBox::warning(this, tr("NC laden"), result.error.isEmpty() ? tr("Die Datei kann nicht geladen werden.") : result.error);
        return;
    }
    if (result.sentLines == 0) {
        QMessageBox::warning(this, tr("NC laden"), tr("Die Datei enthält keinen Befehl, der gesendet werden dürfte."));
        QFile::remove(dst);
        return;
    }
    if (result.hasProblems() && !silent && !confirmNcIssues(result)) {
        QFile::remove(dst);
        return;
    }
    // In den NC-Modus wechseln. Das Bild bleibt geladen (ausgegraut), das Projekt unberührt.
    if (simulation_->state() != ld::core::Simulation::State::Stopped) stopSimulation(tr("Simulation abgebrochen."));
    canvas_->clearProgress();
    ncMode_ = true;
    ncSourcePath_ = src;
    ncFitPending_ = true;
    ld::core::GcodeJob job;
    job.machine = ld::core::withoutCorrection(machine_);   // fremde NC-Dateien werden unverändert gesendet
    job.origin = ld::core::JobOrigin::TopLeft;   // Größe 0: Nullpunkt der Anzeige = Nullpunkt der Datei
    job.transform.widthMm = 0.0;
    job.transform.heightMm = 0.0;
    ld::core::GcodeStats stats;
    stats.lines = result.sentLines;
    stats.bytes = QFileInfo(dst).size();
    rightPanel_->setNcMode(true);
    canvas_->setNcMode(true, QRectF());
    closeNcAction_->setEnabled(true);
    pathPreview_->setExternalFile(dst, job, stats);
    updateWindowTitle();
    updateRunButtons();
    if (result.hasProblems()) {
        console_->appendInfo(tr("NC-Datei geladen: %L1 Zeile(n) nicht gesendet, %L2 begrenzt.").arg(result.blockedLines).arg(result.clampedLines));
    }
    statusBar()->showMessage(tr("NC-Datei geladen: %1").arg(QFileInfo(src).fileName()), 6000);
}

// Knopf "Leeren": Arbeitsfläche leeren = Projekt schließen (wie Datei > Neu, mit Rückfrage zum Speichern); eine geladene
// NC-Datei wird ebenfalls geschlossen.
void MainWindow::clearWorkArea()
{
    if (grbl_ && grbl_->isJobActive()) {
        QMessageBox::information(this, tr("Leeren"), tr("Es läuft gerade ein Auftrag."));
        return;
    }
    if (!projectController_->newProject()) return;   // Abbruch bei der Frage zum Speichern
    leaveNcMode();
}

void MainWindow::leaveNcMode()
{
    if (!ncMode_) return;
    if (grbl_ && grbl_->isJobActive()) {
        QMessageBox::information(this, tr("NC schließen"), tr("Es läuft gerade ein Auftrag."));
        return;
    }
    if (simulation_->state() != ld::core::Simulation::State::Stopped) stopSimulation(tr("Simulation abgebrochen."));
    ncLoader_->cancel();
    ncMode_ = false;
    ncSourcePath_.clear();
    ncFitPending_ = false;
    pathPreview_->clearJob();   // verwirft auch die bereinigte Kopie
    canvas_->setPaths(nullptr);
    canvas_->setPathMessage(QString());
    rightPanel_->setNcMode(false);
    canvas_->setNcMode(false);
    closeNcAction_->setEnabled(false);
    onJobChanged();   // die Wege des Bildes wieder aufbauen
    updateWindowTitle();
    updateRunButtons();
    durationLabel_->setText(tr("Dauer –"));
    updateDurationLabel();
}

// ---------------------------------------------------------------------------------------------
// Materialtest (M9, siehe planung.md 3.20)
// ---------------------------------------------------------------------------------------------
void MainWindow::openProtocol()
{
    if (!protocolDialog_) {
        protocolDialog_ = new ProtocolDialog(protocol_, this);
        protocolDialog_->setAttribute(Qt::WA_DeleteOnClose);
    }
    protocolDialog_->show();
    protocolDialog_->raise();
    protocolDialog_->activateWindow();
}

void MainWindow::openMaterialTest()
{
    tools_->openMaterialTest();
    updateRunButtons();   // gibt dem Dialog den Zustand des Lasers
}

// Beginn eines Laufs (Signal des JobControllers): Live-Regler, Arbeitsfläche, Konsole und Schalter einstellen.
void MainWindow::onJobStarted(JobController::Kind kind)
{
    using Kind = JobController::Kind;
    beginLive();
    if (kind == Kind::Image) {
        canvas_->clearProgress();
        canvas_->beginProgress();
    }
    if (kind != Kind::Frame) progressBar_->setValue(0);
    console_->setInputEnabled(false);
    updateRunButtons();
    updateMotionEnabled();
}

// Ende eines Laufs (Signal des JobControllers).
void MainWindow::onJobFinished(JobController::Kind kind)
{
    using Kind = JobController::Kind;
    console_->setInputEnabled(true);
    endLive();
    if (kind == Kind::Frame || kind == Kind::MaterialTest) {
        const QString what = kind == Kind::MaterialTest ? tr("Materialtest") : tr("Rahmen");
        if (kind == Kind::MaterialTest) progressBar_->setValue(100);
        statusBar()->showMessage(tr("%1 fertig.").arg(what), 5000);
        console_->appendInfo(tr("%1 fertig.").arg(what));
        updateDurationLabel();
    } else {
        const auto data = pathPreview_->data();
        if (data) canvas_->advanceProgress(*data, jobs_->progressIndex(), data->segments.size(), QPointF());
        canvas_->hideHead();
        progressBar_->setValue(100);
        const QString text = tr("Fertig nach %1.").arg(ld::core::formatDuration(jobs_->elapsedSeconds()));
        statusBar()->showMessage(text, 10000);
        console_->appendInfo(text);
        updateDurationLabel();   // Dauer-Anzeige wieder auf die Schätzung stellen
    }
    updateRunButtons();
    updateMotionEnabled();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) {
        return QMainWindow::eventFilter(watched, event);
    }
    auto* key = static_cast<QKeyEvent*>(event);
    const int k = key->key();
    const bool arrow = k == Qt::Key_Left || k == Qt::Key_Right || k == Qt::Key_Up || k == Qt::Key_Down;
    if (!(arrow || k == Qt::Key_Space) || !isActiveWindow() || QApplication::activeModalWidget() ||
        (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        return QMainWindow::eventFilter(watched, event);
    }
    // Nur, wenn gerade kein Eingabefeld die Taste braucht: Fokus nirgends, auf der Arbeitsfläche oder (Pfeile) auf einem Knopf.
    QWidget* focus = QApplication::focusWidget();
    const bool onCanvas = !focus || focus == canvas_ || canvas_->isAncestorOf(focus);
    const bool onButton = qobject_cast<QAbstractButton*>(focus) != nullptr;
    if (!(onCanvas || (arrow && onButton))) {
        return QMainWindow::eventFilter(watched, event);
    }
    if (arrow) {
        const QPointF direction = k == Qt::Key_Left ? QPointF(-1, 0) : k == Qt::Key_Right ? QPointF(1, 0)
                                  : k == Qt::Key_Up ? QPointF(0, 1) : QPointF(0, -1);
        if (event->type() == QEvent::KeyPress) {
            if (key->isAutoRepeat()) return heldKey_ != 0;   // die Wiederholung macht der JogController
            if (!jog_->canJog()) return QMainWindow::eventFilter(watched, event);
            heldKey_ = k;
            jog_->press(direction);
            return true;
        }
        if (!key->isAutoRepeat() && heldKey_ == k) {
            heldKey_ = 0;
            jog_->release();
            return true;
        }
        return QMainWindow::eventFilter(watched, event);
    }
    // Leertaste: Pause/Fortsetzen, wenn der Knopf aktiv ist.
    if (event->type() == QEvent::KeyPress && !key->isAutoRepeat() && rightPanel_->pauseButton()->isEnabled()) {
        onPauseClicked();
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::showAbout()
{
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!projectController_->maybeSave()) {
        event->ignore();   // Abbrechen: das Programm bleibt offen
        return;
    }
    if (grbl_->state() != ld::grbl::GrblConnection::State::Disconnected) {
        if (grbl_->isJobActive() &&
            QMessageBox::question(this, tr("Beenden"),
                                  tr("Der Laser arbeitet noch. Beim Beenden wird der Auftrag abgebrochen und der Laser ausgeschaltet.\n\nTrotzdem beenden?"),
                                  QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        grbl_->disconnectDevice();   // Reset und M5, dann Port schließen
    }
    protocol_->log(ld::core::LogLevel::Info, tr("Programm"), tr("Programm beendet"));
    projectController_->saveSession();       // Bild und Einstellungen für den nächsten Start merken
    AppSettings::instance().setLastOperation(document_->operation());   // Gravurart, Speed, Leistung usw. merken
    AppSettings::instance().setLastFrame(rightPanel_->frameSpeedMmPerMin(), rightPanel_->framePowerPercent());
    projectController_->discardRecovery();   // normales Beenden: keine Wiederherstellung nötig
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    QMainWindow::closeEvent(event);
}

}  // namespace ld::app
