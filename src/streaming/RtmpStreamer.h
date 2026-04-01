#pragma once

#include <QObject>
#include <QImage>
#include <QString>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

#if defined(HAS_FFMPEG)
struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct AVStream;
struct SwsContext;
#endif

class RtmpStreamer : public QObject
{
    Q_OBJECT

public:
    struct Config {
        QString url;
        int width = 1280;
        int height = 720;
        int fps = 25;
        int bitrateKbps = 2500;
        int reconnectMaxRetries = 5;
        int reconnectBaseDelayMs = 1000;
    };

    explicit RtmpStreamer(QObject* parent = nullptr);
    ~RtmpStreamer() override;

    bool start(const Config& config);
    void stop();
    void pushFrame(const QImage& image);

    bool isRunning() const;

signals:
    void started();
    void stopped();
    void errorOccurred(const QString& message);
    void infoMessage(const QString& message);
    void statsUpdated(quint64 inputFrames,
                      quint64 encodedPackets,
                      quint64 droppedFrames,
                      quint64 reconnectCount,
                      quint64 failedWrites);

private:
    static constexpr std::size_t kMaxQueueSize = 6;

    void workerLoop();
    bool reconnectOutput();
    void emitStatsIfNeeded(bool force = false);

#if defined(HAS_FFMPEG)
    bool initOutput(const Config& config);
    void cleanupOutput();
    bool encodeAndWriteImage(const QImage& image);
    bool flushEncoder();
    QString ffmpegError(int code) const;

    AVFormatContext* m_outputCtx = nullptr;
    AVCodecContext* m_codecCtx = nullptr;
    AVFrame* m_frame = nullptr;
    AVPacket* m_packet = nullptr;
    AVStream* m_videoStream = nullptr;
    SwsContext* m_swsCtx = nullptr;
    int64_t m_frameIndex = 0;
#endif

    Config m_config;
    std::atomic<bool> m_running { false };
    std::atomic<bool> m_stopping { false };
    std::atomic<quint64> m_inputFrames { 0 };
    std::atomic<quint64> m_encodedPackets { 0 };
    std::atomic<quint64> m_droppedFrames { 0 };
    std::atomic<quint64> m_reconnectCount { 0 };
    std::atomic<quint64> m_failedWrites { 0 };
    std::chrono::steady_clock::time_point m_lastStatsEmit;
    std::chrono::steady_clock::time_point m_lastQueueDropLogEmit;
    std::thread m_worker;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<QImage> m_frameQueue;
};
