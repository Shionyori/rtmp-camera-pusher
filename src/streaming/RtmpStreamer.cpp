#include "RtmpStreamer.h"

#include <QByteArray>

#include <algorithm>
#include <chrono>
#include <QUrl>

#include <cerrno>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

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

    QUrl parsedUrl(config.url);
    QString scheme = parsedUrl.scheme().toLower();
    if (!(scheme == "rtmp" || scheme == "rtmps")) {
        emit errorOccurred("RTMP 地址必须以 rtmp:// 或 rtmps:// 开头。");
        return false;
    }

    auto probeRtmpHost = [](const QString& urlStr, int timeoutMs) -> bool {
        QUrl u(urlStr);
        if (!u.isValid()) return false;
        QString host = u.host();
        if (host.isEmpty()) return false;
        int port = u.port(1935);

        struct addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        char portbuf[16];
        snprintf(portbuf, sizeof(portbuf), "%d", port);

        struct addrinfo* res = nullptr;
        int err = getaddrinfo(host.toUtf8().constData(), portbuf, &hints, &res);
        if (err != 0 || res == nullptr) {
            return false;
        }

        bool ok = false;
        for (struct addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
            int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (fd < 0) continue;

            int flags = fcntl(fd, F_GETFL, 0);
            if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

            int r = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
            if (r == 0) {
                ok = true;
                close(fd);
                break;
            } else if (errno == EINPROGRESS) {
                fd_set wf;
                FD_ZERO(&wf);
                FD_SET(fd, &wf);
                struct timeval tv;
                tv.tv_sec = timeoutMs / 1000;
                tv.tv_usec = (timeoutMs % 1000) * 1000;
                int sel = select(fd + 1, nullptr, &wf, nullptr, &tv);
                if (sel > 0 && FD_ISSET(fd, &wf)) {
                    int soerr = 0;
                    socklen_t len = sizeof(soerr);
                    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) == 0) {
                        if (soerr == 0) {
                            ok = true;
                            close(fd);
                            break;
                        }
                    }
                }
            }

            close(fd);
        }

        freeaddrinfo(res);
        return ok;
    };

    if (!probeRtmpHost(config.url, 2000)) {
        emit errorOccurred("无法连接到 RTMP 主机，请检查 URL 与网络。");
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
    emit infoMessage(QString("推流已启动: %1x%2 @ %3fps")
                         .arg(config.width)
                         .arg(config.height)
                         .arg(config.fps));
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
    
    const quint64 inputFrames = m_inputFrames.load();
    const quint64 encodedPackets = m_encodedPackets.load();
    const quint64 droppedFrames = m_droppedFrames.load();
    const quint64 reconnectCount = m_reconnectCount.load();
    const quint64 failedWrites = m_failedWrites.load();
    emit infoMessage(QString("推流已停止: 采集帧 %1 编码包 %2 丢帧 %3 重连 %4 失败 %5")
                         .arg(inputFrames)
                         .arg(encodedPackets)
                         .arg(droppedFrames)
                         .arg(reconnectCount)
                         .arg(failedWrites));
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
    m_codecCtx->time_base = AVRational { 1, 1000 };
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

    if (m_videoStream->codecpar->extradata == nullptr || m_videoStream->codecpar->extradata_size <= 0) {
        emit errorOccurred("编码器未生成 SPS/PPS (extradata)，无法初始化 RTMP H.264 码流。");
        return false;
    }

    m_videoStream->time_base = AVRational { 1, 1000 };

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

    m_lastFramePtsMs = -1;
    m_ptsClockStarted = false;
    m_hasSentKeyframe = false;
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

    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
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

    const int packedStride = image.width() * 4;
    QByteArray packedRgba;
    const uint8_t* sourceBits = image.constBits();

    if (image.bytesPerLine() != packedStride) {
        packedRgba.resize(packedStride * image.height());
        uint8_t* dst = reinterpret_cast<uint8_t*>(packedRgba.data());
        for (int y = 0; y < image.height(); ++y) {
            const uint8_t* srcRow = sourceBits + (static_cast<ptrdiff_t>(y) * image.bytesPerLine());
            std::memcpy(dst + (static_cast<ptrdiff_t>(y) * packedStride), srcRow, packedStride);
        }
        sourceBits = reinterpret_cast<const uint8_t*>(packedRgba.constData());
    }

    uint8_t* srcData[4] = {
        const_cast<uint8_t*>(sourceBits),
        nullptr,
        nullptr,
        nullptr
    };
    int srcLineSize[4] = {
        packedStride,
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

    const auto now = std::chrono::steady_clock::now();
    int64_t ptsMs = 0;
    if (!m_ptsClockStarted) {
        m_ptsStartTime = now;
        m_ptsClockStarted = true;
    } else {
        ptsMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_ptsStartTime).count();
    }

    if (ptsMs <= m_lastFramePtsMs) {
        ptsMs = m_lastFramePtsMs + 1;
    }
    m_lastFramePtsMs = ptsMs;
    m_frame->pts = ptsMs;

    if (!m_hasSentKeyframe) {
        m_frame->pict_type = AV_PICTURE_TYPE_I;
        m_frame->key_frame = 1;
    } else {
        m_frame->pict_type = AV_PICTURE_TYPE_NONE;
        m_frame->key_frame = 0;
    }

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

        if (!m_hasSentKeyframe) {
            if ((m_packet->flags & AV_PKT_FLAG_KEY) == 0) {
                av_packet_unref(m_packet);
                continue;
            }

            m_hasSentKeyframe = true;
            emit infoMessage("已发送首个关键帧，解码器初始化完成。");
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
