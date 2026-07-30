import os
import sys

os.environ.setdefault("QT_API", "pyside6")

from PySide6.QtWidgets import QApplication

if __package__ in (None, ""):
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from cfd_paraview_app.ui.main_window import MainWindow
else:
    from .ui.main_window import MainWindow


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("CFD Studio")
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
