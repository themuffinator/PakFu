#include "main_window.h"

#include <QLabel>

MainWindow::MainWindow() {
  setWindowTitle("PakFu");
  auto* label = new QLabel("PakFu UI scaffold", this);
  label->setAlignment(Qt::AlignCenter);
  setCentralWidget(label);
  resize(1000, 700);
}
