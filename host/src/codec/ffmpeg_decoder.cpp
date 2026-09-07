
#include "ffmpeg_decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <iostream>
#include <cstring>

namespace moonmic {

FFmpegDecoder::FFmpegDecoder()
    : codec_(nullptr), codec_ctx_(nullptr), frame_(nullptr), packet_(nullptr), swr_ctx_(nullptr) {
}

FFmpegDecoder::~FFmpegDecoder() {
    cleanup();
}

void FFmpegDecoder::cleanup() {
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
        swr_ctx_ = nullptr;
    }

    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }

    if (packet_) {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }

    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
    }
}

bool FFmpegDecoder::init(int sample_rate, int channels) {
    cleanup();

    codec_ = avcodec_find_decoder(AV_CODEC_ID_OPUS);
    if (!codec_) {
        std::cerr << "[FFmpegDecoder] Opus codec not found" << std::endl;
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec_);
    if (!codec_ctx_) {
        std::cerr << "[FFmpegDecoder] Failed to allocate codec context" << std::endl;
        return false;
    }

    codec_ctx_->sample_rate = sample_rate;
    codec_ctx_->ch_layout.nb_channels = channels;
    if (channels == 1) {
        codec_ctx_->ch_layout = AV_CHANNEL_LAYOUT_MONO;
    } else {
        codec_ctx_->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    }
    codec_ctx_->sample_fmt = AV_SAMPLE_FMT_FLT;

    int ret = avcodec_open2(codec_ctx_, codec_, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[FFmpegDecoder] Failed to open codec: " << errbuf << std::endl;
        cleanup();
        return false;
    }

    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!frame_ || !packet_) {
        std::cerr << "[FFmpegDecoder] Failed to allocate frame/packet" << std::endl;
        cleanup();
        return false;
    }

    swr_ctx_ = swr_alloc();
    if (!swr_ctx_) {
        std::cerr << "[FFmpegDecoder] Failed to allocate resampler" << std::endl;
        cleanup();
        return false;
    }

    av_opt_set_chlayout(swr_ctx_, "in_chlayout", &codec_ctx_->ch_layout, 0);
    av_opt_set_int(swr_ctx_, "in_sample_rate", sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx_, "in_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);

    av_opt_set_chlayout(swr_ctx_, "out_chlayout", &codec_ctx_->ch_layout, 0);
    av_opt_set_int(swr_ctx_, "out_sample_rate", sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx_, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

    ret = swr_init(swr_ctx_);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[FFmpegDecoder] Failed to initialize resampler: " << errbuf << std::endl;
        cleanup();
        return false;
    }

    std::cout << "[FFmpegDecoder] Initialized: " << sample_rate << "Hz, " << channels << " channels (Opus via FFmpeg)"
              << std::endl;
    return true;
}

int FFmpegDecoder::decode(const uint8_t* input, int input_size, float* output, int max_frames) {
    if (!codec_ctx_ || !input || !output) {
        return -1;
    }

    packet_->data = const_cast<uint8_t*>(input);
    packet_->size = input_size;

    int ret = avcodec_send_packet(codec_ctx_, packet_);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[FFmpegDecoder] avcodec_send_packet failed: " << errbuf << std::endl;
        return -1;
    }

    ret = avcodec_receive_frame(codec_ctx_, frame_);
    if (ret < 0) {
        if (ret != AVERROR(EAGAIN)) {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "[FFmpegDecoder] avcodec_receive_frame failed: " << errbuf << std::endl;
        }
        return 0;
    }

    int num_samples = frame_->nb_samples;

    if (frame_->format == AV_SAMPLE_FMT_FLTP) {

        const uint8_t* in_data[AV_NUM_DATA_POINTERS] = {0};
        for (int i = 0; i < codec_ctx_->ch_layout.nb_channels; i++) {
            in_data[i] = frame_->data[i];
        }

        uint8_t* out_data[1] = {reinterpret_cast<uint8_t*>(output)};
        int out_samples = max_frames;

        ret = swr_convert(swr_ctx_, out_data, out_samples, in_data, num_samples);

        if (ret < 0) {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "[FFmpegDecoder] swr_convert failed: " << errbuf << std::endl;
            av_frame_unref(frame_);
            return -1;
        }

        av_frame_unref(frame_);
        return ret;
    } else if (frame_->format == AV_SAMPLE_FMT_FLT) {

        int samples_to_copy =
            std::min(num_samples * codec_ctx_->ch_layout.nb_channels, max_frames * codec_ctx_->ch_layout.nb_channels);
        memcpy(output, frame_->data[0], samples_to_copy * sizeof(float));

        av_frame_unref(frame_);
        return num_samples;
    } else {
        std::cerr << "[FFmpegDecoder] Unexpected sample format: " << frame_->format << std::endl;
        av_frame_unref(frame_);
        return -1;
    }
}

} // namespace moonmic
