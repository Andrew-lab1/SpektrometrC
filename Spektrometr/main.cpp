#include "Spektrometr.h"
#include <QtWidgets/QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/Spektrometr/icon.ico")));

    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    {
        QPalette pal;
        pal.setColor(QPalette::Window, QColor(18, 18, 20));
        pal.setColor(QPalette::WindowText, QColor(230, 230, 230));
        pal.setColor(QPalette::Base, QColor(12, 12, 14));
        pal.setColor(QPalette::AlternateBase, QColor(18, 18, 20));
        pal.setColor(QPalette::ToolTipBase, QColor(230, 230, 230));
        pal.setColor(QPalette::ToolTipText, QColor(20, 20, 20));
        pal.setColor(QPalette::Text, QColor(230, 230, 230));
        pal.setColor(QPalette::Button, QColor(28, 28, 32));
        pal.setColor(QPalette::ButtonText, QColor(230, 230, 230));
        pal.setColor(QPalette::BrightText, Qt::red);
        pal.setColor(QPalette::Link, QColor(110, 170, 255));
        pal.setColor(QPalette::Highlight, QColor(60, 120, 220));
        pal.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
        pal.setColor(QPalette::Disabled, QPalette::Text, QColor(140, 140, 140));
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(140, 140, 140));
        app.setPalette(pal);
    }

    Spektrometr window;
    window.setWindowIcon(QIcon(QStringLiteral(":/Spektrometr/icon.ico")));
    window.resize(1400, 900);
    window.showMaximized();
    return app.exec();
}
