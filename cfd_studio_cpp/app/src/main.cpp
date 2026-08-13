#include <QApplication>
#include <QIcon>
#include <QSurfaceFormat>

#include "main_window.hpp"
#include "preview_types.hpp"

int main(int argc, char** argv) {
    qRegisterMetaType<Preview2DSnapshot>("Preview2DSnapshot");
    qRegisterMetaType<Preview3DSnapshot>("Preview3DSnapshot");

    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    app.setApplicationName("CFD Studio");
    app.setWindowIcon(QIcon(":/app_icon.ico"));

    MainWindow window;
    window.show();

    return app.exec();
}
