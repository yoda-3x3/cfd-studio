import importlib
import os
import sys

os.environ.setdefault("QT_API", "pyside6")

from PySide6.QtCore import QObject, QThread, Signal, Slot
from PySide6.QtWidgets import QApplication, QMessageBox

if __package__ in (None, ""):
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    _PKG = "cfd_paraview_app"
else:
    _PKG = __package__


class _AppLoader(QObject):
    """Runs the heavy imports (numpy/scipy/matplotlib/numba/trimesh --
    several seconds of pure import time) on a background thread so the
    splash screen can show and animate immediately instead of the app
    appearing to hang at launch."""

    stage = Signal(str)
    ready = Signal(object)  # the MainWindow class
    failed = Signal(str)

    def run(self):
        try:
            self.stage.emit("Warming up the numerical libraries...")
            import numpy  # noqa: F401
            import scipy  # noqa: F401

            self.stage.emit("Loading the plotting toolkit...")
            import matplotlib  # noqa: F401
            from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg  # noqa: F401

            self.stage.emit("Loading the geometry engine...")
            import trimesh  # noqa: F401

            self.stage.emit("Loading the solver...")
            import numba  # noqa: F401

            self.stage.emit("Building the window...")
            main_window_mod = importlib.import_module(f"{_PKG}.ui.main_window")
            self.ready.emit(main_window_mod.MainWindow)
        except Exception as exc:  # noqa: BLE001
            self.failed.emit(str(exc))


class _Coordinator(QObject):
    """Receives the loader's signals. Being a QObject that's never moved
    off the main thread, Qt's auto-connection correctly detects the
    sender (the loader, on a background thread) and receiver (this, on
    the main thread) differ and queues delivery -- so these slots always
    run on the main/GUI thread, where it's actually safe to construct
    QWidgets. Connecting the loader's signals directly to plain
    functions instead of QObject slots would NOT get this thread-safe
    queuing (there'd be no receiver thread affinity for Qt to check
    against), and constructing a QMainWindow off the GUI thread hangs."""

    def __init__(self, splash, thread, app):
        super().__init__()
        self._splash = splash
        self._thread = thread
        self._app = app
        self.window = None

    @Slot(object)
    def on_ready(self, window_cls):
        self.window = window_cls()
        self.window.show()
        self._splash.close()
        self._thread.quit()

    @Slot(str)
    def on_failed(self, message):
        self._splash.close()
        QMessageBox.critical(None, "Failed to start CFD Studio", message)
        self._thread.quit()
        self._app.quit()


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("CFD Studio")

    splash_mod = importlib.import_module(f"{_PKG}.ui.splash")
    splash = splash_mod.CowSplashScreen()
    splash.show()

    thread = QThread()
    loader = _AppLoader()
    loader.moveToThread(thread)
    coordinator = _Coordinator(splash, thread, app)

    thread.started.connect(loader.run)
    loader.stage.connect(splash.set_status)
    loader.ready.connect(coordinator.on_ready)
    loader.failed.connect(coordinator.on_failed)
    thread.start()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
