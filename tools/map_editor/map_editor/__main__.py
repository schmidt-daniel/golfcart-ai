"""Entry point for the golf course map editor."""

import sys

from PySide6.QtWidgets import QApplication

from map_editor.main_window import MainWindow


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("Golf Course Map Editor")
    win = MainWindow()
    win.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())