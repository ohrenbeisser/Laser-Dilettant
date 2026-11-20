#include "grbl/baudtester.h"

#include <QCoreApplication>

namespace ld::grbl {

BaudTester::BaudTester(PortFactory factory, QList<int> bauds, QObject* parent, int perBaudMs)
    : QObject(parent), factory_(std::move(factory)), bauds_(std::move(bauds)), perBaudMs_(perBaudMs)
{
    pingTimer_.setInterval(400);
    timeout_.setSingleShot(true);
    connect(&pingTimer_, &QTimer::timeout, this, [this] {
        if (port_ && port_->isOpen()) port_->write("?");
    });
    connect(&timeout_, &QTimer::timeout, this, &BaudTester::next);
}

BaudTester::~BaudTester()
{
    stopCurrent();
}

QList<int> BaudTester::commonBauds()
{
    return {115200, 250000, 57600, 38400, 19200, 9600, 230400, 460800, 921600};
}

void BaudTester::start()
{
    if (running_) return;
    running_ = true;
    index_ = -1;
    next();
}

void BaudTester::cancel()
{
    if (!running_) return;
    running_ = false;
    stopCurrent();
}

void BaudTester::stopCurrent()
{
    pingTimer_.stop();
    timeout_.stop();
    if (port_) {
        if (port_->isOpen()) port_->close();
        port_->deleteLater();
        port_ = nullptr;
    }
    buffer_.clear();
}

void BaudTester::next()
{
    stopCurrent();
    if (!running_) return;
    if (++index_ >= bauds_.size()) {
        running_ = false;
        emit finished(0);
        return;
    }
    const int baud = bauds_[index_];
    emit testing(baud);
    port_ = factory_(baud);
    if (!port_) {
        running_ = false;
        emit failed(QCoreApplication::translate("ld::grbl", "Der Anschluss konnte nicht erzeugt werden."));
        return;
    }
    port_->setParent(this);
    connect(port_, &IPort::readyRead, this, &BaudTester::onReadyRead);
    QString error;
    if (!port_->open(&error)) {
        running_ = false;
        stopCurrent();
        emit failed(error);
        return;
    }
    pingTimer_.start();
    timeout_.start(perBaudMs_);
}

void BaudTester::onReadyRead()
{
    buffer_ += port_->readAll();
    // Eine gültige Antwort: Begrüßung "Grbl ..." oder eine vollständige Statuszeile "<...>".
    const bool answered = buffer_.contains("Grbl ") || (buffer_.contains('<') && buffer_.contains('>'));
    if (!answered) return;
    const int baud = bauds_[index_];
    running_ = false;
    stopCurrent();
    emit finished(baud);
}

}  // namespace ld::grbl
