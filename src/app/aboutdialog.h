// Über-Dialog mit drei Reitern: "Über" (Logo, Version, Autor, Links), "Haftung" (Sicherheitshinweise und
// Haftungsausschluss) und "Lizenzen" (eigene Lizenz, Drittbibliotheken, Volltext der GPL).
#pragma once

#include <QDialog>

namespace ld::app {

class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);

private:
    QWidget* createAboutTab();
    QWidget* createLiabilityTab();
    QWidget* createLicenseTab();
};

}  // namespace ld::app
