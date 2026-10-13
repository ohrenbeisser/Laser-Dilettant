#pragma once

#include "device/GrblDriver.h"
#include "device/SimulatedGrbl.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPointer>

// Creates a driver whose transport is a fast SimulatedGrbl.
struct SimRig
{
    GrblDriver driver;
    QPointer<SimulatedGrbl> sim;
    int baud = 0;
    double timeScale = 20.0;
    std::function<void(SimulatedGrbl*)> configure;

    SimRig()
    {
        driver.setTransportFactory([this](const ConnectionParams&) -> ITransport* {
            auto* s = new SimulatedGrbl;
            s->setBootDelayMs(30);
            s->setBaudRate(baud);
            s->setTimeScale(timeScale);
            if (configure)
                configure(s);
            sim = s;
            return s;
        });
        driver.setPollInterval(50);
    }

    bool connect(int timeoutMs = 5000)
    {
        ConnectionParams p;
        p.simulator = true;
        p.baudRate = baud;
        driver.connectDevice(p);
        QElapsedTimer t;
        t.start();
        while (!driver.isConnected() && t.elapsed() < timeoutMs)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        // wait for the first status report
        while (driver.status().state == MachineState::Connecting && t.elapsed() < timeoutMs)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        return driver.isConnected();
    }
};
