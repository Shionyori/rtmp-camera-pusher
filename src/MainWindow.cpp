#include "MainWindow.h"

#include "streaming/RtmpStreamer.h"

#include <QCamera>
#include <QCameraDevice>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>
#include <QVideoWidget>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Qt RTMP Camera Pusher");
    resize(1080, 720);

    buildUi();

    m_captureSession = new QMediaCaptureSession(this);
    m_streamer = new RtmpStreamer(this);

    setupConnections();
    refreshCameraDevices();
    setStreamingControls(false);
    appendLog("应用已启动，等待选择摄像头并开始推流。");
}

MainWindow::~MainWindow()
{
    if (m_streamer != nullptr && m_streamer->isRunning()) {
        m_streamer->stop();
    }
}

void MainWindow::refreshCameraDevices()
{
    m_cameraDevices = QMediaDevices::videoInputs();
    m_cameraCombo->clear();

    for (const auto& device : m_cameraDevices) {
        m_cameraCombo->addItem(device.description());
    }

    if (m_cameraDevices.isEmpty()) {
        appendLog("未检测到可用摄像头设备。");
        return;
    }

    startPreviewForIndex(0);
}

void MainWindow::onCameraSelectionChanged(int index)
{
    startPreviewForIndex(index);
}

void MainWindow::onStartClicked()
{
    RtmpStreamer::Config config;
    config.url = m_rtmpUrlEdit->text().trimmed();
    config.width = 1280;
    config.height = 720;
    config.fps = 25;
    config.bitrateKbps = 2500;

    if (m_streamer->start(config)) {
        setStreamingControls(true);
    }
}

void MainWindow::onStopClicked()
{
    m_streamer->stop();
    setStreamingControls(false);
}

void MainWindow::appendLog(const QString& message)
{
    m_logView->append(message);
}

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    auto* topBar = new QHBoxLayout();
    topBar->addWidget(new QLabel("摄像头:", central));

    m_cameraCombo = new QComboBox(central);
    topBar->addWidget(m_cameraCombo, 1);

    topBar->addWidget(new QLabel("RTMP URL:", central));
    m_rtmpUrlEdit = new QLineEdit(central);
    m_rtmpUrlEdit->setPlaceholderText("rtmp://127.0.0.1/live/stream");
    topBar->addWidget(m_rtmpUrlEdit, 2);

    m_startButton = new QPushButton("开始推流", central);
    m_stopButton = new QPushButton("停止推流", central);
    topBar->addWidget(m_startButton);
    topBar->addWidget(m_stopButton);

    rootLayout->addLayout(topBar);

    m_videoWidget = new QVideoWidget(central);
    m_videoWidget->setMinimumHeight(360);
    rootLayout->addWidget(m_videoWidget, 1);

    m_logView = new QTextEdit(central);
    m_logView->setReadOnly(true);
    m_logView->setMinimumHeight(180);
    rootLayout->addWidget(m_logView);

    setCentralWidget(central);
}

void MainWindow::setupConnections()
{
    connect(m_cameraCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onCameraSelectionChanged);
    connect(m_startButton, &QPushButton::clicked,
            this, &MainWindow::onStartClicked);
    connect(m_stopButton, &QPushButton::clicked,
            this, &MainWindow::onStopClicked);

    connect(m_streamer, &RtmpStreamer::started, this, [this]() {
        appendLog("推流启动成功。正在发送摄像头帧。");
    });
    connect(m_streamer, &RtmpStreamer::stopped, this, [this]() {
        appendLog("推流已停止。");
    });
    connect(m_streamer, &RtmpStreamer::errorOccurred,
            this, &MainWindow::appendLog);
    connect(m_streamer, &RtmpStreamer::infoMessage,
            this, &MainWindow::appendLog);

    QVideoSink* sink = m_videoWidget->videoSink();
    if (sink != nullptr) {
        connect(sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& frame) {
            if (!m_streamer->isRunning() || !frame.isValid()) {
                return;
            }

            QVideoFrame copy(frame);
            QImage image = copy.toImage();
            if (image.isNull()) {
                return;
            }

            m_streamer->pushFrame(image);
        });
    }
}

void MainWindow::startPreviewForIndex(int index)
{
    if (index < 0 || index >= m_cameraDevices.size()) {
        return;
    }

    if (m_camera != nullptr) {
        m_camera->stop();
        m_camera->deleteLater();
        m_camera = nullptr;
    }

    m_camera = new QCamera(m_cameraDevices.at(index), this);
    m_captureSession->setCamera(m_camera);
    m_captureSession->setVideoOutput(m_videoWidget);
    m_camera->start();

    appendLog(QString("已切换摄像头: %1").arg(m_cameraDevices.at(index).description()));
}

void MainWindow::setStreamingControls(bool running)
{
    m_startButton->setEnabled(!running);
    m_stopButton->setEnabled(running);
    m_cameraCombo->setEnabled(!running);
}