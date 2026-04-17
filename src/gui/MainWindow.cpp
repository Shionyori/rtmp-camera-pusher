#include "gui/MainWindow.h"

#include "common/AppLocale.h"
#include "core/StreamSession.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    resize(1080, 720);

    buildUi();
    m_session = new StreamSession(this);

    setupConnections();
    applyLanguage();
    refreshCameraDevices();
    setStreamingControls(false);
    appendLog("Application started. Select a camera and start streaming.");
}

MainWindow::~MainWindow()
{
    if (m_session != nullptr && m_session->isStreaming()) {
        m_session->stopStreaming();
    }
}

void MainWindow::refreshCameraDevices()
{
    m_session->refreshCameras();
}

void MainWindow::onCameraListChanged(const QStringList& cameras)
{
    m_cameraCombo->clear();
    m_cameraCombo->addItems(cameras);

    if (cameras.isEmpty()) {
        appendLog("No available camera devices were detected.");
        return;
    }

    if (m_cameraCombo->currentIndex() < 0) {
        m_cameraCombo->setCurrentIndex(0);
    }
}

void MainWindow::onPreviewFrameReady(const QImage& frame)
{
    if (frame.isNull()) {
        return;
    }

    QPixmap pixmap = QPixmap::fromImage(frame);
    m_previewLabel->setPixmap(
        pixmap.scaled(m_previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::onCameraSelectionChanged(int index)
{
    m_session->selectCamera(index);
}

void MainWindow::onLanguageChanged(int index)
{
    m_language = AppLocale::fromUiIndex(index);
    AppLocale::apply(m_language);
    applyLanguage();
}

void MainWindow::onStartClicked()
{
    RtmpStreamer::Config config;
    config.url = m_rtmpUrlEdit->text().trimmed();
    config.width = 1280;
    config.height = 720;
    config.fps = 25;
    config.bitrateKbps = 2500;

    if (m_session->startStreaming(config)) {
        setStreamingControls(true);
    }
}

void MainWindow::onStopClicked()
{
    m_session->stopStreaming();
    setStreamingControls(false);
}

void MainWindow::appendLog(const QString& message)
{
    m_logView->append(AppLocale::localizeLogMessage(message, m_language));
}

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    auto* topBar = new QHBoxLayout();
    m_cameraLabel = new QLabel(central);
    topBar->addWidget(m_cameraLabel);

    m_cameraCombo = new QComboBox(central);
    topBar->addWidget(m_cameraCombo, 1);

    m_languageLabel = new QLabel(central);
    topBar->addWidget(m_languageLabel);

    m_languageCombo = new QComboBox(central);
    m_languageCombo->addItem("English");
    m_languageCombo->addItem("中文");
    m_languageCombo->setCurrentIndex(AppLocale::toUiIndex(AppLocale::Language::English));
    topBar->addWidget(m_languageCombo);

    m_urlLabel = new QLabel(central);
    topBar->addWidget(m_urlLabel);
    m_rtmpUrlEdit = new QLineEdit(central);
    topBar->addWidget(m_rtmpUrlEdit, 2);

    m_startButton = new QPushButton(central);
    m_stopButton = new QPushButton(central);
    topBar->addWidget(m_startButton);
    topBar->addWidget(m_stopButton);

    rootLayout->addLayout(topBar);

    m_previewLabel = new QLabel(central);
    m_previewLabel->setMinimumHeight(360);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet("background: #111; color: #ddd;");
    rootLayout->addWidget(m_previewLabel, 1);

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
    connect(m_languageCombo, &QComboBox::currentIndexChanged,
        this, &MainWindow::onLanguageChanged);
    connect(m_startButton, &QPushButton::clicked,
            this, &MainWindow::onStartClicked);
    connect(m_stopButton, &QPushButton::clicked,
            this, &MainWindow::onStopClicked);

    connect(m_session, &StreamSession::cameraListChanged,
            this, &MainWindow::onCameraListChanged);
    connect(m_session, &StreamSession::previewFrameReady,
            this, &MainWindow::onPreviewFrameReady);
    connect(m_session, &StreamSession::logMessage,
            this, &MainWindow::appendLog);
    connect(m_session, &StreamSession::streamingStateChanged,
            this, &MainWindow::setStreamingControls);
}

void MainWindow::startPreviewForIndex(int index)
{
    m_session->selectCamera(index);
}

void MainWindow::setStreamingControls(bool running)
{
    m_startButton->setEnabled(!running);
    m_stopButton->setEnabled(running);
    m_cameraCombo->setEnabled(!running);
}

void MainWindow::applyLanguage()
{
    const bool zh = (m_language == AppLocale::Language::Chinese);

    setWindowTitle(zh ? "RTMP 摄像头推流器" : "RTMP Camera Pusher");
    m_cameraLabel->setText(zh ? "摄像头:" : "Camera:");
    m_languageLabel->setText(zh ? "语言:" : "Language:");
    m_urlLabel->setText("RTMP URL:");
    m_rtmpUrlEdit->setPlaceholderText("rtmp://127.0.0.1/live/stream");
    m_startButton->setText(zh ? "开始推流" : "Start");
    m_stopButton->setText(zh ? "停止推流" : "Stop");
    m_previewLabel->setText(zh ? "等待摄像头画面..." : "Waiting for camera preview...");
}
