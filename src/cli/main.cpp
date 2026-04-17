#include "core/StreamSession.h"

#include "common/AppLocale.h"

#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QStringList>
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

bool parseLanguageValue(const QString& langValue, AppLocale::Language* language)
{
    if (langValue == "en" || langValue == "en-us") {
        *language = AppLocale::Language::English;
        return true;
    }
    if (langValue == "zh" || langValue == "zh-cn" || langValue == "cn") {
        *language = AppLocale::Language::Chinese;
        return true;
    }
    return false;
}

bool detectLanguageFromArgs(int argc,
                            char* argv[],
                            AppLocale::Language* language,
                            QString* invalidValue,
                            bool* missingValue)
{
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]).trimmed();

        if (arg == "--lang") {
            if (i + 1 >= argc) {
                *missingValue = true;
                return false;
            }

            const QString value = QString::fromLocal8Bit(argv[i + 1]).trimmed().toLower();
            if (!parseLanguageValue(value, language)) {
                *invalidValue = value;
                return false;
            }
            return true;
        }

        if (arg.startsWith("--lang=")) {
            const QString value = arg.mid(QString("--lang=").size()).trimmed().toLower();
            if (value.isEmpty()) {
                *missingValue = true;
                return false;
            }
            if (!parseLanguageValue(value, language)) {
                *invalidValue = value;
                return false;
            }
            return true;
        }
    }

    return true;
}

QString completionScript(const QString& shell, const QString& programName)
{
    const QString opts = "--help --list-cameras --lang --url --camera --width --height --fps --bitrate --duration --print-completion";

    if (shell == "bash") {
        return QString(
            "_%1_complete() {\n"
            "    local cur prev opts\n"
            "    COMPREPLY=()\n"
            "    cur=\\\"${COMP_WORDS[COMP_CWORD]}\\\"\n"
            "    prev=\\\"${COMP_WORDS[COMP_CWORD-1]}\\\"\n"
            "    opts=\\\"%2\\\"\n"
            "\n"
            "    if [[ \\\"$prev\\\" == \\\"--lang\\\" ]]; then\n"
            "        COMPREPLY=( $(compgen -W \\\"en zh en-us zh-cn cn\\\" -- \\\"$cur\\\") )\n"
            "        return 0\n"
            "    fi\n"
            "\n"
            "    if [[ \\\"$prev\\\" == \\\"--print-completion\\\" ]]; then\n"
            "        COMPREPLY=( $(compgen -W \\\"bash zsh fish\\\" -- \\\"$cur\\\") )\n"
            "        return 0\n"
            "    fi\n"
            "\n"
            "    COMPREPLY=( $(compgen -W \\\"$opts\\\" -- \\\"$cur\\\") )\n"
            "}\n"
            "complete -F _%1_complete %3\n")
            .arg(programName)
            .arg(opts)
            .arg(programName);
    }

    if (shell == "zsh") {
        return QString(
            "#compdef %1\n"
            "_arguments \\\n"
            "  '--help[Show help]' \\\n"
            "  '--list-cameras[List available cameras and exit]' \\\n"
            "  '--lang[Language: en|zh]:lang:(en zh en-us zh-cn cn)' \\\n"
            "  '--url[RTMP destination URL]:url:' \\\n"
            "  '--camera[Camera index]:index:' \\\n"
            "  '--width[Output width]:width:' \\\n"
            "  '--height[Output height]:height:' \\\n"
            "  '--fps[Output FPS]:fps:' \\\n"
            "  '--bitrate[Video bitrate in kbps]:kbps:' \\\n"
            "  '--duration[Streaming duration in seconds]:seconds:' \\\n"
            "  '--print-completion[Print completion script]:shell:(bash zsh fish)'\n")
            .arg(programName);
    }

    if (shell == "fish") {
        return QString(
            "complete -c %1 -l help -d 'Show help'\n"
            "complete -c %1 -l list-cameras -d 'List available cameras and exit'\n"
            "complete -c %1 -l lang -x -a 'en zh en-us zh-cn cn' -d 'Language'\n"
            "complete -c %1 -l url -r -d 'RTMP destination URL'\n"
            "complete -c %1 -l camera -r -d 'Camera index'\n"
            "complete -c %1 -l width -r -d 'Output width'\n"
            "complete -c %1 -l height -r -d 'Output height'\n"
            "complete -c %1 -l fps -r -d 'Output FPS'\n"
            "complete -c %1 -l bitrate -r -d 'Video bitrate in kbps'\n"
            "complete -c %1 -l duration -r -d 'Duration in seconds'\n"
            "complete -c %1 -l print-completion -x -a 'bash zsh fish' -d 'Print completion script'\n")
            .arg(programName);
    }

    return {};
}
}

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("rtmp-camera-pusher");

     AppLocale::Language language = AppLocale::Language::English;
     QString invalidLangValue;
     bool missingLangValue = false;
     if (!detectLanguageFromArgs(argc, argv, &language, &invalidLangValue, &missingLangValue)) {
          if (missingLangValue) {
                std::cerr << "Missing value for --lang. Use: --lang en|zh" << std::endl;
          } else {
                std::cerr << "Invalid --lang value: " << invalidLangValue.toStdString()
                             << ". Use: en or zh" << std::endl;
          }
          return 2;
     }

     AppLocale::apply(language);
     const bool zh = (language == AppLocale::Language::Chinese);

    QCommandLineParser parser;
    parser.setApplicationDescription(zh ? "RTMP 摄像头推流 CLI" : "RTMP Camera Pusher CLI");
    parser.addHelpOption();
    
    QCommandLineOption listCamerasOption("list-cameras", zh ? "列出摄像头并退出" : "List available cameras and exit");
    QCommandLineOption langOption("lang", zh ? "语言: en|zh (默认: en)" : "Language: en|zh (default: en)", "lang", "en");
    QCommandLineOption urlOption("url", zh ? "RTMP 推流地址" : "RTMP destination URL", "url");
    QCommandLineOption cameraOption("camera", zh ? "摄像头索引（默认 0）" : "Camera index (default: 0)", "index", "0");
    QCommandLineOption widthOption("width", zh ? "编码宽度" : "Output width", "width", "1280");
    QCommandLineOption heightOption("height", zh ? "编码高度" : "Output height", "height", "720");
    QCommandLineOption fpsOption("fps", zh ? "编码帧率" : "Output FPS", "fps", "25");
    QCommandLineOption bitrateOption("bitrate", zh ? "码率 kbps" : "Video bitrate in kbps", "kbps", "2500");
    QCommandLineOption durationOption("duration", zh ? "推流时长（秒，0 表示直到中断）" : "Streaming duration in seconds (0 means until interrupted)", "seconds", "0");
    QCommandLineOption printCompletionOption("print-completion", zh ? "打印补全脚本: bash|zsh|fish" : "Print completion script: bash|zsh|fish", "shell", "bash");

    parser.addOption(listCamerasOption);
    parser.addOption(langOption);
    parser.addOption(urlOption);
    parser.addOption(cameraOption);
    parser.addOption(widthOption);
    parser.addOption(heightOption);
    parser.addOption(fpsOption);
    parser.addOption(bitrateOption);
    parser.addOption(durationOption);
    parser.addOption(printCompletionOption);
    parser.process(app);

    if (parser.isSet(printCompletionOption)) {
        const QString shell = parser.value(printCompletionOption).trimmed().toLower();
        const QString script = completionScript(shell, QCoreApplication::applicationName());
        if (script.isEmpty()) {
            std::cerr << (zh
                ? "不支持的补全类型，请使用: bash | zsh | fish"
                : "Unsupported completion shell. Use: bash | zsh | fish")
                << std::endl;
            return 2;
        }
        std::cout << script.toStdString();
        return 0;
    }

    StreamSession session;
    QObject::connect(&session, &StreamSession::logMessage, [language](const QString& message) {
        std::cout << AppLocale::localizeLogMessage(message, language).toStdString() << std::endl;
    });
    QObject::connect(&session, &StreamSession::statsUpdated,
                     [zh](quint64 inputFrames,
                        quint64 encodedPackets,
                        quint64 droppedFrames,
                        quint64 reconnectCount,
                        quint64 failedWrites) {
                         std::cout << (zh ? "[统计] in=" : "[stats] in=") << inputFrames
                                   << (zh ? " out=" : " out=") << encodedPackets
                                   << (zh ? " drop=" : " drop=") << droppedFrames
                                   << (zh ? " reconnect=" : " reconnect=") << reconnectCount
                                   << (zh ? " failedWrite=" : " failedWrite=") << failedWrites
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
