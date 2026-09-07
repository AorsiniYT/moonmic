
#pragma once

#include <cstdint>

struct AVCodec;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;

namespace moonmic {

class FFmpegDecoder {
  public:
    FFmpegDecoder();
    ~FFmpegDecoder();

    bool init(int sample_rate, int channels);

    int decode(const uint8_t* input, int input_size, float* output, int max_frames);

  private:
    const AVCodec* codec_;
    AVCodecContext* codec_ctx_;
    AVFrame* frame_;
    AVPacket* packet_;
    SwrContext* swr_ctx_;

    void cleanup();
};

} // namespace moonmic
