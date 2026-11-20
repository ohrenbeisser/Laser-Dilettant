#include "core/gcodereader.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <numbers>

namespace ld::core {

void GcodeReader::addArc(double endX, double endY, double i, double j, double r, bool hasR, bool clockwise)
{
    const double startX = x_, startY = y_;
    double centerOffsetX = i, centerOffsetY = j;
    if (hasR) {
        // Wie GRBL: Der Mittelpunkt folgt aus dem Radius (negativ = der große Bogen).
        const double dx = endX - startX, dy = endY - startY;
        const double d2 = dx * dx + dy * dy;
        double radius = r;
        double h = 4.0 * radius * radius - d2;
        if (d2 <= 0.0 || h < 0.0) {
            // Der Radius reicht nicht bis zum Ziel: wie eine Gerade behandeln
            centerOffsetX = dx / 2.0;
            centerOffsetY = dy / 2.0;
        } else {
            double hDivD = -std::sqrt(h) / std::sqrt(d2);
            if (!clockwise) hDivD = -hDivD;
            if (radius < 0.0) {
                hDivD = -hDivD;
                radius = -radius;
            }
            centerOffsetX = 0.5 * (dx - dy * hDivD);
            centerOffsetY = 0.5 * (dy + dx * hDivD);
        }
    }
    const double cx = startX + centerOffsetX, cy = startY + centerOffsetY;
    const double radius = std::hypot(centerOffsetX, centerOffsetY);
    double a0 = std::atan2(startY - cy, startX - cx);
    double a1 = std::atan2(endY - cy, endX - cx);
    double sweep = a1 - a0;
    const double twoPi = 2.0 * std::numbers::pi;
    if (clockwise) {
        if (sweep >= -1e-12) sweep -= twoPi;   // Uhrzeigersinn: der Winkel nimmt ab (voller Kreis, wenn Start = Ziel)
    } else {
        if (sweep <= 1e-12) sweep += twoPi;
    }
    // Zahl der Strecken aus der erlaubten Abweichung (Pfeilhöhe)
    double step = twoPi / 64.0;
    if (radius > kArcToleranceMm) step = 2.0 * std::acos(std::clamp(1.0 - kArcToleranceMm / radius, -1.0, 1.0));
    int segments = std::clamp(static_cast<int>(std::ceil(std::abs(sweep) / std::max(step, 1e-3))), 1, 4000);
    double px = startX, py = startY;
    for (int k = 1; k <= segments; ++k) {
        const double angle = a0 + sweep * static_cast<double>(k) / segments;
        const double nx = k == segments ? endX : cx + radius * std::cos(angle);
        const double ny = k == segments ? endY : cy + radius * std::sin(angle);
        GcodeMove move;
        move.from = QPointF(px, py);
        move.to = QPointF(nx, ny);
        move.rapid = false;
        move.power = laserOn_ ? s_ : 0;
        move.feedMmPerMin = feed_;
        if (nx != px || ny != py) pending_.push_back(move);
        px = nx;
        py = ny;
    }
    x_ = endX;
    y_ = endY;
}

bool GcodeReader::next(GcodeMove& move)
{
    if (!pending_.empty()) {
        move = pending_.front();
        pending_.pop_front();
        return true;
    }
    while (!device_->atEnd()) {
        line_ = device_->readLine(1024);
        const char* p = line_.constData();
        const char* end = p + line_.size();

        double newX = x_, newY = y_;
        double wordX = 0.0, wordY = 0.0, wordI = 0.0, wordJ = 0.0, wordR = 0.0;
        bool hasX = false, hasY = false, hasR = false;
        bool hasMotionWord = false;
        int newMotion = motion_;
        bool dwell = false;

        while (p < end) {
            const char c = *p;
            if (c == ';') {
                break;                      // Rest der Zeile ist Kommentar
            }
            if (c == '(') {                 // Kommentar in Klammern überspringen
                while (p < end && *p != ')') {
                    ++p;
                }
                ++p;
                continue;
            }
            if (!std::isalpha(static_cast<unsigned char>(c))) {
                ++p;
                continue;
            }
            const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            ++p;
            double value = 0.0;
            const auto result = std::from_chars(p, end, value);
            if (result.ec != std::errc()) {
                continue;                   // Buchstabe ohne Zahl: ignorieren
            }
            p = result.ptr;
            switch (letter) {
            case 'G':
                if (value == 0.0) { newMotion = 0; hasMotionWord = true; }
                else if (value == 1.0) { newMotion = 1; hasMotionWord = true; }
                else if (value == 2.0) { newMotion = 2; hasMotionWord = true; }
                else if (value == 3.0) { newMotion = 3; hasMotionWord = true; }
                else if (value == 4.0) { dwell = true; }
                else if (value == 20.0) { unit_ = 25.4; }
                else if (value == 21.0) { unit_ = 1.0; }
                else if (value == 90.0) { relative_ = false; }
                else if (value == 91.0) { relative_ = true; }
                break;
            case 'X': wordX = value * unit_; hasX = true; break;
            case 'Y': wordY = value * unit_; hasY = true; break;
            case 'I': wordI = value * unit_; break;
            case 'J': wordJ = value * unit_; break;
            case 'R': wordR = value * unit_; hasR = true; break;
            case 'S': s_ = static_cast<int>(value); break;
            case 'F': feed_ = value * unit_; break;
            case 'M':
                if (value == 3.0 || value == 4.0) laserOn_ = true;
                else if (value == 5.0) laserOn_ = false;
                break;
            default: break;
            }
        }

        motion_ = newMotion;
        if (dwell) {
            continue;   // G4: keine Fahrt (das P/X des G4 ist kein Ziel)
        }
        const bool hasAxis = hasX || hasY;
        if (!hasMotionWord && !hasAxis) {
            continue;   // keine Fahrt
        }
        if (hasX) newX = relative_ ? x_ + wordX : wordX;
        if (hasY) newY = relative_ ? y_ + wordY : wordY;

        if (motion_ >= 2) {
            // Bogen im Uhrzeigersinn (G2) oder dagegen (G3); ohne Achsenwort bleibt das Ziel der Start (voller Kreis).
            addArc(newX, newY, wordI, wordJ, wordR, hasR, motion_ == 2);
            if (!pending_.empty()) {
                move = pending_.front();
                pending_.pop_front();
                return true;
            }
            continue;
        }
        if (newX == x_ && newY == y_) {
            continue;   // Fahrt ohne Ortsänderung
        }
        move.from = QPointF(x_, y_);
        move.to = QPointF(newX, newY);
        move.rapid = motion_ == 0;
        move.power = laserOn_ ? s_ : 0;
        move.feedMmPerMin = feed_;
        x_ = newX;
        y_ = newY;
        return true;
    }
    return false;
}

}  // namespace ld::core
