# 本地 ZLMediaKit 相比官方仓库的改动说明（中文）

> 仓库路径：`C:\Users\Administrator\Desktop\新建文件夹\ZLMediaKit`
>
> 本文档用于说明：当前本地 `master` 分支相对于官方 `ZLMediaKit/ZLMediaKit` 仓库 `master` 的主要改动、优化和新增功能，方便后续维护和对外说明。

---

## 一、基线信息

- 官方基线：`ZLMediaKit/ZLMediaKit` 的 `master` 分支
- 本地基线分支：`official_master`（通过以下命令拉取）
  ```bash
  git fetch https://github.com/ZLMediaKit/ZLMediaKit.git master:official_master
  ```
- 当前本地分支：`master`（`origin/master`）
- 相对官方的额外提交范围：`official_master..master`

主要代表性提交（按时间顺序）：

- `1250f3ef` / `b207a13c`：transcode AAC_TO_Opus（WebRTC 音频转码相关基础）
- `b1ab4cfc`：修复 FFmpeg 8.x AVChannelLayout 未初始化导致的崩溃
- `30c16eeb` / `e4e5b587` / `1235ac90`：音频转码相关 bug 修复
- `b8326afa` / `aaf5e2f4`：内置 TURN / JitterBuffer 逻辑修复与性能优化
- `89e12f3b` / `9cd86acf`：新增并优化 `getWatchers` 接口，支持 WebRTC/HTTP 实际客户端 IP 和过滤已停止观看的流
- `0cf99b0d` / `ee006c29`：WebRTC 逻辑与日志级别优化

---

## 二、功能增强概览

### 1. WebRTC 音频 AAC → Opus 转码能力

**目的：** 解决浏览器侧只支持 Opus 而源流是 AAC 时的兼容性问题，提升 WebRTC 播放成功率与音质可控性。

**主要改动：**

- 新增编码器：
  - `src/Codec/OpusEncoder.cpp`
  - `src/Codec/OpusEncoder.h`
- 集成到转码/媒体管线：
  - 修改 `src/Codec/Transcode.cpp`、`src/Codec/Transcode.h`，在合适路径调用 Opus 编码器实现 AAC→Opus。
- 配置层面：
  - 在 `conf/config.ini` 的 `[protocol]` 段新增音频转码相关配置：
    ```ini
    # 音频转码配置（用于 WebRTC 播放 AAC 流）
    enable_audio_transcode=0           # 是否启用音频转码（0=关闭，1=启用）
    audio_transcode_bitrate=64000     # 转码音频比特率（bps）
    audio_transcode_sample_rate=48000 # 转码音频采样率（Hz）
    audio_transcode_channels=2        # 转码音频声道数（1=单声道，2=立体声）
    ```
  - 在 `src/Common/config.h` 的 `Protocol` 命名空间中新增对应 key：
    - `kEnableAudioTranscode`
    - `kAudioTranscodeBitrate`
    - `kAudioTranscodeSampleRate`
    - `kAudioTranscodeChannels`

**效果：**

- 当启用 `enable_audio_transcode=1` 且满足条件时，可将 AAC 源流音频实时转为 Opus，供 WebRTC 端播放。
- 转码参数（码率/采样率/声道）可通过配置灵活调整，便于在“带宽”和“音质”之间权衡。

---

### 2. Watcher 统计与真实客户端 IP（含 WebRTC 场景）

**目的：** 提供更精确的观众统计能力，包括在有反向代理/网关的情况下获取 WebRTC 与 HTTP 客户端的真实 IP，辅助运维排查卡顿、掉线和流量来源问题。

**主要改动文件：**

- `server/WebHook.h` / `server/WebHook.cpp`
- `server/WebApi.cpp`
- `conf/config.ini` 中的 `http.forwarded_ip_header` 相关配置
- 部分对齐：`api/source/mk_webrtc.cpp`、`api/source/mk_media.cpp`

#### 2.1 WatcherRecord 与内存缓存

- 在 `WebHook` 中新增：
  - `struct WatcherRecord { ... }`，记录：
    - `ip` / `port` / `id` / `protocol` / `schema` / `vhost` / `app` / `stream` / `params` / `start_stamp`
  - 静态队列 `std::deque<WatcherRecord>`，最大约 300 条，用于记录最近一段时间内的观看者。
- 监听 `BroadcastMediaPlayed` 事件：
  - 每次有播放行为时写入一条 watcher 记录。
- 监听 `BroadcastStreamNoneReader` 事件：
  - 当某路流无人观看并触发事件时，清理该流对应的 watcher 记录。

#### 2.2 HTTP 接口：`/index/api/getWatchers`

- 接口：`GET /index/api/getWatchers`，需要 `secret`。
- 支持过滤参数：
  - `schema` / `vhost` / `app` / `stream` / `ip`
- 行为：
  - 从内存 watcher 队列中选出满足过滤条件的记录；
  - 对于非 `rtc` 协议，仅保留当前仍在线 Session 且对应流仍存在的 watcher；
- 返回字段（核心）：
  - `schema` / `vhost` / `app` / `stream`
  - `ip` / `port` / `id` / `protocol`
  - `params` / `start_stamp`

#### 2.3 HTTP 接口：`/index/api/getMediaListWithWatchers`

- 接口：`GET /index/api/getMediaListWithWatchers`，需要 `secret`。
- 在原有 `/index/api/getMediaList` 的每个媒体对象上，增加字段：
  - `watchers`: watcher 列表（精简版），每项仅包含：
    - `ip`
    - `port`
- 为控制返回体长度与性能：
  - 至多返回 **前 4 个（最早） + 1 个最新** 的 watcher 记录。

#### 2.4 WebRTC 实际客户端 IP 解析与绑定

- 在 `/index/api/webrtc` 中：
  - 优先从 HTTP 头 `X-Real-IP` 读取真实客户端 IP；
  - 如果为空，再从 `X-Forwarded-For` 读取（取第一个 IP）；
  - 若仍为空，则回退到 `Session.get_peer_ip()`；
  - 将得到的 `real_ip` 通过 `setRtcSessionPeerIp(session_id, real_ip)` 写入缓存映射：
    - `session_id`（HTTP 信令会话 ID） → `real_ip`；
- 在 watcher 记录逻辑中：
  - 如果是 `rtc` 协议，并且 `params` 中携带 `session=xxx`，则会根据前述映射表，把 watcher 记录里的 `ip` 修正为真实客户端 IP。

**效果：**

- 无论客户端是否处在代理/网关之后，都可以通过以上接口拿到更真实的终端 IP。
- 可以结合 `/index/api/getTranscodeList` 等转码统计 API，共同用于分析：
  - 某一路转码流当前真实观众数量与 IP 分布；
  - 某些 IP 段用户的卡顿、掉线与带宽问题。

---

### 3. WebRTC / TURN 优化与日志调整

**目的：** 提升 WebRTC 在复杂网络（NAT、丢包、抖动）和 TURN 转发场景下的稳定性，并降低生产环境中日志噪音。

**主要涉及文件：**

- `webrtc/` 目录下：
  - `IceTransport.cpp` / `IceTransport.hpp`
  - `DtlsTransport.cpp`
  - `Sdp.cpp` / `Sdp.h`
  - `SrtpSession.cpp`
  - `WebRtcPlayer.cpp`
  - `WebRtcSession.cpp`
  - `WebRtcTransport.cpp`
  - 等
- `conf/config.ini` `[rtc]` 段若干参数注释优化
- WebRTC 相关示例：`www/webrtc/index.html`、`turn_test.html`

**主要改动方向：**

- 修复 JitterBuffer ticker 相关逻辑，使在弱网场景下缓冲/收包行为更合理，减少马赛克和卡顿。
- 修复/优化内置 TURN 逻辑：
  - Allocation 生命周期、端口池、心跳检查等行为更加合理；
  - 更适合多客户端、大规模中转场景。
- 调整部分 WebRTC 日志级别：
  - 降级一些过于频繁的 debug 日志，避免生产环境日志刷屏；
  - 保留足够的信息用于问题排查。

---

### 4. Windows 平台与依赖层改动

**目的：** 提升在 Windows 平台上的 I/O 性能和构建体验。

**主要改动：**

- 新增 `3rdpart/wepoll/` 目录及相关源码（`wepoll.c/h`、`epoll.cpp/h` 等）：
  - 为 Windows 提供 epoll 风格的事件循环实现，优化网络多路复用性能。
- 更新 CMake 与 GitHub Actions：
  - 顶层 `CMakeLists.txt`
  - `.github/workflows/windows.yml`
  - `3rdpart/CMakeLists.txt`

---

## 三、Bug 修复

### 1. FFmpeg 8.x AVChannelLayout 崩溃问题

- 提交：`b1ab4cfc` 等
- 问题：
  - FFmpeg 8.x 起引入了 `AVChannelLayout` 新结构，如果未正确初始化，部分音频处理路径会在运行时崩溃。
- 修复：
  - 在相关音频初始化逻辑中补充对 `AVChannelLayout` 的初始化与兼容处理，保证在 FFmpeg 8.x 环境下稳定运行。

### 2. 音频转码链路若干问题

- 提交：`30c16eeb`、`e4e5b587`、`1235ac90` 等
- 内容：
  - 修复 AAC→Opus 转码过程中参数不一致、状态异常等问题；
  - 确保异常情况下转码任务能正确退出并清理资源；
  - 提高 WebRTC 播放 AAC 源流时的稳定性。

---

## 四、行为和配置上的主要差异小结

与官方 `master` 相比，本地 `master` 版本具备以下显著差异：

1. **新增 WebRTC 音频 AAC→Opus 转码能力**，并通过配置项灵活控制。
2. **新增 watcher 统计与真实客户端 IP 分析能力**：
   - `/index/api/getWatchers`：按 `schema/vhost/app/stream/ip` 过滤近期 watcher；
   - `/index/api/getMediaListWithWatchers`：在媒体列表中直接附带 `watchers` 精简信息；
   - `/index/api/webrtc`：结合 `X-Real-IP` 与 `X-Forwarded-For` 解析真实 IP 并与 Session 绑定。
3. **WebRTC / TURN 稳定性与性能提升**，更适合 NAT、多跳代理和弱网环境。
4. **Windows 平台通过引入 wepoll 等方式改进 I/O 性能与兼容性**。
5. **针对 FFmpeg 8.x 与音频转码链路的多处兼容性与稳定性修复**。

---

## 五、建议对外说明的一句话摘要

> 本地 ZLMediaKit 在官方版本基础上，重点增强了 **WebRTC 播放 AAC 源流的音频转码能力（AAC→Opus）** 与 **watcher 统计/真实客户端 IP 分析能力**，并对 **TURN/JitterBuffer/Windows I/O 兼容性** 等做了多处优化修复，更适合在复杂网络与高并发 WebRTC 场景下部署使用。
