#pragma once

#include "device/GrblTypes.h"

#include <QWidget>

class QLabel;

// Bottom line: state, position, feed, power, overrides, firmware, messages.
class StatusLine : public QWidget
{
    Q_OBJECT
public:
    explicit StatusLine(QWidget* parent = nullptr);

    void setStatus(const MachineStatus& status, double maxS);
    void setFirmware(const QString& version);
    // Shows a message; errors stay until the next message, others time out.
    void showMessage(const QString& text, bool error = false);

private:
    QLabel* m_state;
    QLabel* m_x;
    QLabel* m_y;
    QLabel* m_feed;
    QLabel* m_power;
    QLabel* m_ov;
    QLabel* m_firmware;
    QLabel* m_message;
    int m_messageSerial = 0;
};
