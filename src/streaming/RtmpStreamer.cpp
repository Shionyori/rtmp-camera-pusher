#include "RtmpStreamer.h"

#include <QByteArray>

#include <algorithm>
#include <chrono>

#if defined(HAS_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}
#endif

RtmpStreamer::RtmpStreamer(QObject* parent)
    : QObject(parent)
{
    m_lastStatsEmit = std::chrono::steady_clock::now();
    m_lastQueueDropLogEmit = std::chrono::steady_clock::now() - std::chrono::seconds(10);
}

RtmpStreamer::~RtmpStreamer()
{
    stop();
}

bool RtmpStreamer::start(const Config& config)
{
    if (m_running.load()) {
        emit infoMessage("推流已在运行中。");
        return true;
    }

    if (config.url.trimmed().isEmpty()) {
        emit errorOccurred("RTMP 地址不能为空。");
        return false;
    }

#if !defined(HAS_FFMPEG)
    emit errorOccurred("未检测到 FFmpeg 开发库，无法启动推流");
    return false;
#else
    m_config = config;
    m_inputFrames.store(0);
    m_encodedPackets.store(0);
    m_droppedFrames.store(0);
    m_reconnectCount.store(0);
    m_failedWrites.store(0);
    m_lastStatsEmit = std::chrono::steady_clock::now();

    if (!initOutput(config)) {
        cleanupOutput();
        return false;
    }

    m_stopping.store(false);
    m_running.store(true);
    m_worker = std::thread(&RtmpStreamer::workerLoop, this);

    emit started();
    emitStatsIfNeeded(true);
    return true;
#endif
}

void RtmpStreamer::stop()
{
    if (!m_running.load()) {
        return;
    }

    m_stopping.store(true);
    m_queueCv.notify_all();

    if (m_worker.joinable()) {
        m_worker.join();
    }

#if defined(HAS_FFMPEG)
    cleanupOutput();
    avformat_network_deinit();
#endif

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_frameQueue.clear();
    }

    m_running.store(false);
    m_stopping.store(false);
    emitStatsIfNeeded(true);
    emit stopped();
}

void RtmpStreamer::pushFrame(const QImage& image)
{
    if (!m_running.load() || m_stopping.load() || image.isNull()) {
        return;
    }

    QImage converted = image.convertToFormat(QImage::Format_RGBA8888);
    if (converted.isNull()) {
        return;
    }

    m_inputFrames.fetch_add(1);

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_frameQueue.size() >= kMaxQueueSize) {
            m_frameQueue.pop_front();
            m_droppedFrames.fetch_add(1);
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastQueueDropLogEmit).count();
            if (elapsed >= 2000) {
                m_lastQueueDropLogEmit = now;
                emit infoMessage("帧队列持续积压，已丢弃旧帧以保持实时性。");
            }
        }
        m_frameQueue.push_back(std::move(converted));
    }

    m_queueCv.notify_one();
    emitStatsIfNeeded();
}

bool RtmpStreamer::isRunning() const
{
    return m_running.load();
}

void RtmpStreamer::workerLoop()
{
#if defined(HAS_FFMPEG)
    while (true) {
        QImage frame;

        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [this]() {
                return m_stopping.load() || !m_frameQueue.empty();
            });

            if (m_stopping.load() && m_frameQueue.empty()) {
                break;
            }

            frame = std::move(m_frameQueue.front());
            m_frameQueue.pop_front();
        }

        if (!encodeAndWriteImage(frame)) {
            if (!reconnectOutput()) {
                emit errorOccurred("编码或发送失败，重连多次后仍失败，推流即将停止。");
                m_stopping.store(true);
            }
        }

        emitStatsIfNeeded();
    }

    if (!flushEncoder()) {
        emit errorOccurred("编码器 flush 失败。");
    }

    if (m_outputCtx != nullptr) {
        int writeRet = av_write_trailer(m_outputCtx);
        if (writeRet < 0) {
            emit errorOccurred(QString("写入 trailer 失败: %1").arg(ffmpegError(writeRet)));
        }
    }
#endif
}

#if defined(HAS_FFMPEG)
bool RtmpStreamer::reconnectOutput()
{
    cleanupOutput();

    const int maxRetries = std::max(1, m_config.reconnectMaxRetries);
    const int baseDelayMs = std::max(100, m_config.reconnectBaseDelayMs);

    for (int attempt = 1; attempt <= maxRetries && !m_stopping.load(); ++attempt) {
        emit infoMessage(QString("推流链路异常，尝试重连 (%1/%2)...").arg(attempt).arg(maxRetries));

        const int delayMs = baseDelayMs * attempt;
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

        if (initOutput(m_config)) {
            m_reconnectCount.fetch_add(1);
            emit infoMessage("重连成功，恢复推流。");
            emitStatsIfNeeded(true);
            return true;
        }
    }

    return false;
}

void RtmpStreamer::emitStatsIfNeeded(bool force)
{
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastStatsEmit).count();
    if (!force && elapsed < 1000) {
        return;
    }

    m_lastStatsEmit = now;
    emit statsUpdated(
        m_inputFrames.load(),
        m_encodedPackets.load(),
        m_droppedFrames.load(),
        m_reconnectCount.load(),
        m_failedWrites.load());
}

bool RtmpStreamer::initOutput(const Config& config)
{
    int ret = avformat_network_init();
    if (ret < 0) {
        emit errorOccurred(QString("FFmpeg 网络初始化失败: %1").arg(ffmpegError(ret)));
        return false;
    }

    ret = avformat_alloc_output_context2(&m_outputCtx, nullptr, "flv", config.url.toUtf8().constData());
    if (ret < 0 || m_outputCtx == nullptr) {
        emit errorOccurred(QString("创建 RTMP 输出上下文失败: %1").arg(ffmpegError(ret)));
        return false;
    }

    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (codec == nullptr) {
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    if (codec == nullptr) {
        emit errorOccurred("未找到 H.264 编码器（libx264 或内置 H264）。");
        return false;
    }

    m_videoStream = avformat_new_stream(m_outputCtx, codec);
    if (m_videoStream == nullptr) {
        emit errorOccurred("创建视频流失败。");
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (m_codecCtx == nullptr) {
        emit errorOccurred("创建编码器上下文失败。");
        return false;
    }

    m_codecCtx->codec_id = codec->id;
    m_codecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    m_codecCtx->width = config.width;
    m_codecCtx->height = config.height;
    m_codecCtx->time_base = AVRational { 1, config.fps };
    m_codecCtx->framerate = AVRational { config.fps, 1 };
    m_codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    m_codecCtx->bit_rate = static_cast<int64_t>(config.bitrateKbps) * 1000;
    m_codecCtx->gop_size = config.fps * 2;
    m_codecCtx->max_b_frames = 0;

    if (m_outputCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        m_codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    av_opt_set(m_codecCtx->priv_data, "preset", "veryfast", 0);
    av_opt_set(m_codecCtx->priv_data, "tune", "zerolatency", 0);

    ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        emit errorOccurred(QString("打开编码器失败: %1").arg(ffmpegError(ret)));
        return false;
    }

    ret = avcodec_parameters_from_context(m_videoStream->codecpar, m_codecCtx);
    if (ret < 0) {
        emit errorOccurred(QString("写入编码参数失败: %1").arg(ffmpegError(ret)));
        return false;
    }

    m_videoStream->time_base = m_codecCtx->time_base;

    m_frame = av_frame_alloc();
    if (m_frame == nullptr) {
        emit errorOccurred("分配编码帧失败。");
        return false;
    }

    m_frame->format = m_codecCtx->pix_fmt;
    m_frame->width = m_codecCtx->width;
    m_frame->height = m_codecCtx->height;

    ret = av_frame_get_buffer(m_frame, 32);
    if (ret < 0) {
        emit errorOccurred(QString("为编码帧分配缓冲失败: %1").arg(ffmpegError(ret)));
        return false;
    }

    m_packet = av_packet_alloc();
    if (m_packet == nullptr) {
        emit errorOccurred("分配编码包失败。");
        return false;
    }

    if (!(m_outputCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_outputCtx->pb, config.url.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            emit errorOccurred(QString("打开 RTMP 输出失败: %1").arg(ffmpegError(ret)));
            return false;
        }
    }

    ret = avformat_write_header(m_outputCtx, nullptr);
    if (ret < 0) {
        emit errorOccurred(QString("写入 RTMP header 失败: %1").arg(ffmpegError(ret)));
        return false;
    }

    m_frameIndex = 0;
    return true;
}

void RtmpStreamer::cleanupOutput()
{
    if (m_packet != nullptr) {
        av_packet_free(&m_packet);
    }

    if (m_frame != nullptr) {
        av_frame_free(&m_frame);
    }

    if (m_codecCtx != nullptr) {
        avcodec_free_context(&m_codecCtx);
    }

    if (m_swsCtx != nullptr) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }

    if (m_outputCtx != nullptr) {
        if (!(m_outputCtx->oformat->flags & AVFMT_NOFILE) && m_outputCtx->pb != nullptr) {
            avio_closep(&m_outputCtx->pb);
        }
        avformat_free_context(m_outputCtx);
        m_outputCtx = nullptr;
    }

    m_videoStream = nullptr;
}

bool RtmpStreamer::encodeAndWriteImage(const QImage& image)
{
    if (m_codecCtx == nullptr || m_frame == nullptr || m_packet == nullptr || m_videoStream == nullptr) {
        return false;
    }

    int ret = av_frame_make_writable(m_frame);
    if (ret < 0) {
        emit errorOccurred(QString("编码帧不可写: %1").arg(ffmpegError(ret)));
        return false;
    }

    m_swsCtx = sws_getCachedContext(
        m_swsCtx,
        image.width(),
        image.height(),
        AV_PIX_FMT_RGBA,
        m_codecCtx->width,
        m_codecCtx->height,
        m_codecCtx->pix_fmt,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr);

    if (m_swsCtx == nullptr) {
        emit errorOccurred("创建像素格式转换上下文失败。");
        return false;
    }

    uint8_t* srcData[4] = {
        const_cast<uint8_t*>(image.constBits()),
        nullptr,
        nullptr,
        nullptr
    };
    int srcLineSize[4] = {
        static_cast<int>(image.bytesPerLine()),
        0,
        0,
        0
    };

    sws_scale(
        m_swsCtx,
        srcData,
        srcLineSize,
        0,
        image.height(),
        m_frame->data,
        m_frame->linesize);

    m_frame->pts = m_frameIndex++;

    ret = avcodec_send_frame(m_codecCtx, m_frame);
    if (ret < 0) {
        emit errorOccurred(QString("送入编码器失败: %1").arg(ffmpegError(ret)));
        return false;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(m_codecCtx, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            emit errorOccurred(QString("从编码器取包失败: %1").arg(ffmpegError(ret)));
            return false;
        }

        av_packet_rescale_ts(m_packet, m_codecCtx->time_base, m_videoStream->time_base);
        m_packet->stream_index = m_videoStream->index;

        int writeRet = av_interleaved_write_frame(m_outputCtx, m_packet);
        av_packet_unref(m_packet);

        if (writeRet < 0) {
            m_failedWrites.fetch_add(1);
            emit errorOccurred(QString("写入 RTMP 包失败: %1").arg(ffmpegError(writeRet)));
            return false;
        }

        m_encodedPackets.fetch_add(1);
    }

    return true;
}

bool RtmpStreamer::flushEncoder()
{
    if (m_codecCtx == nullptr || m_outputCtx == nullptr || m_packet == nullptr || m_videoStream == nullptr) {
        return false;
    }

    int ret = avcodec_send_frame(m_codecCtx, nullptr);
    if (ret < 0) {
        emit errorOccurred(QString("发送 flush 帧失败: %1").arg(ffmpegError(ret)));
        return false;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(m_codecCtx, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            emit errorOccurred(QString("flush 取包失败: %1").arg(ffmpegError(ret)));
            return false;
        }

        av_packet_rescale_ts(m_packet, m_codecCtx->time_base, m_videoStream->time_base);
        m_packet->stream_index = m_videoStream->index;

        int writeRet = av_interleaved_write_frame(m_outputCtx, m_packet);
        av_packet_unref(m_packet);
        if (writeRet < 0) {
            emit errorOccurred(QString("flush 写包失败: %1").arg(ffmpegError(writeRet)));
            return false;
        }
    }

    return true;
}

QString RtmpStreamer::ffmpegError(int code) const
{
    char errBuf[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    av_strerror(code, errBuf, sizeof(errBuf));
    return QString::fromUtf8(errBuf);
}
#endif
