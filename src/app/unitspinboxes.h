// Eingabefelder für Längen und Geschwindigkeiten, die in der gewählten Einheit anzeigen
// (mm oder Zoll, mm/min oder mm/s), aber intern immer in mm bzw. mm/min rechnen.
// Programmgesteuertes Setzen (setMm, setMmPerMin) meldet nichts; nur Eingaben des Bedieners
// lösen die Signale aus.
#pragma once

#include <QDoubleSpinBox>
#include <QValidator>

namespace ld::app {

class LengthSpinBox : public QDoubleSpinBox {
    Q_OBJECT

public:
    // Bereich und Schritt in mm; decimalsMm/decimalsInch: Nachkommastellen je Einheit.
    LengthSpinBox(double minMm, double maxMm, int decimalsMm, int decimalsInch, double stepMm,
                  QWidget* parent = nullptr);

    double mm() const { return mm_; }
    void setMm(double mm);   // meldet nichts

signals:
    void mmChanged(double mm);

private:
    void applyUnit();

    double minMm_, maxMm_, stepMm_;
    int decimalsMm_, decimalsInch_;
    double mm_ = 0.0;
};

// Auflösung eines Bildes: Intern die Pixelgröße in mm (wie im Dokument), angezeigt nach der Einstellung als
// Linien/mm (mit dpi in Klammern, Vorgabe), dpi oder Pixelgröße. Ein größerer Anzeigewert bei Linien/mm und dpi
// heißt feiner, bei der Pixelgröße gröber; Bereich und Schritte passen sich der Einheit an.
class ResolutionSpinBox : public QDoubleSpinBox {
    Q_OBJECT

public:
    ResolutionSpinBox(double minMm, double maxMm, QWidget* parent = nullptr);

    double mm() const { return mm_; }
    void setMm(double mm);   // meldet nichts

signals:
    void mmChanged(double mm);

protected:
    QString textFromValue(double value) const override;
    double valueFromText(const QString& text) const override;
    QValidator::State validate(QString& input, int& pos) const override;

private:
    void applyUnit();
    double toDisplay(double mm) const;
    double fromDisplay(double shown) const;

    double minMm_, maxMm_;
    double mm_ = 0.0;
};

class SpeedSpinBox : public QDoubleSpinBox {
    Q_OBJECT

public:
    SpeedSpinBox(double minMmPerMin, double maxMmPerMin, QWidget* parent = nullptr);

    double mmPerMin() const { return value_; }
    void setMmPerMin(double v);   // meldet nichts
    // Neue obere Grenze (zum Beispiel die maximale Speed des Maschinenprofils); der Wert wird nötigenfalls gekappt.
    void setMaxMmPerMin(double maxMmPerMin);

signals:
    void mmPerMinChanged(double v);

private:
    void applyUnit();

    double minV_, maxV_;
    double value_ = 0.0;
};

}  // namespace ld::app
