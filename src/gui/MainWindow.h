#pragma once

#include <QImage>
#include <QMainWindow>
#include <QStringList>

#include "common/AppLocale.h"

class QLabel;
class QComboBox;
class QLineEdit;
class QPushButton;
class QTextEdit;
class StreamSession;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void refreshCameraDevices();
    void onCameraListChanged(const QStringList& cameras);
    void onPreviewFrameReady(const QImage& frame);
    void onCameraSelectionChanged(int index);
    void onLanguageChanged(int index);
    void onStartClicked();
    void onStopClicked();
    void appendLog(const QString& message);

private:
    void buildUi();
    void setupConnections();
    void startPreviewForIndex(int index);
    void setStreamingControls(bool running);
    void applyLanguage();
    void loadPersistentSettings();
    void savePersistentSettings() const;

    QLabel* m_cameraLabel = nullptr;
    QLabel* m_languageLabel = nullptr;
    QLabel* m_urlLabel = nullptr;
    QComboBox* m_cameraCombo = nullptr;
    QComboBox* m_languageCombo = nullptr;
    QLineEdit* m_rtmpUrlEdit = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QTextEdit* m_logView = nullptr;
    QLabel* m_previewLabel = nullptr;
    AppLocale::Language m_language = AppLocale::Language::English;

    StreamSession* m_session = nullptr;
};
