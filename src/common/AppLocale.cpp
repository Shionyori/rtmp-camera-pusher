#include "common/AppLocale.h"

#include <QRegularExpression>
#include <QLocale>

#include <clocale>

namespace AppLocale {

namespace {

void applyCLocale(Language language)
{
    const char* target = (language == Language::Chinese) ? "zh_CN.UTF-8" : "en_US.UTF-8";
    if (std::setlocale(LC_ALL, target) == nullptr) {
        std::setlocale(LC_ALL, "C.UTF-8");
    }
}

} // namespace

void apply(Language language)
{
    const QLocale locale = (language == Language::Chinese)
        ? QLocale(QLocale::Chinese, QLocale::China)
        : QLocale(QLocale::English, QLocale::UnitedStates);
    QLocale::setDefault(locale);
    applyCLocale(language);
}

Language fromUiIndex(int index)
{
    return (index == static_cast<int>(Language::Chinese)) ? Language::Chinese : Language::English;
}

int toUiIndex(Language language)
{
    return static_cast<int>(language);
}

QString localizeLogMessage(const QString& message, Language language)
{
    if (language == Language::Chinese) {
        return message;
    }

    if (message == "应用已启动，等待选择摄像头并开始推流。") {
        return "Application started. Select a camera and start streaming.";
    }
    if (message == "未检测到可用摄像头设备。") {
        return "No available camera devices were detected.";
    }
    if (message == "推流状态: 已启动") {
        return "Streaming status: started";
    }
    if (message == "推流状态: 已停止") {
        return "Streaming status: stopped";
    }
    if (message == "没有可用摄像头，无法启动推流。") {
        return "No available camera. Unable to start streaming.";
    }
    if (message == "推流已在运行中。") {
        return "Streaming is already running.";
    }
    if (message == "RTMP 地址不能为空。") {
        return "RTMP URL cannot be empty.";
    }
    if (message == "未检测到 FFmpeg 开发库，无法启动推流") {
        return "FFmpeg development libraries not found; cannot start streaming.";
    }
    if (message == "帧队列持续积压，已丢弃旧帧以保持实时性。") {
        return "Frame queue remains backlogged; old frames were dropped to keep real-time behavior.";
    }
    if (message == "编码或发送失败，重连多次后仍失败，推流即将停止。") {
        return "Encoding or sending failed repeatedly after retries; streaming will stop.";
    }
    if (message == "编码器 flush 失败。") {
        return "Encoder flush failed.";
    }
    if (message == "重连成功，恢复推流。") {
        return "Reconnected successfully; streaming resumed.";
    }

    QRegularExpressionMatch match;

    match = QRegularExpression(QStringLiteral("^摄像头索引无效: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("Invalid camera index: %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^已切换摄像头: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("Switched camera: %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^推流已启动: (\\d+)x(\\d+) @ (\\d+)fps$")).match(message);
    if (match.hasMatch()) {
        return QString("Streaming started: %1x%2 @ %3fps")
            .arg(match.captured(1), match.captured(2), match.captured(3));
    }

    match = QRegularExpression(QStringLiteral("^推流已停止: 采集帧 (\\d+) 编码包 (\\d+) 丢帧 (\\d+) 重连 (\\d+) 失败 (\\d+)$"))
        .match(message);
    if (match.hasMatch()) {
        return QString("Streaming stopped: input frames %1, encoded packets %2, dropped %3, reconnects %4, failed writes %5")
            .arg(match.captured(1), match.captured(2), match.captured(3), match.captured(4), match.captured(5));
    }

    match = QRegularExpression(QStringLiteral("^推流链路异常，尝试重连 \\((\\d+)/(\\d+)\\)\\.\\.\\.$")).match(message);
    if (match.hasMatch()) {
        return QString("Streaming link issue, retrying (%1/%2)...")
            .arg(match.captured(1), match.captured(2));
    }

    match = QRegularExpression(QStringLiteral("^写入 trailer 失败: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("Failed to write trailer: %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^FFmpeg 网络初始化失败: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("FFmpeg network initialization failed: %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^创建 RTMP 输出上下文失败: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("Failed to create RTMP output context: %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^打开编码器失败: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("Failed to open encoder: %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^写入编码参数失败: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("Failed to write codec parameters: %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^为编码帧分配缓冲失败: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("Failed to allocate encoder frame buffer: %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^打开 RTMP 输出失败: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("Failed to open RTMP output: %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^写入 RTMP header 失败: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("Failed to write RTMP header: %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^编码帧不可写: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("Encoder frame is not writable: %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^送入编码器失败: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("Failed to send frame to encoder: %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^从编码器取包失败: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("Failed to receive packet from encoder: %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^首个关键帧已发送 \\| pts=(\\d+)ms size=(\\d+)B$")).match(message);
    if (match.hasMatch()) {
        return QString("First keyframe sent | pts=%1ms size=%2B")
            .arg(match.captured(1), match.captured(2));
    }

    match = QRegularExpression(QStringLiteral("^写入 RTMP 包失败: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("Failed to write RTMP packet: %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^发送 flush 帧失败: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("Failed to send flush frame: %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^flush 取包失败: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("Flush packet receive failed: %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^flush 写包失败: (.+)$")).match(message);
    if (match.hasMatch()) {
        return QString("Flush packet write failed: %1").arg(match.captured(1));
    }

    return message;
}

} // namespace AppLocale
