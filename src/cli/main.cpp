#include "core/StreamSession.h"

#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QTimer>
#include <QByteArray>

#include <atomic>
#include <clocale>
#include <csignal>
#include <iostream>

namespace {
std::atomic<bool> g_interrupted(false);

void signalHandler(int)
{
    g_interrupted.store(true);
}
}

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("rtmp-camera-pusher");

    QCommandLineParser parser;
    parser.setApplicationDescription("RTMP Camera Pusher CLI");
    parser.addHelpOption();

    QCommandLineOption listCamerasOption("list-cameras", "列出摄像头并退出");
    QCommandLineOption urlOption("url", "RTMP 推流地址", "url");
    QCommandLineOption cameraOption("camera", "摄像头索引（默认 0）", "index", "0");
    QCommandLineOption widthOption("width", "编码宽度", "width", "1280");
    QCommandLineOption heightOption("height", "编码高度", "height", "720");
    QCommandLineOption fpsOption("fps", "编码帧率", "fps", "25");
    QCommandLineOption bitrateOption("bitrate", "码率 kbps", "kbps", "2500");
    QCommandLineOption durationOption("duration", "推流时长（秒，0 表示直到中断）", "seconds", "0");

    parser.addOption(listCamerasOption);
    parser.addOption(urlOption);
    parser.addOption(cameraOption);
    parser.addOption(widthOption);
    parser.addOption(heightOption);
    parser.addOption(fpsOption);
    parser.addOption(bitrateOption);
    parser.addOption(durationOption);
    parser.process(app);

    StreamSession session;
    QObject::connect(&session, &StreamSession::logMessage, [](const QString& message) {
        std::cout << message.toStdString() << std::endl;
    });

    const QStringList cameras = session.cameraDescriptions();
    if (parser.isSet(listCamerasOption)) {
        for (int i = 0; i < cameras.size(); ++i) {
            std::cout << i << ": " << cameras.at(i).toStdString() << std::endl;
        }
        return 0;
    }

    if (!parser.isSet(urlOption)) {
        std::cerr << "缺少参数: --url <rtmp-url>" << std::endl;
        parser.showHelp(2);
    }

    const int cameraIndex = parser.value(cameraOption).toInt();
    if (!session.selectCamera(cameraIndex)) {
        std::cerr << "选择摄像头失败，请先使用 --list-cameras 查看可用索引。" << std::endl;
        return 2;
    }

    RtmpStreamer::Config config;
    config.url = parser.value(urlOption).trimmed();
    config.width = parser.value(widthOption).toInt();
    config.height = parser.value(heightOption).toInt();
    config.fps = parser.value(fpsOption).toInt();
    config.bitrateKbps = parser.value(bitrateOption).toInt();

    if (!session.startStreaming(config)) {
        std::cerr << "启动推流失败。" << std::endl;
        return 3;
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&session]() {
        session.stopStreaming();
    });

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    QTimer signalPoll;
    QObject::connect(&signalPoll, &QTimer::timeout, [&app]() {
        if (g_interrupted.load()) {
            app.quit();
        }
    });
    signalPoll.start(200);

    const int duration = parser.value(durationOption).toInt();
    if (duration > 0) {
        QTimer::singleShot(duration * 1000, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
