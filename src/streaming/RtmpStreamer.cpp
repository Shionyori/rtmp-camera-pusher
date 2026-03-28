#include "RtmpStreamer.h"

#if defined(HAS_FFMPEG)
extern "C" {
#include <libavformat/avformat.h>
}
#endif

RtmpStreamer::RtmpStreamer(QObject* parent)
    : QObject(parent)
{
}

RtmpStreamer::~RtmpStreamer()
{
    stop();
}

bool RtmpStreamer::start(const Config& config)
{
    if (m_running) {
        emit infoMessage("推流已在运行中");
        return true;
    }

    if (config.url.trimmed().isEmpty()) {
        emit errorOccurred("RTMP 地址不能为空");
        return false;
    }

#if defined(HAS_FFMPEG)
    avformat_network_init();
    emit infoMessage("FFmpeg 网络模块初始化完成");
#else
    emit errorOccurred("未检测到 FFmpeg 开发库，无法启动推流");
    return false;
#endif

    // 占位：后续在这里接入编码器初始化、输出上下文和发送循环。
    m_running = true;
    emit started();
    emit infoMessage("推流骨架已启动（编码/发送尚未接入）");
    return true;
}

void RtmpStreamer::stop()
{
    if (!m_running) {
        return;
    }

#if defined(HAS_FFMPEG)
    avformat_network_deinit();
#endif

    m_running = false;
    emit stopped();
    emit infoMessage("推流已停止");
}

bool RtmpStreamer::isRunning() const
{
    return m_running;
}
