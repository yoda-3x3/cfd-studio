#include <QApplication>
#include <QIcon>
#include <QSurfaceFormat>
#include <QTimer>

#include "main_window.hpp"
#include "preview_types.hpp"
#include "splash_screen.hpp"

int main(int argc, char** argv) {
    qRegisterMetaType<Preview2DSnapshot>("Preview2DSnapshot");
    qRegisterMetaType<Preview3DSnapshot>("Preview3DSnapshot");

    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    app.setApplicationName("Venturi CFD");
    app.setWindowIcon(QIcon(":/app_icon.ico"));

    // Unlike ui/splash.py, there's no multi-second import cost to cover
    // here (no numpy/scipy/matplotlib/numba/trimesh chain) -- just show
    // the splash for a short fixed branding moment, then swap to the
    // real window.
    CowSplashScreen splash;
    splash.show();

    MainWindow window;

    QTimer::singleShot(1500, [&]() {
        splash.close();
        window.show();
    });

    return app.exec();
}
