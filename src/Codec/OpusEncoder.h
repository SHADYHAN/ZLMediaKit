/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_OPUSENCODER_H
#define ZLMEDIAKIT_OPUSENCODER_H

#if defined(ENABLE_FFMPEG)

#include "Transcode.h"
#include "Extension/Frame.h"
#include "Extension/Track.h"

namespace mediakit {

/**
 * Opus 音频编码器
 * 用于将 PCM 音频编码为 Opus 格式
 * Opus audio encoder
 * Used to encode PCM audio to Opus format
 */
class OpusEncoder : public TaskManager {
public:
    using Ptr = std::shared_ptr<OpusEncoder>;
    using onOutput = std::function<void(const Frame::Ptr &)>;
    
    /**
     * 构造函数
     * @param sample_rate 采样率（通常 48000）
     * @param channels 声道数（1 或 2）
     * @param bitrate 比特率（建议 32000-128000）
     */
    OpusEncoder(int sample_rate = 48000, int channels = 2, int bitrate = 64000);
    ~OpusEncoder() override;
    
    /**
     * 输入 PCM 音频帧
     * @param pcm_frame FFmpeg 解码后的 PCM 帧
     * @param async 是否异步编码
     * @return 是否成功
     */
    bool inputFrame(const FFmpegFrame::Ptr &pcm_frame, bool async = false);
    
    /**
     * 设置输出回调
     * @param cb 编码后的 Opus 帧回调
     */
    void setOnOutput(onOutput cb);
    
    /**
     * 获取编码器上下文
     */
    const AVCodecContext *getContext() const;
    
private:
    bool init();
    bool encodeFrame(const FFmpegFrame::Ptr &pcm_frame);
    void onEncoded(const Frame::Ptr &opus_frame);
    
private:
    int _sample_rate;
    int _channels;
    int _bitrate;
    int64_t _total_samples = 0;  // 已编码的总样本数（用于 PTS 计算）
    onOutput _on_output;
    std::shared_ptr<AVCodecContext> _context;
    std::shared_ptr<AVFrame> _encode_frame;  // 复用编码帧
    toolkit::ResourcePool<FrameImp> _frame_pool;
    AVAudioFifo *_fifo = nullptr;
    
    // 流控统计（用于监控和调试）
    uint64_t _total_input_frames = 0;
    uint64_t _dropped_frames = 0;
    uint64_t _overflow_events = 0;
    int _drop_counter = 0;  // 丢帧计数器（实例独立）
};

/**
 * 音频转码器：AAC/G711 → PCM → Opus
 * 完整的音频转码管道
 * Audio transcoder: AAC/G711 → PCM → Opus
 * Complete audio transcoding pipeline
 */
class AudioTranscoder : public std::enable_shared_from_this<AudioTranscoder> {
public:
    using Ptr = std::shared_ptr<AudioTranscoder>;
    using onOutput = std::function<void(const Frame::Ptr &)>;
    
    /**
     * 构造函数
     * @param track 输入音频 Track（AAC/G711 等）
     * @param target_sample_rate 目标采样率（Opus 通常 48000）
     * @param target_channels 目标声道数（1 或 2）
     * @param target_bitrate 目标比特率
     */
    AudioTranscoder(const Track::Ptr &track, 
                   int target_sample_rate = 48000,
                   int target_channels = 2,
                   int target_bitrate = 64000);
    ~AudioTranscoder();
    
    /**
     * 输入音频帧（AAC/G711 等）
     * @param frame 输入帧
     * @return 是否成功
     */
    bool inputFrame(const Frame::Ptr &frame);
    
    /**
     * 设置输出回调
     * @param cb Opus 帧回调
     */
    void setOnOutput(onOutput cb);
    
    /**
     * 获取转码后的 Opus Track
     */
    Track::Ptr getOutputTrack() const;
    
    /**
     * 刷新编码器缓冲
     */
    void flush();
    
private:
    void onDecoded(const FFmpegFrame::Ptr &pcm_frame);
    void onEncoded(const Frame::Ptr &opus_frame);
    
private:
    int _target_sample_rate;
    int _target_channels;
    int _target_bitrate;
    
    FFmpegDecoder::Ptr _decoder;
    FFmpegSwr::Ptr _resampler;
    OpusEncoder::Ptr _encoder;
    
    onOutput _on_output;
    Track::Ptr _output_track;
    
    // 统计信息
    uint64_t _input_frame_count = 0;
    uint64_t _output_frame_count = 0;
};

} // namespace mediakit

#endif // ENABLE_FFMPEG
#endif // ZLMEDIAKIT_OPUSENCODER_H

