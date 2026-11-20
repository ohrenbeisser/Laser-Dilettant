#include "helpwindow.h"

#include <QApplication>
#include <QEvent>
#include <QPushButton>
#include <QScrollBar>
#include <functional>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QShortcut>
#include <QSplitter>
#include <QImage>
#include <QTextBlock>
#include <QTimer>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace ld::app {

QPointer<HelpWindow> HelpWindow::instance_;

namespace {

// Das Handbuch hat Bilder (manual/bilder). Sie liegen als Ressourcen im Programm. Zu breite Bilder werden auf die Breite des
// Fensters verkleinert (QTextBrowser tut das nicht selbst).
class HelpBrowser : public QTextBrowser {
public:
    using QTextBrowser::QTextBrowser;

protected:
    QVariant loadResource(int type, const QUrl& name) override
    {
        if (type == QTextDocument::ImageResource) {
            QString path = name.scheme() == QLatin1String("qrc") ? QStringLiteral(":") + name.path() : name.toString();
            if (!path.startsWith(QLatin1Char(':'))) path = QStringLiteral(":/help/") + path;
            QImage image(path);
            if (!image.isNull()) {
                const int maxWidth = viewport()->width() - 48;
                if (maxWidth > 120 && image.width() > maxWidth) image = image.scaledToWidth(maxWidth, Qt::SmoothTransformation);
                return image;
            }
        }
        return QTextBrowser::loadResource(type, name);
    }
};

}  // namespace

static QString loadManual()
{
    QFile file(QStringLiteral(":/help/MANUAL.md"));
    if (!file.open(QIODevice::ReadOnly)) return QString();
    return ld::core::prepareHelpMarkdown(QString::fromUtf8(file.readAll()));
}

void HelpWindow::open(QWidget* from, const QString& topic)
{
    // Aus einem modalen Dialog heraus: eigenes Fenster als Kind dieses Dialogs (sonst wäre es blockiert).
    if (QWidget* modal = QApplication::activeModalWidget()) {
        auto* window = new HelpWindow(modal);
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->showTopic(topic);
        window->show();
        return;
    }
    if (!instance_) instance_ = new HelpWindow(from ? from->window() : nullptr);
    instance_->showTopic(topic);
    instance_->show();
    instance_->raise();
    instance_->activateWindow();
}

HelpWindow::HelpWindow(QWidget* parent) : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("Handbuch – Laser-Dilettant"));
    resize(980, 680);
    document_ = ld::core::HelpDocument::parse(loadManual());

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* splitter = new QSplitter;
    layout->addWidget(splitter);

    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(8, 8, 4, 8);
    search_ = new QLineEdit;
    search_->setPlaceholderText(tr("Im Handbuch suchen …"));
    search_->setClearButtonEnabled(true);
    leftLayout->addWidget(search_);
    tree_ = new QTreeWidget;
    tree_->setHeaderHidden(true);
    tree_->setIndentation(14);
    tree_->setUniformRowHeights(true);
    leftLayout->addWidget(tree_, 1);
    splitter->addWidget(left);

    browser_ = new HelpBrowser;
    browser_->document()->setBaseUrl(QUrl(QStringLiteral("qrc:/help/")));   // Bilder des Handbuchs: relative Pfade zur Ressource
    browser_->setOpenLinks(false);   // Sprünge im Handbuch machen wir selbst
    browser_->setOpenExternalLinks(false);
    browser_->document()->setDefaultStyleSheet(QStringLiteral(
        "h1 { font-size: 22pt; } h2 { font-size: 17pt; margin-top: 18px; } h3 { font-size: 13pt; margin-top: 14px; }"
        "table { border-collapse: collapse; } th, td { padding: 4px 8px; }"
        "code, pre { font-family: monospace; }"));
    browser_->setContentsMargins(8, 4, 8, 4);
    splitter->addWidget(browser_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({280, 700});

    fillTree();
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (!item) return;
        showLocation({item->data(0, Qt::UserRole).toInt(), item->data(0, Qt::UserRole + 1).toString()});
    });
    connect(search_, &QLineEdit::textChanged, this, &HelpWindow::applySearch);
    connect(search_, &QLineEdit::returnPressed, this, [this] { findNext(false); });
    connect(browser_, &QTextBrowser::anchorClicked, this, &HelpWindow::onAnchorClicked);
    // Ändert sich die Breite, werden die Bilder neu auf die Breite gerechnet (kurz verzögert, nicht bei jedem Pixel)
    resizeTimer_ = new QTimer(this);
    resizeTimer_->setSingleShot(true);
    resizeTimer_->setInterval(200);
    connect(resizeTimer_, &QTimer::timeout, this, [this] {
        if (currentChapter_ < 0 || browser_->viewport()->width() == renderedWidth_) return;
        const int scroll = browser_->verticalScrollBar()->value();
        renderChapter();
        browser_->verticalScrollBar()->setValue(scroll);
    });
    browser_->viewport()->installEventFilter(this);
    new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] { close(); });
    showTopic(QString());
}

bool HelpWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == browser_->viewport() && event->type() == QEvent::Resize) resizeTimer_->start();
    return QWidget::eventFilter(watched, event);
}

void HelpWindow::renderChapter()
{
    if (currentChapter_ < 0) return;
    renderedWidth_ = browser_->viewport()->width();
    browser_->clear();   // leert auch die Bilder, die für die alte Breite geladen wurden
    browser_->setMarkdown(document_.chapters()[currentChapter_].markdown);
}

void HelpWindow::fillTree()
{
    tree_->clear();
    const auto& chapters = document_.chapters();
    for (int c = 0; c < chapters.size(); ++c) {
        auto* item = new QTreeWidgetItem(tree_, {chapters[c].title});
        item->setData(0, Qt::UserRole, c);
        for (const ld::core::HelpHeading& h : chapters[c].headings) {
            if (h.level != 3) continue;
            auto* child = new QTreeWidgetItem(item, {h.title});
            child->setData(0, Qt::UserRole, c);
            child->setData(0, Qt::UserRole + 1, h.title);
        }
    }
}

void HelpWindow::applySearch()
{
    const QString text = search_->text().trimmed();
    const QList<int> hits = document_.searchChapters(text);
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = tree_->topLevelItem(i);
        item->setHidden(!text.isEmpty() && !hits.contains(item->data(0, Qt::UserRole).toInt()));
    }
    if (text.isEmpty() || hits.isEmpty()) return;
    // Im gezeigten Kapitel die erste Fundstelle markieren, sonst zum ersten Kapitel mit Treffer springen.
    if (!hits.contains(currentChapter_)) showLocation({hits.first(), QString()});
    findNext(true);
}

void HelpWindow::findNext(bool fromStart)
{
    const QString text = search_->text().trimmed();
    if (text.isEmpty()) return;
    if (fromStart) {
        QTextCursor start(browser_->document());
        browser_->setTextCursor(start);
    }
    if (!browser_->find(text)) {   // am Ende des Kapitels: wieder von vorn
        browser_->setTextCursor(QTextCursor(browser_->document()));
        browser_->find(text);
    }
}

void HelpWindow::showTopic(const QString& topic)
{
    const ld::core::HelpLocation location = document_.findTopic(topic);
    showLocation(location.valid() ? location : ld::core::HelpLocation{0, QString()});
}

void HelpWindow::showLocation(const ld::core::HelpLocation& location)
{
    if (!location.valid() || location.chapter >= document_.chapters().size()) return;
    if (location.chapter != currentChapter_) {
        currentChapter_ = location.chapter;
        renderChapter();
    }
    // Baum auf den Eintrag stellen (ohne die Anzeige noch einmal zu laden)
    QTreeWidgetItem* chapterItem = tree_->topLevelItem(location.chapter);
    QTreeWidgetItem* target = chapterItem;
    if (chapterItem && !location.heading.isEmpty()) {
        for (int i = 0; i < chapterItem->childCount(); ++i) {
            if (chapterItem->child(i)->text(0) == location.heading) target = chapterItem->child(i);
        }
    }
    if (target && tree_->currentItem() != target) {
        const QSignalBlocker blocker(tree_);
        tree_->setCurrentItem(target);
        if (target->parent()) target->parent()->setExpanded(true);
    }
    // zum Abschnitt springen
    if (!location.heading.isEmpty()) {
        const QTextCursor found = browser_->document()->find(location.heading);
        if (!found.isNull()) {
            browser_->setTextCursor(found);
            browser_->ensureCursorVisible();
            // die Überschrift an den oberen Rand holen
            const QRect rect = browser_->cursorRect(found);
            browser_->verticalScrollBar()->setValue(browser_->verticalScrollBar()->value() + rect.top() - 8);
        }
    } else {
        browser_->verticalScrollBar()->setValue(0);
    }
}

void HelpWindow::onAnchorClicked(const QUrl& url)
{
    const QString text = url.toString();
    if (text.startsWith(QLatin1Char('#'))) {
        const ld::core::HelpLocation location = document_.findAnchor(text);
        if (location.valid()) showLocation(location);
    } else if (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https")) {
        QDesktopServices::openUrl(url);
    }
    // andere Verweise (zum Beispiel auf die README.md) gibt es in der Hilfe nicht
}

void attachHelp(QDialog* dialog, const char* topic)
{
    const QString topicText = QString::fromUtf8(topic);
    auto openHelp = [dialog, topicText] { HelpWindow::open(dialog, topicText); };
    new QShortcut(QKeySequence(QKeySequence::HelpContents), dialog, openHelp);

    // Den Knopf "Hilfe" beim ersten Anzeigen hinzufügen (dann steht die Knopfleiste des Dialogs schon).
    class Adder : public QObject {
    public:
        Adder(QDialog* d, std::function<void()> f) : QObject(d), dialog_(d), open_(std::move(f)) { d->installEventFilter(this); }
        bool eventFilter(QObject* watched, QEvent* event) override
        {
            if (watched == dialog_ && event->type() == QEvent::Show) {
                dialog_->removeEventFilter(this);
                if (auto* box = dialog_->findChild<QDialogButtonBox*>()) {
                    if (!box->button(QDialogButtonBox::Help)) {
                        QPushButton* help = box->addButton(QDialogButtonBox::Help);
                        QObject::connect(help, &QPushButton::clicked, dialog_, open_);
                    }
                }
                deleteLater();
            }
            return false;
        }

    private:
        QDialog* dialog_;
        std::function<void()> open_;
    };
    new Adder(dialog, openHelp);
}

}  // namespace ld::app
