#pragma once

#include <QObject>
#include <QCameraDevice>
#include <QImage>
#include <QList>
#include <QStringList>

#include "streaming/RtmpStreamer.h"

class QCamera;
class QMediaCaptureSession;
class QVideoFrame;
class QVideoSink;

class StreamSession : public QObject
{
    Q_OBJECT

public:
    explicit StreamSession(QObject* parent = nullptr);
    ~StreamSession() override;

    void refreshCameras();
    QStringList cameraDescriptions() const;
    bool selectCamera(int index);

    bool startStreaming(const RtmpStreamer::Config& config);
    void stopStreaming();
    bool isStreaming() const;

signals:
    void cameraListChanged(const QStringList& cameras);
    void previewFrameReady(const QImage& frame);
    void logMessage(const QString& message);
    void streamingStateChanged(bool running);
    void statsUpdated(quint64 inputFrames,
                      quint64 encodedPackets,
                      quint64 droppedFrames,
                      quint64 reconnectCount,
                      quint64 failedWrites);

private:
    void onVideoFrameChanged(const QVideoFrame& frame);

    QList<QCameraDevice> m_cameraDevices;
    int m_selectedCameraIndex = -1;

    QCamera* m_camera = nullptr;
    QMediaCaptureSession* m_captureSession = nullptr;
    QVideoSink* m_videoSink = nullptr;
    RtmpStreamer* m_streamer = nullptr;
};
