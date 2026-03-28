#pragma once

#include <QObject>
#include <QString>

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
    };

    explicit RtmpStreamer(QObject* parent = nullptr);
    ~RtmpStreamer() override;

    bool start(const Config& config);
    void stop();

    bool isRunning() const;

signals:
    void started();
    void stopped();
    void errorOccurred(const QString& message);
    void infoMessage(const QString& message);

private:
    bool m_running = false;
};
