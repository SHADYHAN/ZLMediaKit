/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "TranscodeConfig.h"
#include "Common/config.h"
#include "Util/logger.h"
#include "Util/util.h"
#include "Util/File.h"
#include <regex>
#include <sstream>

using namespace std;
using namespace toolkit;

namespace mediakit {

string TranscodeTemplate::getFFmpegParams() const {
    stringstream ss;
    
    // 视频编码参数
    if (!video_codec.empty()) {
        ss << " -vcodec " << video_codec;
        
        if (video_bitrate > 0) {
            ss << " -b:v " << video_bitrate << "k";
        }
        
        // 注意：scale参数已经在filter_params中处理，这里不再重复添加
        // if (width > 0 && height > 0) {
        //     ss << " -vf scale=" << width << ":" << height;
        // }
        
        if (fps > 0) {
            ss << " -r " << fps;
        }
        
        if (!video_params.empty()) {
            ss << " " << video_params;
        }
    }
    
    // 音频编码参数
    if (!audio_codec.empty()) {
        ss << " -acodec " << audio_codec;
        
        if (audio_bitrate > 0) {
            ss << " -b:a " << audio_bitrate << "k";
        }
        
        if (!audio_params.empty()) {
            ss << " " << audio_params;
        }
    }
    
    // 滤镜参数
    if (!filter_params.empty()) {
        ss << " " << filter_params;
    }
    
    return ss.str();
}

bool TranscodeTemplate::isValid() const {
    return !name.empty() && (!video_codec.empty() || !audio_codec.empty());
}

bool TranscodeRule::matchStream(const string &app, const string &stream) const {
    try {
        // 简单的通配符匹配，支持 * 通配符
        auto pattern_to_regex = [](const string &pattern) {
            string regex_pattern = pattern;
            // 转义正则表达式特殊字符
            regex_pattern = regex_replace(regex_pattern, regex("\\*"), ".*");
            regex_pattern = regex_replace(regex_pattern, regex("\\?"), ".");
            return "^" + regex_pattern + "$";
        };
        
        regex app_regex(pattern_to_regex(app_pattern));
        regex stream_regex(pattern_to_regex(stream_pattern));
        
        return regex_match(app, app_regex) && regex_match(stream, stream_regex);
    } catch (const exception &ex) {
        WarnL << "Pattern matching failed: " << ex.what();
        return false;
    }
}

TranscodeConfig &TranscodeConfig::Instance() {
    static TranscodeConfig instance;
    return instance;
}

bool TranscodeConfig::loadConfig() {
    try {
        parseBasicConfig();
        parseTemplates();
        parseRules();
        
        if (!validateConfig()) {
            return false;
        }
        
        InfoL << "Transcode config loaded successfully";
        return true;
    } catch (const exception &ex) {
        _last_error = ex.what();
        ErrorL << "Failed to load transcode config: " << _last_error;
        return false;
    }
}

void TranscodeConfig::reloadConfig() {
    _templates.clear();
    _rules.clear();
    loadConfig();
}

void TranscodeConfig::parseBasicConfig() {
    _enable = mINI::Instance()[Transcode::kEnable];
    _max_concurrent = mINI::Instance()[Transcode::kMaxConcurrent];
    _temp_dir = mINI::Instance()[Transcode::kTempDir];
    _timeout_sec = mINI::Instance()[Transcode::kTimeoutSec];
    _ffmpeg_bin = mINI::Instance()[Transcode::kFFmpegBin];
    
    string hw_accel_str = mINI::Instance()[Transcode::kHwAccel];
    if (hw_accel_str == "nvidia") {
        _hw_accel = HWAccelType::NVIDIA_NVENC;
    } else if (hw_accel_str == "intel") {
        _hw_accel = HWAccelType::INTEL_QSV;
    } else if (hw_accel_str == "amd") {
        _hw_accel = HWAccelType::AMD_VCE;
    } else if (hw_accel_str == "vaapi") {
        _hw_accel = HWAccelType::VAAPI;
    } else {
        _hw_accel = HWAccelType::NONE;
    }
}

void TranscodeConfig::parseTemplates() {
    auto &ini = mINI::Instance();
    string section_prefix = string(kTranscodeTemplates) + ".";
    
    // 遍历所有配置项，查找transcode_templates.*
    for (auto &pair : ini) {
        if (pair.first.find(section_prefix) == 0) {
            string template_name = pair.first.substr(section_prefix.length());
            if (!template_name.empty()) {
                TranscodeTemplate tmpl;
                if (parseTemplate(template_name, pair.second, tmpl)) {
                    _templates[template_name] = make_shared<TranscodeTemplate>(tmpl);
                    InfoL << "Loaded transcode template: " << template_name;
                } else {
                    WarnL << "Invalid transcode template: " << template_name;
                }
            }
        }
    }
}

void TranscodeConfig::parseRules() {
    auto &ini = mINI::Instance();
    string section_prefix = string(kTranscodeRules) + ".";
    
    // 遍历所有配置项，查找transcode_rules.*
    for (auto &pair : ini) {
        if (pair.first.find(section_prefix) == 0) {
            string pattern = pair.first.substr(section_prefix.length());
            if (!pattern.empty()) {
                TranscodeRule rule;
                
                // 解析 app/stream 模式
                size_t slash_pos = pattern.find('/');
                if (slash_pos != string::npos) {
                    rule.app_pattern = pattern.substr(0, slash_pos);
                    rule.stream_pattern = pattern.substr(slash_pos + 1);
                } else {
                    rule.app_pattern = pattern;
                    rule.stream_pattern = "*";
                }
                
                // 解析模板列表
                string templates_str = pair.second;
                stringstream ss(templates_str);
                string template_name;
                
                while (getline(ss, template_name, ',')) {
                    // 去除前后空格
                    template_name.erase(0, template_name.find_first_not_of(" \t"));
                    template_name.erase(template_name.find_last_not_of(" \t") + 1);
                    if (!template_name.empty()) {
                        rule.templates.push_back(template_name);
                    }
                }
                
                if (!rule.templates.empty()) {
                    _rules.push_back(rule);
                    InfoL << "Loaded transcode rule: " << pattern << " -> " << templates_str;
                }
            }
        }
    }
}

bool TranscodeConfig::parseTemplate(const string &name, const string &params, TranscodeTemplate &tmpl) {
    tmpl.name = name;
    
    // 解析FFmpeg参数
    stringstream ss(params);
    string token;
    vector<string> tokens;
    
    while (ss >> token) {
        tokens.push_back(token);
    }
    
    for (size_t i = 0; i < tokens.size(); ++i) {
        const string &token = tokens[i];
        
        if (token == "-vcodec" && i + 1 < tokens.size()) {
            tmpl.video_codec = tokens[++i];
        } else if (token == "-acodec" && i + 1 < tokens.size()) {
            tmpl.audio_codec = tokens[++i];
        } else if (token == "-b:v" && i + 1 < tokens.size()) {
            string bitrate_str = tokens[++i];
            if (bitrate_str.back() == 'k' || bitrate_str.back() == 'K') {
                bitrate_str.pop_back();
            }
            tmpl.video_bitrate = stoi(bitrate_str);
        } else if (token == "-b:a" && i + 1 < tokens.size()) {
            string bitrate_str = tokens[++i];
            if (bitrate_str.back() == 'k' || bitrate_str.back() == 'K') {
                bitrate_str.pop_back();
            }
            tmpl.audio_bitrate = stoi(bitrate_str);
        } else if (token == "-vf" && i + 1 < tokens.size()) {
            string vf = tokens[++i];
            // 解析 scale=width:height
            regex scale_regex(R"(scale=(\d+):(\d+))");
            smatch match;
            if (regex_search(vf, match, scale_regex)) {
                tmpl.width = stoi(match[1]);
                tmpl.height = stoi(match[2]);
            }
            tmpl.filter_params += " -vf " + vf;
        } else if (token == "-r" && i + 1 < tokens.size()) {
            tmpl.fps = stoi(tokens[++i]);
        }
    }
    
    return tmpl.isValid();
}

shared_ptr<TranscodeTemplate> TranscodeConfig::getTemplate(const string &name) const {
    auto it = _templates.find(name);
    return (it != _templates.end()) ? it->second : nullptr;
}

vector<string> TranscodeConfig::getAllTemplateNames() const {
    vector<string> names;
    for (const auto &pair : _templates) {
        names.push_back(pair.first);
    }
    return names;
}

bool TranscodeConfig::addTemplate(const TranscodeTemplate &tmpl) {
    if (!tmpl.isValid()) {
        return false;
    }
    
    _templates[tmpl.name] = make_shared<TranscodeTemplate>(tmpl);
    return true;
}

bool TranscodeConfig::removeTemplate(const string &name) {
    return _templates.erase(name) > 0;
}

vector<string> TranscodeConfig::getMatchedTemplates(const string &app, const string &stream) const {
    vector<string> matched;
    
    for (const auto &rule : _rules) {
        if (rule.matchStream(app, stream)) {
            for (const auto &tmpl_name : rule.templates) {
                if (_templates.find(tmpl_name) != _templates.end()) {
                    matched.push_back(tmpl_name);
                }
            }
            break; // 只匹配第一个规则
        }
    }
    
    return matched;
}

vector<TranscodeRule> TranscodeConfig::getAllRules() const {
    return _rules;
}

bool TranscodeConfig::addRule(const TranscodeRule &rule) {
    _rules.push_back(rule);
    return true;
}

bool TranscodeConfig::removeRule(const string &app_pattern, const string &stream_pattern) {
    auto it = remove_if(_rules.begin(), _rules.end(), 
        [&](const TranscodeRule &rule) {
            return rule.app_pattern == app_pattern && rule.stream_pattern == stream_pattern;
        });
    
    if (it != _rules.end()) {
        _rules.erase(it, _rules.end());
        return true;
    }
    return false;
}

bool TranscodeConfig::validateConfig() const {
    if (!_enable) {
        return true; // 禁用时不需要验证
    }
    
    // 检查FFmpeg可执行文件
    if (!File::fileExist(_ffmpeg_bin)) {
        _last_error = "FFmpeg binary not found: " + _ffmpeg_bin;
        return false;
    }
    
    // 检查临时目录
    if (!File::is_dir(_temp_dir)) {
        if (!File::create_path(_temp_dir, 0755)) {
            _last_error = "Cannot create temp directory: " + _temp_dir;
            return false;
        }
    }
    
    // 检查模板
    if (_templates.empty()) {
        _last_error = "No transcode templates configured";
        return false;
    }
    
    for (const auto &pair : _templates) {
        if (!pair.second->isValid()) {
            _last_error = "Invalid template: " + pair.first;
            return false;
        }
    }
    
    return true;
}

string TranscodeConfig::getConfigError() const {
    return _last_error;
}

string TranscodeConfig::getHWAccelParams() const {
    switch (_hw_accel) {
    case HWAccelType::NVIDIA_NVENC:
        return "-hwaccel cuda -hwaccel_output_format cuda";
    case HWAccelType::INTEL_QSV:
        return "-hwaccel qsv -hwaccel_output_format qsv";
    case HWAccelType::AMD_VCE:
        return "-hwaccel d3d11va -hwaccel_output_format d3d11";
    case HWAccelType::VAAPI:
        return "-hwaccel vaapi -hwaccel_output_format vaapi -vaapi_device /dev/dri/renderD128";
    default:
        return "";
    }
}

} // namespace mediakit