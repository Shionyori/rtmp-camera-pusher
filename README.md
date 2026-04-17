# RTMP Camera Pusher

- 核心目标：开发一个直播推流程序，替代 OBS 进行单路摄像头推流。
- 数据流程：摄像头采集 → 视频编码 (H.264) → RTMP 推流 → Nginx 服务器 → 网页端播放。
- 技术架构：
    - 界面实现：Qt GUI (C++)
    - 视频采集：OpenCV
    - 编码/推流：FFmpeg (libavcodec/libavformat)
    - 流媒体服务器：Nginx + RTMP 模块
- 部署环境：Linux (Ubuntu)

## 运行模式

项目已拆分为共享核心层与双入口：
- CLI（主体）：`rtmp-camera-pusher`
- GUI（辅助工具）：`rtmp-camera-pusher-gui`

核心推流逻辑在 `src/core` + `src/streaming`，GUI 只负责交互展示，CLI 适合 Ubuntu server 部署。

## CLI 使用

先列出可用摄像头：

```bash
./build/linux-debug/rtmp-camera-pusher --list-cameras
```

启动推流：

```bash
./build/linux-debug/rtmp-camera-pusher \
    --url rtmp://127.0.0.1/live/stream \
    --camera 0 \
    --width 1280 \
    --height 720 \
    --fps 25 \
    --bitrate 2500
```

参数说明：
- `--url` RTMP 地址（必填）
- `--lang` 语言，`en` 或 `zh`（默认 `en`）
- `--camera` 摄像头索引（默认 0）
- `--width`/`--height` 编码分辨率
- `--fps` 帧率
- `--bitrate` 码率（kbps）
- `--duration` 推流秒数，`0` 表示持续运行直到收到中断信号

帮助文案会跟随 `--lang` 切换，例如：

```bash
./build/linux-debug/rtmp-camera-pusher --lang zh --help
```

## CLI 命令补全

可导出补全脚本（`bash`/`zsh`/`fish`）：

```bash
./build/linux-debug/rtmp-camera-pusher --print-completion bash
```

示例（bash 临时加载）：

```bash
source <(./build/linux-debug/rtmp-camera-pusher --print-completion bash)
```

## 可选构建

仅构建 CLI：

```bash
cmake --preset linux-debug -DBUILD_GUI=OFF
cmake --build --preset linux-debug
```

同时构建 CLI + GUI：

```bash
cmake --preset linux-debug -DBUILD_GUI=ON
cmake --build --preset linux-debug
```
