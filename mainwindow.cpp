#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_actionSettings_triggered()
{
    // Создаем экземпляр диалога.
    // Передаем 'this' как родителя, чтобы диалог был поверх главного окна
    SettingsDialog dialog(this);

    // Запускаем диалог в модальном режиме (окно блокирует основное приложение)
    // Если нужно немодальное окно, используйте dialog.show();
    if (dialog.exec() == QDialog::Accepted) {
        // Сюда можно добавить код, если нужно обработать нажатие кнопки ОК
        // Например, считать данные из диалога и сохранить их
    }
}
