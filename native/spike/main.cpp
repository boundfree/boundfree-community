#include <QApplication>
#include <QMainWindow>

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QMainWindow win;
    win.resize(1280, 800);
    win.setWindowTitle("BoundFree Spike");
    win.show();

    return app.exec();
}
