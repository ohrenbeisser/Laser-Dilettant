#include "core/ncfile.h"

#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <set>

namespace ld::core {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ld::core", text);
}

constexpr int kMaxCompactLength = 75;   // GRBL: Zeilenpuffer 80 Zeichen (ohne Leerzeichen und Kommentare)

struct Word {
    char letter;
    QByteArray text;   // die Zahl wie in der Datei
    double value;
};

// Erlaubte G- und M-Befehle. Alles andere wird nicht gesendet.
bool allowedG(int g)
{
    static const std::set<int> allowed = {0, 1, 2, 3, 4, 17, 20, 21, 40, 49, 54, 61, 80, 90, 91, 94};
    return allowed.count(g) > 0;
}

bool allowedM(int m)
{
    static const std::set<int> allowed = {2, 3, 4, 5, 7, 8, 9, 30};
    return allowed.count(m) > 0;
}

// Befehle, die Koordinaten oder den Nullpunkt verändern (damit der Nullpunkt, den der Bediener gesetzt hat, bleibt).
bool changesCoordinates(int g)
{
    return g == 10 || g == 28 || g == 30 || g == 53 || (g >= 55 && g <= 59) || g == 92;
}

QByteArray shorten(const QByteArray& line)
{
    QByteArray s = line.trimmed();
    if (s.size() > 60) s = s.left(57) + "...";
    return s;
}

}  // namespace

NcAnalysis sanitizeNcFile(const QString& src, const QString& dst, const MachineSetup& machine,
                          const std::atomic_bool* cancel, const std::function<void(double)>& progress)
{
    NcAnalysis result;
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) {
        result.error = tr("Die Datei kann nicht gelesen werden.");
        return result;
    }
    QSaveFile out(dst);
    if (!out.open(QIODevice::WriteOnly)) {
        result.error = tr("Die bereinigte Kopie kann nicht geschrieben werden.");
        return result;
    }
    const qint64 total = std::max<qint64>(1, in.size());
    const int maxS = std::max(1, machine.maxPower);

    auto addIssue = [&](long long lineNo, const QString& text, const QByteArray& content, bool blocked) {
        if (blocked) ++result.blockedLines; else ++result.clampedLines;
        if (result.issues.size() < kMaxNcIssues) {
            result.issues.push_back({lineNo, text, QString::fromLatin1(shorten(content)), blocked});
        } else {
            ++result.moreIssues;
        }
    };

    QByteArray line;
    long long lineNo = 0;
    while (!in.atEnd()) {
        if (cancel && (lineNo & 0x3FFF) == 0 && cancel->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            return result;
        }
        if (progress && (lineNo & 0x3FFF) == 0) progress(static_cast<double>(in.pos()) / static_cast<double>(total));
        line = in.readLine(2048);
        ++lineNo;
        bool tooLong = false;
        if (!line.endsWith('\n') && !in.atEnd()) {
            tooLong = true;
            while (!in.atEnd()) {   // den Rest der überlangen Zeile verwerfen
                const QByteArray rest = in.readLine(4096);
                if (rest.endsWith('\n')) break;
            }
        }
        result.lines = lineNo;

        // 1) Kommentare entfernen (Klammern und ab Semikolon), in Wörter zerlegen
        QByteArray text = line;
        bool inComment = false;
        QByteArray code;
        for (const char c : text) {
            if (inComment) { if (c == ')') inComment = false; continue; }
            if (c == '(') { inComment = true; continue; }
            if (c == ';') break;
            if (c == '\r' || c == '\n') continue;
            code.append(c);
        }
        code = code.trimmed();
        if (code.isEmpty() || code == "%") {
            continue;   // leer oder nur Kommentar: nichts zu senden
        }
        if (tooLong) {
            addIssue(lineNo, tr("Zeile ist viel zu lang."), line, true);
            continue;
        }
        if (code.startsWith('$')) {
            addIssue(lineNo, tr("Einstellungsbefehl: wird nicht gesendet."), code, true);
            continue;
        }

        std::vector<Word> words;
        bool malformed = false;
        const char* p = code.constData();
        const char* end = p + code.size();
        while (p < end) {
            if (std::isspace(static_cast<unsigned char>(*p))) { ++p; continue; }
            if (!std::isalpha(static_cast<unsigned char>(*p))) { malformed = true; break; }
            const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
            ++p;
            while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
            const char* numStart = p;
            double value = 0.0;
            const auto parsed = std::from_chars(p, end, value);
            if (parsed.ec != std::errc()) { malformed = true; break; }
            p = parsed.ptr;
            words.push_back({letter, QByteArray(numStart, static_cast<int>(p - numStart)), value});
        }
        if (malformed || words.empty()) {
            addIssue(lineNo, tr("Zeile nicht lesbar: wird nicht gesendet."), code, true);
            continue;
        }

        // 2) Befehle prüfen
        QString blockReason;
        for (const Word& w : words) {
            if (w.letter == 'G') {
                const int g = static_cast<int>(std::lround(w.value * 10.0) / 10);   // G92.1 -> 92
                if (changesCoordinates(g)) {
                    blockReason = tr("G%1 verschiebt Koordinaten oder den Nullpunkt: wird nicht gesendet.").arg(g);
                } else if (!allowedG(g)) {
                    blockReason = tr("G%1 ist unbekannt oder wird nicht unterstützt: wird nicht gesendet.").arg(g);
                } else if (std::abs(w.value - std::round(w.value)) > 1e-9) {
                    blockReason = tr("G%1 wird nicht unterstützt: wird nicht gesendet.").arg(w.value);
                }
            } else if (w.letter == 'M') {
                if (!allowedM(static_cast<int>(std::lround(w.value)))) {
                    blockReason = tr("M%1 wird nicht unterstützt: wird nicht gesendet.").arg(static_cast<int>(w.value));
                }
            } else if (!QByteArray("XYZIJKRFSPNL").contains(w.letter)) {
                blockReason = tr("Unbekanntes Wort \"%1\": wird nicht gesendet.").arg(QChar::fromLatin1(w.letter));
            }
            if (!blockReason.isEmpty()) break;
        }
        if (blockReason.isEmpty()) {
            // Ebenen außer G17 lassen sich nicht anzeigen; Bögen darin wären falsch.
            for (const Word& w : words) {
                if (w.letter == 'G' && (w.value == 18.0 || w.value == 19.0)) blockReason = tr("Ebene G18/G19 wird nicht unterstützt.");
            }
        }
        if (!blockReason.isEmpty()) {
            addIssue(lineNo, blockReason, code, true);
            continue;
        }

        // 3) S begrenzen, Zeile neu zusammensetzen (ohne Leerzeichen zwischen Buchstabe und Zahl)
        bool clamped = false;
        QByteArray compact;
        for (const Word& w : words) {
            QByteArray number = w.text;
            if (w.letter == 'S') {
                result.maxS = std::max(result.maxS, static_cast<int>(std::lround(w.value)));
                if (w.value > maxS) {
                    number = QByteArray::number(maxS);
                    clamped = true;
                } else if (w.value < 0.0) {
                    number = "0";
                    clamped = true;
                }
            }
            if (!compact.isEmpty()) compact += ' ';
            compact += w.letter;
            compact += number;
        }
        if (compact.size() - std::count(compact.begin(), compact.end(), ' ') > kMaxCompactLength) {
            addIssue(lineNo, tr("Zeile ist zu lang für GRBL: wird nicht gesendet."), code, true);
            continue;
        }
        if (clamped) addIssue(lineNo, tr("S war über dem Maximum der Maschine (%1): auf das Maximum begrenzt.").arg(maxS), code, false);
        out.write(compact + '\n');
        ++result.sentLines;
    }
    if (!out.commit()) {
        result.error = tr("Die bereinigte Kopie kann nicht geschrieben werden.");
        return result;
    }
    if (progress) progress(1.0);
    result.ok = true;
    return result;
}

}  // namespace ld::core
