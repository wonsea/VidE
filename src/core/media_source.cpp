#include "core/media_source.h"

#include <algorithm>
#include <cmath>
#include <fstream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/motion_vector.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace vide {
namespace {

CodecId mapCodecId(AVCodecID id) {
    switch (id) {
        case AV_CODEC_ID_H264: return CodecId::H264;
        case AV_CODEC_ID_HEVC: return CodecId::HEVC;
        case AV_CODEC_ID_VVC: return CodecId::VVC;
        case AV_CODEC_ID_VP9: return CodecId::VP9;
        case AV_CODEC_ID_AV1: return CodecId::AV1;
        case AV_CODEC_ID_MJPEG:
        case AV_CODEC_ID_JPEG2000: return CodecId::JPEG;
        case AV_CODEC_ID_PNG: return CodecId::PNG;
        default: return CodecId::Unknown;
    }
}

PixelFormat mapPixelFormat(AVPixelFormat fmt) {
    switch (fmt) {
        case AV_PIX_FMT_YUV420P: return PixelFormat::I420;
        case AV_PIX_FMT_NV12: return PixelFormat::NV12;
        case AV_PIX_FMT_NV21: return PixelFormat::NV21;
        case AV_PIX_FMT_YUV422P: return PixelFormat::YUV422P;
        case AV_PIX_FMT_YUV444P: return PixelFormat::YUV444P;
        case AV_PIX_FMT_GRAY8: return PixelFormat::YUV400;
        case AV_PIX_FMT_P010LE: return PixelFormat::P010;
        case AV_PIX_FMT_P012LE: return PixelFormat::P012;
        case AV_PIX_FMT_P016LE: return PixelFormat::P016;
        case AV_PIX_FMT_RGB24:
        case AV_PIX_FMT_BGR24: return PixelFormat::RGB888;
        default: return PixelFormat::Unknown;
    }
}

BitDepth mapBitDepth(int depth) {
    switch (depth) {
        case 10: return BitDepth::B10;
        case 12: return BitDepth::B12;
        case 16: return BitDepth::B16;
        default: return BitDepth::B8;
    }
}

}  // namespace

MediaSource::MediaSource() = default;

MediaSource::~MediaSource() {
    close();
}

Status MediaSource::open(const std::string& path) {
    close();
    path_ = path;

    if (avformat_open_input(&format_, path.c_str(), nullptr, nullptr) < 0) {
        return Status::error(StatusCode::IoError, "FFmpeg could not open '" + path + "'");
    }
    // find_stream_info enriches the metadata (bit depth, frame rate, ...) but
    // is not required: avformat_open_input already gives codec + dimensions.
    // Some AV1 IVF files (libdav1d) fail it while still being fully decodable,
    // so treat a failure as non-fatal.
    avformat_find_stream_info(format_, nullptr);
    stream_index_ = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_index_ < 0) {
        return Status::error(StatusCode::Unsupported, "no video stream");
    }
    AVStream* stream = format_->streams[stream_index_];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (decoder == nullptr) {
        return Status::error(StatusCode::Unsupported, "no decoder");
    }
    codec_ = avcodec_alloc_context3(decoder);
    if (codec_ == nullptr || avcodec_parameters_to_context(codec_, stream->codecpar) < 0) {
        return Status::error(StatusCode::DecodeError, "failed to open decoder");
    }
    // Ask the decoder to attach per-block motion vectors (export_mvs) when the
    // codec supports it; a no-op otherwise.
    codec_->flags2 |= AV_CODEC_FLAG2_EXPORT_MVS;
    if (avcodec_open2(codec_, decoder, nullptr) < 0) {
        return Status::error(StatusCode::DecodeError, "failed to open decoder");
    }
    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();

    const AVCodecParameters* par = stream->codecpar;
    const auto pix = static_cast<AVPixelFormat>(par->format);
    info_ = MediaInfo{};
    info_.codec = mapCodecId(par->codec_id);
    info_.width = codec_->width > 0 ? codec_->width : par->width;
    info_.height = codec_->height > 0 ? codec_->height : par->height;
    info_.format = mapPixelFormat(pix);
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(pix);
    info_.bit_depth = mapBitDepth(desc != nullptr ? desc->comp[0].depth : 8);
    AVRational fps = av_guess_frame_rate(format_, stream, nullptr);
    info_.fps_num = fps.num;
    info_.fps_den = fps.den != 0 ? fps.den : 1;
    info_.frame_count = stream->nb_frames > 0 ? static_cast<int64_t>(stream->nb_frames) : -1;
    info_.container = (format_->iformat != nullptr && format_->iformat->name != nullptr)
                          ? format_->iformat->name
                          : "unknown";
    return Status::success();
}

void MediaSource::close() {
    if (sws_ != nullptr) {
        sws_freeContext(sws_);
        sws_ = nullptr;
    }
    if (frame_ != nullptr) av_frame_free(&frame_);
    if (packet_ != nullptr) av_packet_free(&packet_);
    if (codec_ != nullptr) avcodec_free_context(&codec_);
    if (format_ != nullptr) avformat_close_input(&format_);
    stream_index_ = -1;
    decoded_index_ = -1;
    sent_flush_ = false;
    info_ = MediaInfo{};
    path_.clear();
}

Result<DecodedImage> MediaSource::decodeFrame(uint32_t index) {
    if (format_ == nullptr) {
        return Status::error(StatusCode::InvalidArgument, "no source open");
    }
    // Only seek when we truly need to rewind (a raw elementary stream cannot
    // seek; jumping to frame 0 is done by flushing and re-reading instead).
    if (decoded_index_ >= 0 && static_cast<int64_t>(index) < decoded_index_) {
        if (av_seek_frame(format_, stream_index_, 0, AVSEEK_FLAG_BACKWARD) < 0) {
            return Status::error(StatusCode::DecodeError, "seek failed");
        }
        avcodec_flush_buffers(codec_);
        decoded_index_ = -1;
        sent_flush_ = false;
    }
    if (decoded_index_ < 0) {
        decoded_index_ = -1;
        sent_flush_ = false;
    }

    while (decoded_index_ < static_cast<int64_t>(index)) {
        const int ret = avcodec_receive_frame(codec_, frame_);
        if (ret == 0) {
            ++decoded_index_;
            continue;
        }
        if (ret == AVERROR_EOF) {
            return Status::error(StatusCode::DecodeError, "frame beyond end");
        }
        if (ret != AVERROR(EAGAIN)) {
            return Status::error(StatusCode::DecodeError, "decode error");
        }
        const int read_ret = av_read_frame(format_, packet_);
        if (read_ret < 0) {
            if (!sent_flush_) {
                avcodec_send_packet(codec_, nullptr);
                sent_flush_ = true;
                continue;
            }
            return Status::error(StatusCode::DecodeError, "frame beyond end");
        }
        if (packet_->stream_index == stream_index_) {
            avcodec_send_packet(codec_, packet_);
        }
        av_packet_unref(packet_);
    }

    // We now hold `index` in frame_.
    DecodedImage out;
    out.width = frame_->width;
    out.height = frame_->height;
    out.format = PixelFormat::RGBA8888;
    out.bit_depth = BitDepth::B8;
    out.data.resize(static_cast<std::size_t>(out.width) * out.height * 4);
    uint8_t* dst[4] = {out.data.data(), nullptr, nullptr, nullptr};
    int dst_linesize[4] = {out.width * 4, 0, 0, 0};
    sws_ = sws_getCachedContext(sws_, out.width, out.height,
                                static_cast<AVPixelFormat>(frame_->format), out.width, out.height,
                                AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws_ == nullptr) {
        return Status::error(StatusCode::Internal, "sws context failed");
    }
    sws_scale(sws_, frame_->data, frame_->linesize, 0, out.height, dst, dst_linesize);
    return out;
}

Result<DecodedImage> MediaSource::decode(uint32_t index) {
    return decodeFrame(index);
}

Result<BlockGrid> MediaSource::motionVectors(uint32_t index) {
    if (format_ == nullptr) {
        return Status::error(StatusCode::InvalidArgument, "no source open");
    }
    // Decoding positions `frame_` at `index` and populates MV side data.
    auto decoded = decodeFrame(index);
    if (!decoded.ok()) {
        return decoded.status();
    }

    BlockGrid grid;
    grid.width = info_.width;
    grid.height = info_.height;

    AVFrameSideData* side =
        av_frame_get_side_data(frame_, AV_FRAME_DATA_MOTION_VECTORS);
    if (side == nullptr || side->data == nullptr) {
        return Status::error(StatusCode::Unsupported,
                             "no motion vectors exported for this frame");
    }
    const AVMotionVector* mvs = reinterpret_cast<const AVMotionVector*>(side->data);
    const int count = side->size / static_cast<int>(sizeof(AVMotionVector));
    for (int i = 0; i < count; ++i) {
        const AVMotionVector& mv = mvs[i];
        Block block;
        block.id = static_cast<uint32_t>(grid.blocks.size());
        block.rect = Rect{mv.dst_x, mv.dst_y, mv.w, mv.h};
        block.type = (mv.motion_x == 0 && mv.motion_y == 0) ? BlockType::Skip
                                                            : BlockType::Inter;
        block.pred_mode = PredictionMode::Unknown;
        // FFmpeg's export_mvs carries no QP, so it is not available (req 7).
        block.qp = -1;
        block.qp_source = QpSource::Na;
        // Normalise to quarter-pel like the reference helpers.
        const int scale = mv.motion_scale > 0 ? mv.motion_scale : 1;
        block.mv[0][0] = static_cast<int16_t>(mv.motion_x * 4 / scale);
        block.mv[0][1] = static_cast<int16_t>(mv.motion_y * 4 / scale);
        block.ref_idx[0] = 0;
        grid.blocks.push_back(block);
    }
    return grid;
}

std::vector<uint8_t> MediaSource::extradata() const {
    std::vector<uint8_t> out;
    if (format_ == nullptr || stream_index_ < 0) {
        return out;
    }
    const AVCodecParameters* par = format_->streams[stream_index_]->codecpar;
    if (par != nullptr && par->extradata != nullptr && par->extradata_size > 0) {
        out.assign(par->extradata, par->extradata + par->extradata_size);
    }
    return out;
}

Result<DecodedImage> MediaSource::thumbnail(uint32_t index, int max_width, int max_height) {
    auto full = decodeFrame(index);
    if (!full.ok()) {
        return full.status();
    }
    const DecodedImage& src = full.value();
    const double scale = std::min({static_cast<double>(max_width) / src.width,
                                   static_cast<double>(max_height) / src.height, 1.0});
    const int tw = std::max(1, static_cast<int>(std::floor(src.width * scale)));
    const int th = std::max(1, static_cast<int>(std::floor(src.height * scale)));
    if (tw == src.width && th == src.height) {
        return full;
    }

    DecodedImage out;
    out.width = tw;
    out.height = th;
    out.format = PixelFormat::RGBA8888;
    out.bit_depth = BitDepth::B8;
    out.data.resize(static_cast<std::size_t>(tw) * th * 4);
    SwsContext* ctx = sws_getContext(src.width, src.height, AV_PIX_FMT_RGBA, tw, th, AV_PIX_FMT_RGBA,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (ctx == nullptr) {
        return Status::error(StatusCode::Internal, "sws context failed");
    }
    const uint8_t* src_data[4] = {src.data.data(), nullptr, nullptr, nullptr};
    const int src_linesize[4] = {src.width * 4, 0, 0, 0};
    uint8_t* dst[4] = {out.data.data(), nullptr, nullptr, nullptr};
    int dst_linesize[4] = {tw * 4, 0, 0, 0};
    sws_scale(ctx, src_data, src_linesize, 0, src.height, dst, dst_linesize);
    sws_freeContext(ctx);
    return out;
}

Result<std::string> MediaSource::extractEs(const std::string& out_path,
                                           const char* bsf_name) const {
    if (format_ == nullptr || stream_index_ < 0) {
        return Status::error(StatusCode::InvalidArgument, "no source open");
    }
    // Already Annex-B? (elementary stream containers)
    const std::string fmt = format_->iformat != nullptr && format_->iformat->name != nullptr
                                ? format_->iformat->name
                                : "";
    if (fmt.find("hevc") != std::string::npos || fmt.find("vvc") != std::string::npos ||
        fmt.find("h264") != std::string::npos || fmt.find("av1") != std::string::npos) {
        return path_;  // already an ES
    }

    AVFormatContext* input = nullptr;
    if (avformat_open_input(&input, path_.c_str(), nullptr, nullptr) < 0) {
        return Status::error(StatusCode::IoError, "cannot reopen for ES extraction");
    }
    const int vs = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vs < 0) {
        avformat_close_input(&input);
        return Status::error(StatusCode::Unsupported, "no video stream");
    }
    const AVBitStreamFilter* filter = av_bsf_get_by_name(bsf_name);
    AVBSFContext* bsf = nullptr;
    if (filter == nullptr || av_bsf_alloc(filter, &bsf) < 0) {
        avformat_close_input(&input);
        return Status::error(StatusCode::Internal, std::string(bsf_name) + " unavailable");
    }
    avcodec_parameters_copy(bsf->par_in, input->streams[vs]->codecpar);
    if (av_bsf_init(bsf) < 0) {
        av_bsf_free(&bsf);
        avformat_close_input(&input);
        return Status::error(StatusCode::Internal, "bsf init failed");
    }

    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        av_bsf_free(&bsf);
        avformat_close_input(&input);
        return Status::error(StatusCode::IoError, "cannot create ES file");
    }

    AVPacket* packet = av_packet_alloc();
    AVPacket* filtered = av_packet_alloc();
    while (av_read_frame(input, packet) >= 0) {
        if (packet->stream_index == vs) {
            if (av_bsf_send_packet(bsf, packet) < 0) {
                av_packet_unref(packet);
                break;
            }
            while (av_bsf_receive_packet(bsf, filtered) == 0) {
                out.write(reinterpret_cast<const char*>(filtered->data), filtered->size);
                av_packet_unref(filtered);
            }
        }
        av_packet_unref(packet);
    }
    av_bsf_send_packet(bsf, nullptr);
    while (av_bsf_receive_packet(bsf, filtered) == 0) {
        out.write(reinterpret_cast<const char*>(filtered->data), filtered->size);
        av_packet_unref(filtered);
    }
    av_packet_free(&filtered);
    av_packet_free(&packet);
    av_bsf_free(&bsf);
    avformat_close_input(&input);
    out.close();
    return out_path;
}

Result<std::string> MediaSource::extractAv1Ivf(const std::string& out_path) const {
    if (format_ == nullptr || stream_index_ < 0) {
        return Status::error(StatusCode::InvalidArgument, "no source open");
    }
    // The IVF muxer's automatic bit-stream filters can emit noisy errors for
    // non-conforming inputs; silence them (the caller falls back anyway).
    const int prev_log = av_log_get_level();
    av_log_set_level(AV_LOG_QUIET);
    struct LogRestore {
        int level;
        ~LogRestore() { av_log_set_level(level); }
    } log_guard{prev_log};
    AVFormatContext* input = nullptr;
    if (avformat_open_input(&input, path_.c_str(), nullptr, nullptr) < 0) {
        return Status::error(StatusCode::IoError, "cannot reopen for AV1 extraction");
    }
    const int vs = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vs < 0) {
        avformat_close_input(&input);
        return Status::error(StatusCode::Unsupported, "no video stream");
    }

    AVFormatContext* output = nullptr;
    if (avformat_alloc_output_context2(&output, nullptr, "ivf", out_path.c_str()) < 0 ||
        output == nullptr) {
        avformat_close_input(&input);
        return Status::error(StatusCode::Unsupported, "ivf muxer unavailable");
    }
    AVStream* ost = avformat_new_stream(output, nullptr);
    if (ost == nullptr || avcodec_parameters_copy(ost->codecpar, input->streams[vs]->codecpar) < 0) {
        avformat_free_context(output);
        avformat_close_input(&input);
        return Status::error(StatusCode::Internal, "av1 stream copy failed");
    }
    ost->codecpar->codec_tag = 0;
    ost->time_base = AVRational{1, 1000};

    if (!(output->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output->pb, out_path.c_str(), AVIO_FLAG_WRITE) < 0) {
            avformat_free_context(output);
            avformat_close_input(&input);
            return Status::error(StatusCode::IoError, "cannot create AV1 ES file");
        }
    }
    if (avformat_write_header(output, nullptr) < 0) {
        if (output->pb != nullptr) {
            avio_closep(&output->pb);
        }
        avformat_free_context(output);
        avformat_close_input(&input);
        return Status::error(StatusCode::Internal, "ivf write header failed");
    }

    AVPacket* packet = av_packet_alloc();
    int64_t frame_index = 0;
    while (av_read_frame(input, packet) >= 0) {
        if (packet->stream_index == vs) {
            packet->stream_index = ost->index;
            packet->pts = frame_index;
            packet->dts = frame_index;
            packet->duration = 1;
            ++frame_index;
            av_write_frame(output, packet);
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    av_write_trailer(output);
    if (output->pb != nullptr) {
        avio_closep(&output->pb);
    }
    avformat_free_context(output);
    avformat_close_input(&input);
    return out_path;
}

}  // namespace vide
