#pragma once

#include <QRectF>
#include <QString>
#include <QStringList>

// A ready-to-send G-code program: normalised lines plus metadata.
struct GcodeProgram
{
    QString name;
    QStringList lines;          // normalised lines (no comments, no blanks)
    QRectF boundingBox;         // mm, machine coordinates
    double estimatedSeconds = 0; // for one pass

    bool isEmpty() const { return lines.isEmpty(); }
};
