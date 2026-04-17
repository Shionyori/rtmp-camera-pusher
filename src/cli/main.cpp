#include "core/StreamSession.h"

#include "common/AppLocale.h"

#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QTimer>
#include <QByteArray>

#include <atomic>
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
    AppLocale::apply(AppLocale::Language::English);
    QCoreApplication::setApplicationName("rtmp-camera-pusher");

    QCommandLineParser parser;
    parser.setApplicationDescription("RTMP Camera Pusher CLI");
    parser.addHelpOption();

    QCommandLineOption listCamerasOption("list-cameras", "列出摄像头并退出");
    QCommandLineOption langOption("lang", "语言: en|zh (default: en)", "lang", "en");
    QCommandLineOption urlOption("url", "RTMP 推流地址", "url");
    QCommandLineOption cameraOption("camera", "摄像头索引（默认 0）", "index", "0");
    QCommandLineOption widthOption("width", "编码宽度", "width", "1280");
    QCommandLineOption heightOption("height", "编码高度", "height", "720");
    QCommandLineOption fpsOption("fps", "编码帧率", "fps", "25");
    QCommandLineOption bitrateOption("bitrate", "码率 kbps", "kbps", "2500");
    QCommandLineOption durationOption("duration", "推流时长（秒，0 表示直到中断）", "seconds", "0");

    parser.addOption(listCamerasOption);
    parser.addOption(langOption);
    parser.addOption(urlOption);
    parser.addOption(cameraOption);
    parser.addOption(widthOption);
    parser.addOption(heightOption);
    parser.addOption(fpsOption);
    parser.addOption(bitrateOption);
    parser.addOption(durationOption);
    parser.process(app);

    const QString langValue = parser.value(langOption).trimmed().toLower();
    AppLocale::Language language = AppLocale::Language::English;
    if (langValue == "en" || langValue == "en-us") {
        language = AppLocale::Language::English;
    } else if (langValue == "zh" || langValue == "zh-cn" || langValue == "cn") {
        language = AppLocale::Language::Chinese;
    } else {
        std::cerr << "Invalid --lang value. Use: en or zh" << std::endl;
        return 2;
    }
    AppLocale::apply(language);
    const bool zh = (language == AppLocale::Language::Chinese);

    StreamSession session;
    QObject::connect(&session, &StreamSession::logMessage, [language](const QString& message) {
        std::cout << AppLocale::localizeLogMessage(message, language).toStdString() << std::endl;
    });
    QObject::connect(&session, &StreamSession::statsUpdated,
                     [](quint64 inputFrames,
                        quint64 encodedPackets,
                        quint64 droppedFrames,
                        quint64 reconnectCount,
                        quint64 failedWrites) {
                         std::cout << "[stats] in=" << inputFrames
                                   << " out=" << encodedPackets
                                   << " drop=" << droppedFrames
                                   << " reconnect=" << reconnectCount
                                   << " failedWrite=" << failedWrites
                                   << std::endl;
                     });

    const QStringList cameras = session.cameraDescriptions();
    if (parser.isSet(listCamerasOption)) {
        for (int i = 0; i < cameras.size(); ++i) {
            std::cout << i << ": " << cameras.at(i).toStdString() << std::endl;
        }
        return 0;
    }

    if (!parser.isSet(urlOption)) {
        std::cerr << (zh ? "缺少参数: --url <rtmp-url>" : "Missing argument: --url <rtmp-url>") << std::endl;
        parser.showHelp(2);
    }

    const int cameraIndex = parser.value(cameraOption).toInt();
    if (!session.selectCamera(cameraIndex)) {
        std::cerr << (zh
            ? "选择摄像头失败，请先使用 --list-cameras 查看可用索引。"
            : "Failed to select camera. Use --list-cameras to check valid indices.")
            << std::endl;
        return 2;
    }

    RtmpStreamer::Config config;
    config.url = parser.value(urlOption).trimmed();
    config.width = parser.value(widthOption).toInt();
    config.height = parser.value(heightOption).toInt();
    config.fps = parser.value(fpsOption).toInt();
    config.bitrateKbps = parser.value(bitrateOption).toInt();

    if (!session.startStreaming(config)) {
        std::cerr << (zh ? "启动推流失败。" : "Failed to start streaming.") << std::endl;
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
