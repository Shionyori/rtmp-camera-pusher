# Qt RTMP Camera Pusher

- 核心目标：开发一个 Qt C++ 桌面程序，替代 OBS 进行直播推流。
- 数据流程：摄像头采集 → 视频编码 (H.264) → RTMP 推流 → Nginx 服务器 → 网页端播放。
- 技术架构：
    - 界面/逻辑：Qt (C++)
    - 视频采集：OpenCV
    - 编码/推流：FFmpeg (libavcodec/libavformat)
    - 流媒体服务器：Nginx + RTMP 模块
- 部署环境：Linux (Ubuntu)
