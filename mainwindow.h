#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QListWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void startProxy();
    void stopProxy();
    void addToBlacklist();
    void removeFromBlacklist();
    void logMessage(const QString &message);

private:
    Ui::MainWindow *ui;
    QTcpServer *proxyServer;
    QStringList blacklist;

    void handleClient(QTcpSocket *clientSocket);
    bool isBlacklisted(const QString &hostname);
};

#endif // MAINWINDOW_H
