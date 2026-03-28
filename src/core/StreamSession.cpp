#include "core/StreamSession.h"

#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVideoFrame>
#include <QVideoSink>

StreamSession::StreamSession(QObject* parent)
    : QObject(parent)
{
    m_captureSession = new QMediaCaptureSession(this);
    m_videoSink = new QVideoSink(this);
    m_streamer = new RtmpStreamer(this);

    m_captureSession->setVideoSink(m_videoSink);

    connect(m_videoSink, &QVideoSink::videoFrameChanged,
            this, &StreamSession::onVideoFrameChanged);

    connect(m_streamer, &RtmpStreamer::started, this, [this]() {
        emit streamingStateChanged(true);
        emit logMessage("推流启动成功。");
    });
    connect(m_streamer, &RtmpStreamer::stopped, this, [this]() {
        emit streamingStateChanged(false);
        emit logMessage("推流已停止。");
    });
    connect(m_streamer, &RtmpStreamer::infoMessage,
            this, &StreamSession::logMessage);
    connect(m_streamer, &RtmpStreamer::errorOccurred,
            this, &StreamSession::logMessage);

    refreshCameras();
}

StreamSession::~StreamSession()
{
    stopStreaming();

    if (m_camera != nullptr) {
        m_camera->stop();
    }
}

void StreamSession::refreshCameras()
{
    m_cameraDevices = QMediaDevices::videoInputs();

    QStringList names;
    for (const auto& camera : m_cameraDevices) {
        names << camera.description();
    }

    emit cameraListChanged(names);

    if (m_cameraDevices.isEmpty()) {
        m_selectedCameraIndex = -1;
        emit logMessage("未检测到可用摄像头设备。");
        return;
    }

    if (m_selectedCameraIndex < 0 || m_selectedCameraIndex >= m_cameraDevices.size()) {
        selectCamera(0);
    }
}

QStringList StreamSession::cameraDescriptions() const
{
    QStringList names;
    for (const auto& camera : m_cameraDevices) {
        names << camera.description();
    }
    return names;
}

bool StreamSession::selectCamera(int index)
{
    if (index < 0 || index >= m_cameraDevices.size()) {
        emit logMessage(QString("摄像头索引无效: %1").arg(index));
        return false;
    }

    if (m_camera != nullptr) {
        m_camera->stop();
        m_camera->deleteLater();
        m_camera = nullptr;
    }

    m_selectedCameraIndex = index;
    m_camera = new QCamera(m_cameraDevices.at(index), this);
    m_captureSession->setCamera(m_camera);
    m_camera->start();

    emit logMessage(QString("已切换摄像头: %1").arg(m_cameraDevices.at(index).description()));
    return true;
}

bool StreamSession::startStreaming(const RtmpStreamer::Config& config)
{
    if (m_cameraDevices.isEmpty()) {
        emit logMessage("没有可用摄像头，无法启动推流。");
        return false;
    }

    if (m_selectedCameraIndex < 0) {
        if (!selectCamera(0)) {
            return false;
        }
    }

    return m_streamer->start(config);
}

void StreamSession::stopStreaming()
{
    m_streamer->stop();
}

bool StreamSession::isStreaming() const
{
    return m_streamer->isRunning();
}

void StreamSession::onVideoFrameChanged(const QVideoFrame& frame)
{
    if (!frame.isValid()) {
        return;
    }

    QVideoFrame copy(frame);
    QImage image = copy.toImage();
    if (image.isNull()) {
        return;
    }

    emit previewFrameReady(image);

    if (m_streamer->isRunning()) {
        m_streamer->pushFrame(image);
    }
}
