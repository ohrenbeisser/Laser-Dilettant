#pragma once

#include <QWidget>

class NumberSpinBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QToolButton;

// Laser values and job control (section 4.4). The values are central for
// all generators; jobs follow in stage 2.
class JobBar : public QWidget
{
    Q_OBJECT
public:
    explicit JobBar(QWidget* parent = nullptr);

    double powerPercent() const;
    double speedMmPerSec() const;
    int passes() const;
    void setMaxSpeed(double mmPerSec);

    // Enables the job buttons. Not available in stage 1.
    void setJobControlsAvailable(bool available);
    void setProgress(qint64 done, qint64 total);
    void setTimes(double elapsedS, double totalS);

signals:
    void valuesChanged();
    void frameClicked();
    void startClicked();
    void pauseClicked();
    void stopClicked();

private:
    QComboBox* m_material;
    QToolButton* m_saveMaterial;
    NumberSpinBox* m_power;
    NumberSpinBox* m_speed;
    QSpinBox* m_passes;
    QPushButton* m_frame;
    QPushButton* m_start;
    QPushButton* m_pause;
    QPushButton* m_stop;
    QProgressBar* m_progress;
    QLabel* m_time;
};
