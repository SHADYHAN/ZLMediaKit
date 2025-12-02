/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "TranscodeManager.h"
#include "Util/logger.h"
#include "Util/util.h"
#include "Common/config.h"
#include "Common/MediaSource.h"
#include "Util/NoticeCenter.h"
#include "Poller/EventPoller.h"
#include <algorithm>

using namespace std;
using namespace toolkit;

namespace mediakit {

TranscodeManager &TranscodeManager::Instance() {
    static TranscodeManager instance;
    return instance;
}

TranscodeManager::~TranscodeManager() {
    // 确保线程正确清理，避免程序退出时crash
    try {
        stop();
    } catch (...) {
        // 析构函数不应该抛出异常
    }
}

bool TranscodeManager::start() {
    if (_running) {
        return true;
    }
    
    if (!TranscodeConfig::Instance().loadConfig()) {
        ErrorL << "Failed to load transcode config: " << TranscodeConfig::Instance().getConfigError();
        return false;
    }
    
    if (!TranscodeConfig::Instance().isEnabled()) {
        InfoL << "Transcode is disabled";
        return false;
    }
    
    _running = true;
    _exit_flag = false;
    
    // 设置媒体源事件监听器
    setupMediaSourceListener();
    
    // 启动管理线程
    _manager_thread = thread([this]() {
        while (!_exit_flag) {
            try {
                checkAutoStart();
                cleanupFinishedTasks();
            } catch (const exception &ex) {
                WarnL << "TranscodeManager thread exception: " << ex.what();
            }
            
            // 每5秒检查一次
            for (int i = 0; i < 50 && !_exit_flag; ++i) {
                usleep(100000); // 100ms
            }
        }
    });
    
    InfoL << "TranscodeManager started";
    return true;
}

void TranscodeManager::stop() {
    if (!_running) {
        return;
    }
    
    InfoL << "Stopping TranscodeManager...";
    
    _exit_flag = true;
    _running = false;
    
    // 停止所有转码任务 - 先停止任务再清理监听器
    vector<string> task_ids;
    {
        lock_guard<mutex> lock(_task_mutex);
        for (const auto &pair : _tasks) {
            task_ids.push_back(pair.first);
        }
    }
    
    InfoL << "Stopping " << task_ids.size() << " transcode tasks...";
    for (const auto &task_id : task_ids) {
        try {
            stopTranscode(task_id);
        } catch (const std::exception &ex) {
            WarnL << "Error stopping transcode task " << task_id << ": " << ex.what();
        }
    }
    
    // 清理媒体源事件监听器
    try {
        cleanupMediaSourceListener();
    } catch (const std::exception &ex) {
        WarnL << "Error cleaning up media source listener: " << ex.what();
    }
    
    // 安全地join线程 - 使用超时机制避免无限等待
    try {
        if (_manager_thread.joinable()) {
            // 使用detach代替join避免阻塞
            _manager_thread.detach();
            InfoL << "Manager thread detached successfully";
        }
    } catch (const std::exception &ex) {
        WarnL << "Error handling manager thread: " << ex.what();
    }
    
    InfoL << "TranscodeManager stopped";
}

bool TranscodeManager::startTranscode(const string &app, const string &stream, 
                                     const vector<string> &templates,
                                     const string &input_url) {
    if (!_running) {
        WarnL << "TranscodeManager not running";
        return false;
    }
    
    string task_id = generateTaskId(app, stream);
    
    // 检查是否已存在
    if (hasTask(app, stream)) {
        WarnL << "Transcode task already exists: " << app << "/" << stream;
        return false;
    }
    
    // 检查并发限制
    if (!canStartNewTask()) {
        WarnL << "Reached max concurrent transcode limit";
        return false;
    }
    
    // 确定使用的模板
    vector<string> use_templates = templates;
    if (use_templates.empty()) {
        use_templates = TranscodeConfig::Instance().getMatchedTemplates(app, stream);
    }
    
    if (use_templates.empty()) {
        WarnL << "No transcode templates found for: " << app << "/" << stream;
        return false;
    }
    
    // 创建任务
    TranscodeTaskInfo task;
    task.task_id = task_id;
    task.app = app;
    task.stream = stream;
    task.input_url = input_url.empty() ? buildInputUrl(app, stream) : input_url;
    task.templates = use_templates;
    task.create_time = getCurrentMillisecond();
    task.auto_started = templates.empty();
    
    InfoL << "Starting transcode task: " << task_id 
          << " (" << app << "/" << stream << ")"
          << ", templates: " << use_templates.size()
          << ", input: " << task.input_url;
    
    // 创建转码会话
    for (const auto &template_name : use_templates) {
        string output_url = buildOutputUrl(app, stream, template_name);
        
        auto session = make_shared<TranscodeSession>(
            task.input_url, output_url, template_name, app, stream);
        
        // 设置回调
        session->setProgressCallback([this, task_id, template_name](const TranscodeInfo &info) {
            onSessionProgress(task_id, template_name, info);
        });
        
        // 启动会话
        bool started = session->start([this, task_id, template_name](
            const TranscodeInfo &info, bool success, const string &error) {
            onSessionResult(task_id, template_name, info, success, error);
        });
        
        if (started) {
            task.sessions.push_back(session);
            task.total_sessions++;
            _total_sessions++;
        } else {
            WarnL << "Failed to start transcode session: " << template_name;
        }
    }
    
    if (task.sessions.empty()) {
        ErrorL << "Failed to start any transcode sessions for: " << task_id;
        return false;
    }
    
    // 保存任务
    {
        lock_guard<mutex> lock(_task_mutex);
        _tasks[task_id] = task;
        _stream_to_task[app + "/" + stream] = task_id;
        _total_tasks++;
        _running_tasks++;
    }
    
    updateTaskStatistics(task);
    
    if (_event_callback) {
        _event_callback("start", app, stream, "", true, "");
    }
    
    return true;
}

bool TranscodeManager::stopTranscode(const string &app, const string &stream) {
    string stream_key = app + "/" + stream;
    
    lock_guard<mutex> lock(_task_mutex);
    auto it = _stream_to_task.find(stream_key);
    if (it == _stream_to_task.end()) {
        return false;
    }
    
    return stopTranscode(it->second);
}

bool TranscodeManager::stopTranscode(const string &task_id) {
    lock_guard<mutex> lock(_task_mutex);
    auto it = _tasks.find(task_id);
    if (it == _tasks.end()) {
        return false;
    }
    
    TranscodeTaskInfo &task = it->second;
    InfoL << "Stopping transcode task: " << task_id;
    
    // 停止所有会话
    for (auto &session : task.sessions) {
        if (session) {
            session->stop();
        }
    }
    
    // 更新统计
    _running_sessions -= task.running_sessions;
    _running_tasks--;
    
    // 移除任务
    string stream_key = task.app + "/" + task.stream;
    _stream_to_task.erase(stream_key);
    _tasks.erase(it);
    
    if (_event_callback) {
        _event_callback("stop", task.app, task.stream, "", true, "");
    }
    
    return true;
}

vector<TranscodeTaskInfo> TranscodeManager::getAllTasks() const {
    lock_guard<mutex> lock(_task_mutex);
    vector<TranscodeTaskInfo> tasks;
    for (const auto &pair : _tasks) {
        tasks.push_back(pair.second);
    }
    return tasks;
}

TranscodeTaskInfo TranscodeManager::getTask(const string &app, const string &stream) const {
    string stream_key = app + "/" + stream;
    
    lock_guard<mutex> lock(_task_mutex);
    auto it = _stream_to_task.find(stream_key);
    if (it != _stream_to_task.end()) {
        auto task_it = _tasks.find(it->second);
        if (task_it != _tasks.end()) {
            return task_it->second;
        }
    }
    
    return TranscodeTaskInfo{};
}

TranscodeTaskInfo TranscodeManager::getTask(const string &task_id) const {
    lock_guard<mutex> lock(_task_mutex);
    auto it = _tasks.find(task_id);
    if (it != _tasks.end()) {
        return it->second;
    }
    return TranscodeTaskInfo{};
}

bool TranscodeManager::hasTask(const string &app, const string &stream) const {
    string stream_key = app + "/" + stream;
    
    lock_guard<mutex> lock(_task_mutex);
    return _stream_to_task.find(stream_key) != _stream_to_task.end();
}

int TranscodeManager::getRunningTaskCount() const {
    return _running_tasks.load();
}

int TranscodeManager::getTotalTaskCount() const {
    return _total_tasks.load();
}

vector<TranscodeInfo> TranscodeManager::getRunningSessionsInfo() const {
    vector<TranscodeInfo> infos;
    
    lock_guard<mutex> lock(_task_mutex);
    for (const auto &pair : _tasks) {
        const auto &task = pair.second;
        for (const auto &session : task.sessions) {
            if (session && session->isRunning()) {
                infos.push_back(session->getInfo());
            }
        }
    }
    
    return infos;
}

void TranscodeManager::setEventCallback(const onTranscodeEvent &callback) {
    _event_callback = callback;
}

void TranscodeManager::onMediaSourceRegist(MediaSource &source, bool regist) {
    if (!_running || !TranscodeConfig::Instance().isEnabled()) {
        return;
    }
    
    auto &config = TranscodeConfig::Instance();
    if (!config.isEnabled()) {
        return;
    }
    
    if (regist) {
        // 媒体源注册，检查是否需要自动启动转码
        string app = source.getMediaTuple().app;
        string stream = source.getMediaTuple().stream;
        
        // 检查是否已有转码任务
        if (hasTask(app, stream)) {
            return;
        }
        
        // 获取匹配的模板
        auto templates = config.getMatchedTemplates(app, stream);
        if (!templates.empty()) {
            InfoL << "Auto starting transcode for: " << app << "/" << stream;
            startTranscode(app, stream, templates);
        }
    } else {
        // 媒体源注销（推流断开），需要停止转码
        // 因为没有输入源，继续转码会导致FFmpeg报错
        string app = source.getMediaTuple().app;
        string stream = source.getMediaTuple().stream;
        
        if (hasTask(app, stream)) {
            InfoL << "Media source unregistered, stopping transcode: " << app << "/" << stream;
            stopTranscode(app, stream);
        }
    }
}

void TranscodeManager::onMediaSourceNoneReader(MediaSource &source) {
    // 修改：源流存在但无观看者时，保持转码持续运行
    // 这是"持续转码"的核心：不管有没有人看都继续转码
    string app = source.getMediaTuple().app;
    string stream = source.getMediaTuple().stream;
    
    if (hasTask(app, stream)) {
        DebugL << "Media source has no readers, but keeping transcode running: " << app << "/" << stream;
        // 不停止转码，保持持续转码
    }
}

void TranscodeManager::checkAutoStart() {
    // 检查是否有新的媒体源需要自动启动转码
    // 这里可以与MediaSource系统集成
}

void TranscodeManager::cleanupFinishedTasks() {
    vector<string> finished_tasks;
    
    {
        lock_guard<mutex> lock(_task_mutex);
        for (auto &pair : _tasks) {
            auto &task = pair.second;
            updateTaskStatistics(task);
            
            // 检查是否所有会话都已结束
            bool all_finished = true;
            for (const auto &session : task.sessions) {
                if (session && session->isRunning()) {
                    all_finished = false;
                    break;
                }
            }
            
            if (all_finished && task.running_sessions == 0) {
                finished_tasks.push_back(pair.first);
            }
        }
    }
    
    // 清理已完成的任务
    for (const auto &task_id : finished_tasks) {
        auto task = getTask(task_id);
        InfoL << "Cleaning up finished transcode task: " << task_id;
        
        {
            lock_guard<mutex> lock(_task_mutex);
            string stream_key = task.app + "/" + task.stream;
            _stream_to_task.erase(stream_key);
            _tasks.erase(task_id);
            _running_tasks--;
        }
    }
}

void TranscodeManager::updateTaskStatistics(TranscodeTaskInfo &task) {
    int running = 0, error = 0;
    
    for (const auto &session : task.sessions) {
        if (session) {
            auto info = session->getInfo();
            if (info.state == TranscodeState::Running) {
                running++;
            } else if (info.state == TranscodeState::Error) {
                error++;
            }
        }
    }
    
    int old_running = task.running_sessions;
    task.running_sessions = running;
    task.error_sessions = error;
    
    // 更新全局统计
    _running_sessions += (running - old_running);
}

string TranscodeManager::generateTaskId(const string &app, const string &stream) const {
    return app + "_" + stream + "_" + makeRandStr(8);
}

string TranscodeManager::buildInputUrl(const string &app, const string &stream) const {
    // 构建输入URL，这里假设使用RTMP协议
    return "rtmp://127.0.0.1:1935/" + app + "/" + stream;
}

string TranscodeManager::buildOutputUrl(const string &app, const string &stream, 
                                       const string &template_name) const {
    // 构建输出URL，添加模板名称后缀
    return "rtmp://127.0.0.1:1935/" + app + "/" + stream + "_" + template_name;
}

void TranscodeManager::onSessionResult(const string &task_id, const string &template_name,
                                      const TranscodeInfo &info, bool success, const string &error) {
    auto task = getTask(task_id);
    if (task.task_id.empty()) {
        return;
    }
    
    if (_event_callback) {
        string event = success ? "session_success" : "session_error";
        _event_callback(event, task.app, task.stream, template_name, success, error);
    }
    
    if (!success) {
        WarnL << "Transcode session failed: " << task_id 
              << ", template: " << template_name 
              << ", error: " << error;
    } else {
        InfoL << "Transcode session completed: " << task_id 
              << ", template: " << template_name;
    }
}

void TranscodeManager::onSessionProgress(const string &task_id, const string &template_name,
                                        const TranscodeInfo &info) {
    // 可以在这里记录进度信息或触发进度事件
    DebugL << "Transcode progress: " << task_id 
           << ", template: " << template_name
           << ", frames: " << info.frames_out
           << ", fps: " << info.fps
           << ", bitrate: " << info.bitrate;
}

bool TranscodeManager::canStartNewTask() const {
    return _running_sessions.load() < TranscodeConfig::Instance().maxConcurrent();
}

void TranscodeManager::removeTask(const string &task_id) {
    lock_guard<mutex> lock(_task_mutex);
    auto it = _tasks.find(task_id);
    if (it != _tasks.end()) {
        string stream_key = it->second.app + "/" + it->second.stream;
        _stream_to_task.erase(stream_key);
        _tasks.erase(it);
    }
}

void TranscodeManager::setupMediaSourceListener() {
    using namespace toolkit;
    
    // 使用 this 作为监听器标签，确保唯一性
    _media_listener_tag = this;
    
    // 注册媒体源变化事件监听器 - 使用异步处理避免阻塞
    NoticeCenter::Instance().addListener(_media_listener_tag, 
        Broadcast::kBroadcastMediaChanged, 
        [this](BroadcastMediaChangedArgs) {
            // 直接在当前线程处理，但添加异常保护
            if (_running) {  // 检查管理器是否还在运行
                try {
                    this->onMediaSourceRegist(sender, bRegist);
                } catch (const std::exception &ex) {
                    WarnL << "Exception in media source event handler: " << ex.what();
                } catch (...) {
                    WarnL << "Unknown exception in media source event handler";
                }
            }
        });
        
    InfoL << "TranscodeManager media source listener setup completed";
}

void TranscodeManager::cleanupMediaSourceListener() {
    using namespace toolkit;
    
    if (_media_listener_tag) {
        // 移除媒体源变化事件监听器
        NoticeCenter::Instance().delListener(_media_listener_tag, 
            Broadcast::kBroadcastMediaChanged);
        _media_listener_tag = nullptr;
        
        InfoL << "TranscodeManager media source listener cleanup completed";
    }
}

} // namespace mediakit