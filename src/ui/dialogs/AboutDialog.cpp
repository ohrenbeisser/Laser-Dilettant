#include "ui/dialogs/AboutDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QTextBrowser>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About Laser-Dilettant"));
    resize(600, 560);
    auto* text = new QTextBrowser(this);
    text->setOpenExternalLinks(true);
    text->setHtml(
        tr("<h2>Laser-Dilettant %1</h2>"
           "<p>Control software and G-code generator for GRBL laser engravers.</p>"
           "<p>© 2026 Chris Seifert (Ohrenbeisser)<br>"
           "<a href='https://github.com/ohrenbeisser/Laser-Dilettant'>github.com/ohrenbeisser/Laser-Dilettant</a></p>"
           "<p>This program is free software: you can redistribute it and/or modify it under the terms of the "
           "GNU General Public License as published by the Free Software Foundation, either version 3 of the "
           "License, or (at your option) any later version. It comes WITHOUT ANY WARRANTY.</p>"
           "<h3>Safety</h3>"
           "<ul><li>Wear safety glasses that match the wavelength of your laser.</li>"
           "<li>Never leave the laser unattended. Keep a fire extinguisher at hand.</li>"
           "<li>Extract the smoke.</li>"
           "<li>The emergency stop of this program does not replace a hardware emergency stop. If the PC hangs "
           "or the USB connection breaks, it has no effect. Use a switch that cuts the power of the laser module.</li></ul>"
           "<h3>Third-party components</h3>"
           "<ul><li>Qt %2 – LGPL-3.0</li>"
           "<li>GRBL 1.1h firmware and GRBL message texts – GPL-3.0, github.com/gnea/grbl</li>"
           "<li>avrdude (external program) – GPL-2.0</li>"
           "<li>Later stages: Potrace (GPL-2.0-or-later), nanosvg (zlib), dxflib (GPL-2.0-or-later), Hershey fonts</li></ul>"
           "<p>The license texts are in the folder LICENSES.</p>")
            .arg(QStringLiteral(LD_VERSION), QString::fromLatin1(qVersion())));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* l = new QVBoxLayout(this);
    l->addWidget(text);
    l->addWidget(buttons);
}
