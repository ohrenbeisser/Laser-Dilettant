// Hilfefenster: zeigt das Handbuch (MANUAL.md, als Ressource eingebettet) mit Inhaltsverzeichnis und Suche.
//
//   * Menü Hilfe → Handbuch (F1) öffnet es am Anfang.
//   * F1 in einem Dialog öffnet es beim passenden Kapitel (attachHelp, Themen in core/helpdocument.h); dazu bekommt der
//     Dialog, wenn er eine Knopfleiste hat, einen Knopf "Hilfe".
// Das Fenster ist nicht modal, damit der NOT-AUS bedienbar bleibt. Wird es aus einem modalen Dialog geöffnet, bekommt es
// diesen als Elternfenster; sonst wäre es blockiert, solange der Dialog offen ist.
#pragma once

#include <QPointer>
#include <QString>
#include <QWidget>

#include "core/helpdocument.h"

class QLineEdit;
class QTextBrowser;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace ld::app {

class HelpWindow : public QWidget {
    Q_OBJECT

public:
    // Öffnet die Hilfe (from: das Fenster, aus dem sie aufgerufen wird) beim Thema (leer: Anfang).
    static void open(QWidget* from, const QString& topic = QString());

    explicit HelpWindow(QWidget* parent = nullptr);
    void showTopic(const QString& topic);
    void showLocation(const ld::core::HelpLocation& location);

private:
    void fillTree();
    void applySearch();
    void onAnchorClicked(const QUrl& url);
    void findNext(bool fromStart);
    void renderChapter();   // zeigt das aktuelle Kapitel (Bilder passend zur Breite)
    bool eventFilter(QObject* watched, QEvent* event) override;

    ld::core::HelpDocument document_;
    QLineEdit* search_;
    QTreeWidget* tree_;
    QTextBrowser* browser_;
    int currentChapter_ = -1;
    int renderedWidth_ = 0;   // Breite, für die das Kapitel zuletzt gezeigt wurde
    QTimer* resizeTimer_ = nullptr;
    static QPointer<HelpWindow> instance_;
};

// Macht F1 im Dialog zur Hilfe zum Thema und fügt, wenn der Dialog eine Knopfleiste hat, den Knopf "Hilfe" hinzu.
// Am Ende des Konstruktors des Dialogs aufrufen.
void attachHelp(QDialog* dialog, const char* topic);

}  // namespace ld::app
