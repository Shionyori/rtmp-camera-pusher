#pragma once

#include <QMainWindow>
#include <QCameraDevice>
#include <QList>

class QCamera;
class QComboBox;
class QLineEdit;
class QPushButton;
class QTextEdit;
class QMediaCaptureSession;
class QVideoWidget;
class RtmpStreamer;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void refreshCameraDevices();
    void onCameraSelectionChanged(int index);
    void onStartClicked();
    void onStopClicked();
    void appendLog(const QString& message);

private:
    void buildUi();
    void setupConnections();
    void startPreviewForIndex(int index);
    void setStreamingControls(bool running);

    QComboBox* m_cameraCombo = nullptr;
    QLineEdit* m_rtmpUrlEdit = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QTextEdit* m_logView = nullptr;
    QVideoWidget* m_videoWidget = nullptr;

    QList<QCameraDevice> m_cameraDevices;
    QCamera* m_camera = nullptr;
    QMediaCaptureSession* m_captureSession = nullptr;
    RtmpStreamer* m_streamer = nullptr;
};