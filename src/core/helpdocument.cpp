#include "helpdocument.h"

#include <QRegularExpression>

namespace ld::core {

QString helpSlug(const QString& heading)
{
    QString slug;
    for (const QChar c : heading.trimmed().toLower()) {
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')) {
            slug += c;
        } else if (c == QLatin1Char(' ')) {
            slug += QLatin1Char('-');
        }
    }
    return slug;
}

QString prepareHelpMarkdown(const QString& manual)
{
    QString text = manual;
    text.replace(QStringLiteral("**(am Laser bestätigen)**"), QStringLiteral("*(noch nicht am Laser bestätigt)*"));
    return text;
}

HelpDocument HelpDocument::parse(const QString& markdown)
{
    HelpDocument doc;
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    HelpChapter current;
    current.title = QStringLiteral("Einleitung");
    bool inFence = false;
    bool skipping = false;   // das Inhaltsverzeichnis des Handbuchs überspringen
    auto flush = [&] {
        while (current.markdown.endsWith(QLatin1Char('\n'))) current.markdown.chop(1);
        // Trennlinien zwischen den Kapiteln gehören nicht zum Text
        if (current.markdown.endsWith(QStringLiteral("---"))) {
            current.markdown.chop(3);
            while (current.markdown.endsWith(QLatin1Char('\n'))) current.markdown.chop(1);
        }
        if (!current.markdown.trimmed().isEmpty()) doc.chapters_.append(current);
    };
    for (const QString& line : lines) {
        if (line.startsWith(QStringLiteral("```"))) inFence = !inFence;
        const bool isH2 = !inFence && line.startsWith(QStringLiteral("## "));
        const bool isH3 = !inFence && line.startsWith(QStringLiteral("### "));
        if (isH2) {
            flush();
            current = HelpChapter();
            current.title = line.mid(3).trimmed();
            skipping = current.title == QStringLiteral("Inhalt");
            current.markdown = line + QLatin1Char('\n');
            current.headings.append({2, current.title, helpSlug(current.title)});
            continue;
        }
        if (skipping) continue;
        if (isH3) {
            const QString title = line.mid(4).trimmed();
            current.headings.append({3, title, helpSlug(title)});
        }
        current.markdown += line + QLatin1Char('\n');
    }
    flush();
    // das Inhaltsverzeichnis ist übersprungen worden: sein Kapitel hat nur die Überschrift
    for (int i = doc.chapters_.size() - 1; i >= 0; --i) {
        if (doc.chapters_[i].title == QStringLiteral("Inhalt")) doc.chapters_.removeAt(i);
    }
    return doc;
}

HelpLocation HelpDocument::findTopic(const QString& topic) const
{
    if (topic.isEmpty()) return chapters_.isEmpty() ? HelpLocation() : HelpLocation{0, QString()};
    // Erst eine Überschrift, die genau so heißt (ohne die Nummer davor: "13. Einstellungen"), dann die erste, die den Text enthält.
    static const QRegularExpression numberPrefix(QStringLiteral("^\\d+\\.\\s*"));
    for (int c = 0; c < chapters_.size(); ++c) {
        for (const HelpHeading& h : chapters_[c].headings) {
            QString title = h.title;
            title.remove(numberPrefix);
            if (title.compare(topic, Qt::CaseInsensitive) == 0) return {c, h.level == 3 ? h.title : QString()};
        }
    }
    for (int c = 0; c < chapters_.size(); ++c) {
        for (const HelpHeading& h : chapters_[c].headings) {
            if (h.title.contains(topic, Qt::CaseInsensitive)) return {c, h.level == 3 ? h.title : QString()};
        }
    }
    return {};
}

HelpLocation HelpDocument::findAnchor(const QString& anchor) const
{
    QString slug = anchor;
    if (slug.startsWith(QLatin1Char('#'))) slug.remove(0, 1);
    for (int c = 0; c < chapters_.size(); ++c) {
        for (const HelpHeading& h : chapters_[c].headings) {
            if (h.slug == slug) return {c, h.level == 3 ? h.title : QString()};
        }
    }
    return {};
}

QList<int> HelpDocument::searchChapters(const QString& text) const
{
    QList<int> found;
    if (text.trimmed().isEmpty()) return found;
    for (int c = 0; c < chapters_.size(); ++c) {
        if (chapters_[c].markdown.contains(text.trimmed(), Qt::CaseInsensitive)) found << c;
    }
    return found;
}

}  // namespace ld::core
