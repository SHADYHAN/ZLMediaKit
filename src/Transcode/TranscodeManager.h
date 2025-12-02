/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_TRANSCODEMANAGER_H
#define ZLMEDIAKIT_TRANSCODEMANAGER_H

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <functional>
#include <atomic>
#include <thread>
#include "TranscodeSession.h"
#include "TranscodeConfig.h"
#include "Util/TimeTicker.h"
#include "Common/MediaSource.h"
#include "Util/NoticeCenter.h"

namespace mediakit {

struct TranscodeTaskInfo {
    std::string task_id;
    std::string app;
    std::string stream;
    std::string input_url;
    std::vector<std::string> templates;
    std::vector<TranscodeSession::Ptr> sessions;
    uint64_t create_time;
    bool auto_started = false;
    
    // 统计信息
    int total_sessions = 0;
    int running_sessions = 0;
    int error_sessions = 0;
};

class TranscodeManager {
public:
    using Ptr = std::shared_ptr<TranscodeManager>;
    using onTranscodeEvent = std::function<void(const std::string &event, const std::string &app, 
                                               const std::string &stream, const std::string &template_name,
                                               bool success, const std::string &error)>;
    
    static TranscodeManager &Instance();
    
    // 析构函数：确保线程正确清理
    ~TranscodeManager();
    
    // 启动/停止管理器
    bool start();
    void stop();
    
    // 转码任务管理
    bool startTranscode(const std::string &app, const std::string &stream, 
                       const std::vector<std::string> &templates = {},
                       const std::string &input_url = "");
    bool stopTranscode(const std::string &app, const std::string &stream);
    bool stopTranscode(const std::string &task_id);
    
    // 查询接口
    std::vector<TranscodeTaskInfo> getAllTasks() const;
    TranscodeTaskInfo getTask(const std::string &app, const std::string &stream) const;
    TranscodeTaskInfo getTask(const std::string &task_id) const;
    bool hasTask(const std::string &app, const std::string &stream) const;
    
    // 统计信息
    int getRunningTaskCount() const;
    int getTotalTaskCount() const;
    std::vector<TranscodeInfo> getRunningSessionsInfo() const;
    
    // 事件回调
    void setEventCallback(const onTranscodeEvent &callback);
    
    // 媒体源事件处理
    void onMediaSourceRegist(MediaSource &source, bool regist);
    void onMediaSourceNoneReader(MediaSource &source);

private:
    TranscodeManager() = default;
    
    void checkAutoStart();
    void cleanupFinishedTasks();
    void updateTaskStatistics(TranscodeTaskInfo &task);
    
    std::string generateTaskId(const std::string &app, const std::string &stream) const;
    std::string buildInputUrl(const std::string &app, const std::string &stream) const;
    std::string buildOutputUrl(const std::string &app, const std::string &stream, 
                              const std::string &template_name) const;
    
    void onSessionResult(const std::string &task_id, const std::string &template_name,
                        const TranscodeInfo &info, bool success, const std::string &error);
    void onSessionProgress(const std::string &task_id, const std::string &template_name,
                          const TranscodeInfo &info);
    
    bool canStartNewTask() const;
    void removeTask(const std::string &task_id);
    
    // 媒体源事件监听
    void setupMediaSourceListener();
    void cleanupMediaSourceListener();
    
private:
    mutable std::mutex _task_mutex;
    std::unordered_map<std::string, TranscodeTaskInfo> _tasks; // task_id -> task info
    std::unordered_map<std::string, std::string> _stream_to_task; // app/stream -> task_id
    
    std::atomic<bool> _running{false};
    std::thread _manager_thread;
    std::atomic<bool> _exit_flag{false};
    
    onTranscodeEvent _event_callback;
    
    // 统计信息
    std::atomic<int> _total_tasks{0};
    std::atomic<int> _running_tasks{0};
    std::atomic<int> _total_sessions{0};
    std::atomic<int> _running_sessions{0};
    
    // 媒体源事件监听标签
    void *_media_listener_tag = nullptr;
};

} // namespace mediakit

#endif // ZLMEDIAKIT_TRANSCODEMANAGER_H