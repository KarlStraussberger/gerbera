/*GRB*

    Gerbera - https://gerbera.io/

    transcode_int_handler.h - this file is part of Gerbera.

    Copyright (C) 2026 Gerbera Contributors

    Gerbera is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2
    as published by the Free Software Foundation.

    Gerbera is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Gerbera.  If not, see <http://www.gnu.org/licenses/>.

    $Id$
*/

// Based in https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/transcode.c

/*
 * Copyright (c) 2010 Nicolas George
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2014 Andrey Utkin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/// @file transcoding/transcode_int_handler.cc

#include "transcode_int_handler.h"

#ifdef HAVE_FFMPEG

#include "exceptions.h"
#include "iohandler/process_io_handler.h"
#include "upnp/compat.h"
#include "util/logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

// Thanks https://ffmpeg.org/pipermail/ffmpeg-user/2021-June/053077.html
// Thanks https://github.com/joncampbell123/composite-video-simulator/issues/5
#ifdef av_err2str
#undef av_err2str
#include <string>
av_always_inline std::string av_err2string(int errnum)
{
    char str[AV_ERROR_MAX_STRING_SIZE];
    return av_make_error_string(str, AV_ERROR_MAX_STRING_SIZE, errnum);
}
#define av_err2str(err) av_err2string(err).c_str()
#endif // av_err2str

#include <map>
#include <memory>

class FilteringContext {
private:
    AVCodecContext* decContext = nullptr;
    AVCodecContext* encContext = nullptr;
    std::size_t streamNumber;
    AVFilterInOut* outputs = nullptr;
    AVFilterInOut* inputs = nullptr;
    AVFilterContext* buffersink_ctx = nullptr;
    AVFilterContext* buffersrc_ctx = nullptr;
    AVFilterGraph* filter_graph = nullptr;
    AVPacket* enc_pkt = nullptr;
    AVFrame* filtered_frame = nullptr;

    bool createVideoBuffers();
    bool createAudioBuffers();

public:
    FilteringContext(
        AVCodecContext* dec_ctx,
        AVCodecContext* enc_ctx,
        std::size_t stream_index,
        const char* filter_spec);
    virtual ~FilteringContext();

    operator bool() { return filter_graph; };
    int encodeWriteFrame(
        AVFormatContext* formatContext,
        bool flush);
    int filterEncodeWriteFrame(
        AVFrame* frame,
        AVFormatContext* formatContext);
};

#define FILTERNAME_IN "in"
#define FILTERNAME_OUT "out"

FilteringContext::FilteringContext(
    AVCodecContext* dec_ctx,
    AVCodecContext* enc_ctx,
    std::size_t stream_index,
    const char* filter_spec)
    : decContext(dec_ctx)
    , encContext(enc_ctx)
    , streamNumber(stream_index)
{
    int ret = 0;

    outputs = avfilter_inout_alloc();
    inputs = avfilter_inout_alloc();
    filter_graph = avfilter_graph_alloc();

    if (!outputs || !inputs || !filter_graph) {
        return;
    }

    if (decContext->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (!createVideoBuffers())
            return;
    } else if (decContext->codec_type == AVMEDIA_TYPE_AUDIO) {
        if (!createAudioBuffers())
            return;
    } else {
        return;
    }

    /* Endpoints for the filter graph. */
    outputs->name = av_strdup(FILTERNAME_IN);
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup(FILTERNAME_OUT);
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    if (!outputs->name || !inputs->name) {
        return;
    }
    ret = avfilter_graph_parse_ptr(filter_graph, filter_spec, &inputs, &outputs, nullptr);
    if (ret < 0)
        return;

    ret = avfilter_graph_config(filter_graph, nullptr);
    if (ret < 0)
        return;

    this->enc_pkt = av_packet_alloc();
    if (!this->enc_pkt)
        return;

    this->filtered_frame = av_frame_alloc();
    if (!this->filtered_frame)
        return;
}

bool FilteringContext::createVideoBuffers()
{
    const AVFilter* buffersrc = avfilter_get_by_name("buffer");
    const AVFilter* buffersink = avfilter_get_by_name("buffersink");
    if (!buffersrc || !buffersink) {
        log_error("filtering source or sink element not found");
        return false;
    }

    auto args = fmt::format("video_size={}x{}:pix_fmt={}:time_base={}/{}:pixel_aspect={}/{}",
        decContext->width, decContext->height,
        decContext->pix_fmt,
        decContext->pkt_timebase.num, decContext->pkt_timebase.den,
        decContext->sample_aspect_ratio.num, decContext->sample_aspect_ratio.den);

    int ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, FILTERNAME_IN,
        args.c_str(), nullptr, filter_graph);
    if (ret < 0) {
        log_error("Cannot create buffer source");
        return false;
    }

    buffersink_ctx = avfilter_graph_alloc_filter(filter_graph, buffersink, FILTERNAME_OUT);
    if (!buffersink_ctx) {
        log_error("Cannot create buffer sink");
        return false;
    }

    ret = av_opt_set_bin(buffersink_ctx, "pix_fmts",
        (uint8_t*)&encContext->pix_fmt, sizeof(encContext->pix_fmt),
        AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        log_error("Cannot set output pixel format");
    }

    ret = avfilter_init_dict(buffersink_ctx, nullptr);
    if (ret < 0) {
        log_error("Cannot initialize buffer sink");
        return false;
    }
    return true;
}

bool FilteringContext::createAudioBuffers()
{
    const AVFilter* buffersrc = avfilter_get_by_name("abuffer");
    const AVFilter* buffersink = avfilter_get_by_name("abuffersink");
    if (!buffersrc || !buffersink) {
        log_error("filtering source or sink element not found");
        return false;
    }

    if (decContext->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&decContext->ch_layout, decContext->ch_layout.nb_channels);

    char buf[64];
    av_channel_layout_describe(&decContext->ch_layout, buf, sizeof(buf));

    auto args = fmt::format("time_base={}/{}:sample_rate={}:sample_fmt={}:channel_layout={}",
        decContext->pkt_timebase.num, decContext->pkt_timebase.den, decContext->sample_rate,
        av_get_sample_fmt_name(decContext->sample_fmt),
        buf);

    int ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, FILTERNAME_IN,
        args.c_str(), nullptr, filter_graph);
    if (ret < 0) {
        log_error("Cannot create audio buffer source");
        return false;
    }

    buffersink_ctx = avfilter_graph_alloc_filter(filter_graph, buffersink, FILTERNAME_OUT);
    if (!buffersink_ctx) {
        log_error("Cannot create audio buffer sink");
        return false;
    }

    ret = av_opt_set_bin(buffersink_ctx, "sample_fmts",
        (uint8_t*)&encContext->sample_fmt, sizeof(encContext->sample_fmt),
        AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        log_error("Cannot set output sample format");
        return false;
    }

    av_channel_layout_describe(&encContext->ch_layout, buf, sizeof(buf));
    ret = av_opt_set(buffersink_ctx, "ch_layouts",
        buf, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        log_error("Cannot set output channel layout");
        return false;
    }

    ret = av_opt_set_bin(buffersink_ctx, "sample_rates",
        (uint8_t*)&encContext->sample_rate, sizeof(encContext->sample_rate),
        AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        log_error("Cannot set output sample rate");
        return false;
    }

    if (encContext->frame_size > 0)
        av_buffersink_set_frame_size(buffersink_ctx, encContext->frame_size);

    ret = avfilter_init_dict(buffersink_ctx, nullptr);
    if (ret < 0) {
        log_error("Cannot initialize audio buffer sink");
        return false;
    }
    return true;
}

FilteringContext::~FilteringContext()
{
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (filter_graph) {
        avfilter_graph_free(&filter_graph);
    }
    av_packet_free(&enc_pkt);
    av_frame_free(&filtered_frame);
}

int FilteringContext::encodeWriteFrame(
    AVFormatContext* formatContext,
    bool flush)
{
    AVFrame* filt_frame = flush ? nullptr : this->filtered_frame;
    AVPacket* enc_pkt = this->enc_pkt;
    int ret;

    log_debug("Encoding frame");
    /* encode filtered frame */
    av_packet_unref(enc_pkt);

    if (filt_frame && filt_frame->pts != AV_NOPTS_VALUE)
        filt_frame->pts = av_rescale_q(filt_frame->pts, filt_frame->time_base,
            this->encContext->time_base);

    ret = avcodec_send_frame(this->encContext, filt_frame);

    if (ret < 0)
        return ret;

    while (ret >= 0) {
        ret = avcodec_receive_packet(this->encContext, enc_pkt);

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return 0;

        /* prepare packet for muxing */
        enc_pkt->stream_index = this->streamNumber;
        av_packet_rescale_ts(enc_pkt, this->encContext->time_base, formatContext->streams[this->streamNumber]->time_base);

        log_debug("Muxing frame");
        /* mux encoded frame */
        ret = av_interleaved_write_frame(formatContext, enc_pkt);
    }

    return ret;
}

int FilteringContext::filterEncodeWriteFrame(
    AVFrame* frame,
    AVFormatContext* formatContext)
{
    int ret;

    log_debug("Pushing decoded frame to filters");
    /* push the decoded frame into the filtergraph */
    ret = av_buffersrc_add_frame_flags(this->buffersrc_ctx, frame, 0);
    if (ret < 0) {
        log_error("Error while feeding the filtergraph");
        return ret;
    }

    /* pull filtered frames from the filtergraph */
    while (1) {
        log_debug("Pulling filtered frame from filters");
        ret = av_buffersink_get_frame(this->buffersink_ctx, this->filtered_frame);
        if (ret < 0) {
            /* if no more frames for output - returns AVERROR(EAGAIN)
             * if flushed and no more frames for output - returns AVERROR_EOF
             * rewrite retcode to 0 to show it as normal procedure completion
             */
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                ret = 0;
            break;
        }

        this->filtered_frame->time_base = av_buffersink_get_time_base(this->buffersink_ctx);

        this->filtered_frame->pict_type = AV_PICTURE_TYPE_NONE;
        ret = this->encodeWriteFrame(formatContext, false);
        av_frame_unref(this->filtered_frame);
        if (ret < 0)
            break;
    }

    return ret;
}

struct TranscodingSettings {
    AVCodecID codecId;
    int width;
    int height;
    AVRational aspectRatio;
    AVPixelFormat pixelFormat;
    AVRational frameRate;
    int sampleRate;
    AVSampleFormat sampleFormat;
};

class StreamContext {
private:
    AVStream* stream = nullptr;

public:
    AVFrame* dec_frame = nullptr;
    AVCodecContext* dec_ctx = nullptr;
    AVCodecContext* enc_ctx = nullptr;

public:
    StreamContext(AVStream* stream, std::size_t streamNumber, AVFormatContext* ifmt_ctx);
    virtual ~StreamContext();

    int createEncoder(std::size_t stream_index, AVStream* out_stream, AVFormatContext* ofmt_ctx);

    operator bool() { return dec_ctx && enc_ctx; };
};

StreamContext::StreamContext(
    AVStream* stream,
    std::size_t streamNumber,
    AVFormatContext* ifmt_ctx)
    : stream(stream)
{
    const AVCodec* dec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!dec) {
        log_error("Failed to find decoder for stream #{}", streamNumber);
        return;
    }
    this->dec_ctx = avcodec_alloc_context3(dec);
    if (!this->dec_ctx) {
        log_error("Failed to allocate the decoder context for stream #{}", streamNumber);
        return;
    }
    int ret = avcodec_parameters_to_context(this->dec_ctx, stream->codecpar);
    if (ret < 0) {
        log_error("Failed to copy decoder parameters to input decoder context for stream #{}", streamNumber);
        return;
    }

    /* Inform the decoder about the timebase for the packet timestamps.
     * This is highly recommended, but not mandatory. */
    this->dec_ctx->pkt_timebase = stream->time_base;

    /* Reencode video & audio and remux subtitles etc. */
    if (this->dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO
        || this->dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        if (this->dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
            this->dec_ctx->framerate = av_guess_frame_rate(ifmt_ctx, stream, nullptr);
        /* Open decoder */
        ret = avcodec_open2(this->dec_ctx, dec, nullptr);
        if (ret < 0) {
            log_error("Failed to open decoder for stream #{}", streamNumber);
            return;
        }
    }
    this->dec_frame = av_frame_alloc();

    if (!this->dec_frame)
        return;
}

StreamContext::~StreamContext()
{
    if (dec_ctx)
        avcodec_free_context(&dec_ctx);
    if (enc_ctx)
        avcodec_free_context(&enc_ctx);
    if (dec_frame)
        av_frame_free(&dec_frame);
}

int StreamContext::createEncoder(
    std::size_t stream_index,
    AVStream* out_stream,
    AVFormatContext* ofmt_ctx)
{
    int ret = 0;

    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO
        || dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        TranscodingSettings settings;
        /* in this example, we choose transcoding to same codec */
        settings.codecId = dec_ctx->codec_id;
        /* In this example, we transcode to same properties (picture size,
         * sample rate etc.). These properties can be changed for output
         * streams easily using filters */
        settings.height = dec_ctx->height;
        settings.width = dec_ctx->width;
        settings.aspectRatio = dec_ctx->sample_aspect_ratio;
        settings.pixelFormat = dec_ctx->pix_fmt;
        settings.frameRate = dec_ctx->framerate;
        settings.sampleRate = dec_ctx->sample_rate;
        settings.sampleFormat = dec_ctx->sample_fmt;
        const AVCodec* encoder = avcodec_find_encoder(settings.codecId);
        if (!encoder) {
            log_error("Necessary encoder not found");
            return AVERROR_INVALIDDATA;
        }
        enc_ctx = avcodec_alloc_context3(encoder);
        if (!enc_ctx) {
            log_error("Failed to allocate the encoder context");
            return AVERROR(ENOMEM);
        }

        if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            const enum AVPixelFormat* pix_fmts = nullptr;

            enc_ctx->height = settings.height;
            enc_ctx->width = settings.height;
            enc_ctx->sample_aspect_ratio = settings.aspectRatio;

            ret = avcodec_get_supported_config(enc_ctx, nullptr,
                AV_CODEC_CONFIG_PIX_FORMAT, 0,
                (const void**)&pix_fmts, nullptr);

            /* take first format from list of supported formats */
            enc_ctx->pix_fmt = (ret >= 0 && pix_fmts) ? pix_fmts[0] : settings.pixelFormat;

            /* video time_base can be set to whatever is handy and supported by encoder */
            enc_ctx->time_base = av_inv_q(settings.frameRate);
        } else {
            const enum AVSampleFormat* sample_fmts = nullptr;

            enc_ctx->sample_rate = settings.sampleRate;
            ret = av_channel_layout_copy(&enc_ctx->ch_layout, &dec_ctx->ch_layout);
            if (ret < 0)
                return ret;

            ret = avcodec_get_supported_config(enc_ctx, nullptr,
                AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                (const void**)&sample_fmts, nullptr);

            /* take first format from list of supported formats */
            enc_ctx->sample_fmt = (ret >= 0 && sample_fmts) ? sample_fmts[0] : settings.sampleFormat;

            enc_ctx->time_base = (AVRational) { 1, enc_ctx->sample_rate };
        }

        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        /* Third parameter can be used to pass settings to encoder */
        ret = avcodec_open2(enc_ctx, encoder, nullptr);
        if (ret < 0) {
            log_error("Cannot open {} encoder for stream #{}", encoder->name, stream_index);
            return ret;
        }
        ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
        if (ret < 0) {
            log_error("Failed to copy encoder parameters to output stream #{}", stream_index);
            return ret;
        }

        out_stream->time_base = enc_ctx->time_base;
    } else if (dec_ctx->codec_type == AVMEDIA_TYPE_UNKNOWN) {
        log_error("Elementary stream #{} is of unknown type, cannot proceed", stream_index);
        return AVERROR_INVALIDDATA;
    } else {
        /* if this stream must be remuxed */
        ret = avcodec_parameters_copy(out_stream->codecpar, stream->codecpar);
        if (ret < 0) {
            log_error("Copying parameters for stream #{} failed\n", stream_index);
            return ret;
        }
        out_stream->time_base = stream->time_base;
    }

    return ret;
}

/// @brief Convert input to output file, applying some hard-coded filter-graph
///        on both audio and video streams.
class TranscodeInternalIOHandler : public IOHandler {
protected:
    /// @brief context for input file
    AVFormatContext* ifmt_ctx = nullptr;
    /// @brief context for output file
    AVFormatContext* ofmt_ctx = nullptr;
    std::map<std::size_t, std::shared_ptr<FilteringContext>> filteringCtx;
    std::map<std::size_t, std::shared_ptr<StreamContext>> streamCtx;
    /// @brief storage for active packet
    AVPacket* packet = nullptr;

    int open_input_file();
    int open_output_file(const std::string& filename);
    int init_filters(void);
    int flush_encoder(std::size_t stream_index);

    /// @brief Name of the file.
    GrbFile file;

    /// @brief Handle of the file.
    std::FILE* f { };

    std::mutex mutex;

    /// @brief open file with offset
    off_t offset;

public:
    /// @brief Sets the filename to work with.
    explicit TranscodeInternalIOHandler(const fs::path& filename, off_t offset = 0);
    ~TranscodeInternalIOHandler() override;

    /// @brief Opens file for reading (writing is not supported)
    void open(enum UpnpOpenFileMode mode) override;

    /// @brief Reads a previously opened file sequentially.
    /// @param buf Data from the file will be copied into this buffer.
    /// @param length Number of bytes to be copied into the buffer.
    grb_read_t read(std::byte* buf, std::size_t length) override;

    /// @brief Performs seek on an open file.
    /// @param offset Number of bytes to move in the file. For seeking forwards
    /// positive values are used, for seeking backwards - negative. Offset must
    /// be positive if origin is set to SEEK_SET
    /// @param whence The position to move relative to. SEEK_CUR to move relative
    /// to current position, SEEK_END to move relative to the end of file,
    /// SEEK_SET to specify an absolute offset.
    void seek(off_t offset, int whence) override;

    /// @brief Return the current stream position.
    off_t tell() override;

    /// @brief Close a previously opened file.
    void close() override;

    int run(const std::string& outputFileName);
};

TranscodeInternalIOHandler::TranscodeInternalIOHandler(const fs::path& filename, off_t offset)
    : file(filename)
    , offset(offset)
{
    log_debug("path = {}", file.getPath().string());
}

void TranscodeInternalIOHandler::open(enum UpnpOpenFileMode mode)
{
    log_debug("open {}", file.getPath().string());
    if (mode != UPNP_READ)
        throw_std_runtime_error("open: UpnpOpenFileMode mode not supported");

    std::lock_guard<std::mutex> lock(mutex);
    if (open_input_file())
        throw_std_runtime_error("open failed");
    if (offset > 0)
        seek(offset, SEEK_SET);
}

grb_read_t TranscodeInternalIOHandler::read(std::byte* buf, std::size_t length)
{
    std::lock_guard<std::mutex> lock(mutex);

    std::size_t ret = std::fread(buf, sizeof(std::byte), length, f);

    log_debug("read {} {}", file.getPath().string(), length);
    if (ret == 0) {
        if (std::feof(f))
            return GRB_READ_END;
        if (std::ferror(f))
            return GRB_READ_ERROR;
    }

    return ret;
}

// https://www.ffmpeg.org/doxygen/8.0/group__lavf__decoding.html#ga3b40fc8d2fda6992ae6ea2567d71ba30
// int avformat_seek_file(AVFormatContext* s, int stream_index, int64_t min_ts, int64_t ts, int64_t max_ts, int flags);
void TranscodeInternalIOHandler::seek(off_t offset, int whence)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (avformat_seek_file(ifmt_ctx, -1, offset, offset, offset, AVSEEK_FLAG_BYTE) != 0) {
        throw_std_runtime_error("seek failed");
    }
}

off_t TranscodeInternalIOHandler::tell()
{
    std::lock_guard<std::mutex> lock(mutex);
    return -1; // ftello(f);
}

void TranscodeInternalIOHandler::close()
{
    std::lock_guard<std::mutex> lock(mutex);
}

int TranscodeInternalIOHandler::open_input_file()
{
    ifmt_ctx = nullptr;
    int ret = avformat_open_input(&ifmt_ctx, file.getPath().c_str(), nullptr, nullptr);
    if (ret < 0) {
        log_error("Cannot open input file");
        return ret;
    }

    ret = avformat_find_stream_info(ifmt_ctx, nullptr);
    if (ret < 0) {
        log_error("Cannot find stream information");
        return ret;
    }

    for (std::size_t streamNumber = 0; streamNumber < ifmt_ctx->nb_streams; streamNumber++) {
        auto sCtx = std::make_shared<StreamContext>(ifmt_ctx->streams[streamNumber], streamNumber, ifmt_ctx);
        streamCtx.emplace(streamNumber, std::move(sCtx));
    }

    av_dump_format(ifmt_ctx, 0, file.getPath().c_str(), 0);
    return 0;
}

int TranscodeInternalIOHandler::open_output_file(const std::string& filename)
{
    int ret;

    ofmt_ctx = nullptr;
    avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, filename.c_str());
    if (!ofmt_ctx) {
        log_error("Could not create output context");
        return AVERROR_UNKNOWN;
    }

    for (std::size_t i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream* out_stream = avformat_new_stream(ofmt_ctx, nullptr);
        if (!out_stream) {
            log_error("Failed allocating output stream");
            return AVERROR_UNKNOWN;
        }
        ret = streamCtx.at(i)->createEncoder(i, out_stream, ofmt_ctx);
        if (ret < 0)
            return ret;
    }
    av_dump_format(ofmt_ctx, 0, filename.c_str(), 1);

    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            log_error("Could not open output file '{}'", filename);
            return ret;
        }
    }

    /* init muxer, write output file header */
    ret = avformat_write_header(ofmt_ctx, nullptr);
    if (ret < 0) {
        log_error("Error occurred when opening output file");
        return ret;
    }

    return 0;
}

int TranscodeInternalIOHandler::init_filters(void)
{
    for (std::size_t i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (!(ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO
                || ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO))
            continue;

        const char* filter_spec = (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            ? "null" /* passthrough (dummy) filter for video */
            : "anull"; /* passthrough (dummy) filter for audio */

        auto fCtx = std::make_shared<FilteringContext>(streamCtx.at(i)->dec_ctx, streamCtx.at(i)->enc_ctx, i, filter_spec);
        filteringCtx.emplace(i, std::move(fCtx));
    }
    return 0;
}

int TranscodeInternalIOHandler::flush_encoder(std::size_t stream_index)
{
    if (!(streamCtx.at(stream_index)->enc_ctx->codec->capabilities & AV_CODEC_CAP_DELAY))
        return 0;

    log_debug("Flushing stream #{} encoder", stream_index);
    return filteringCtx.at(stream_index)->encodeWriteFrame(ofmt_ctx, true);
}

int TranscodeInternalIOHandler::run(const std::string& outputFileName)
{
    int ret = open_input_file();
    if (ret < 0)
        return ret;
    ret = open_output_file(outputFileName);
    if (ret < 0)
        return ret;
    ret = init_filters();
    if (ret < 0)
        return ret;
    packet = av_packet_alloc();
    if (!packet)
        return -1;

    /* read all packets */
    while (1) {
        ret = av_read_frame(ifmt_ctx, packet);
        if (ret < 0)
            break;
        std::size_t stream_index = packet->stream_index;
        log_debug("Demuxer gave frame of stream_index #{}", stream_index);
        auto filter = filteringCtx.at(stream_index);

        if (filter && *filter) {
            auto stream = streamCtx.at(stream_index);

            log_debug("Going to reencode&filter the frame");

            ret = avcodec_send_packet(stream->dec_ctx, packet);
            if (ret < 0) {
                log_error("Decoding failed");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(stream->dec_ctx, stream->dec_frame);
                if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                    break;
                else if (ret < 0)
                    return ret;

                stream->dec_frame->pts = stream->dec_frame->best_effort_timestamp;
                ret = filter->filterEncodeWriteFrame(stream->dec_frame, ofmt_ctx);
                if (ret < 0)
                    return ret;
            }
        } else {
            /* remux this frame without reencoding */
            av_packet_rescale_ts(packet,
                ifmt_ctx->streams[stream_index]->time_base,
                ofmt_ctx->streams[stream_index]->time_base);

            ret = av_interleaved_write_frame(ofmt_ctx, packet);
            if (ret < 0)
                return ret;
        }
        av_packet_unref(packet);
    }

    /* flush decoders, filters and encoders */
    for (std::size_t i = 0; i < ifmt_ctx->nb_streams; i++) {
        auto filter = filteringCtx.at(i);
        if (!filter || !*filter)
            continue;

        auto inputContext = streamCtx.at(i);

        log_debug("Flushing stream #{} decoder", i);

        /* flush decoder */
        ret = avcodec_send_packet(inputContext->dec_ctx, nullptr);
        if (ret < 0) {
            log_error("Flushing decoding failed");
            return ret;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(inputContext->dec_ctx, inputContext->dec_frame);
            if (ret == AVERROR_EOF)
                break;
            else if (ret < 0)
                return ret;

            inputContext->dec_frame->pts = inputContext->dec_frame->best_effort_timestamp;
            ret = filter->filterEncodeWriteFrame(inputContext->dec_frame, ofmt_ctx);
            if (ret < 0)
                return ret;
        }

        /* flush filter */
        ret = filter->filterEncodeWriteFrame(nullptr, ofmt_ctx);
        if (ret < 0) {
            log_error("Flushing filter failed");
            return ret;
        }

        /* flush encoder */
        ret = flush_encoder(i);
        if (ret < 0) {
            log_error("Flushing encoder failed");
            return ret;
        }
    }

    av_write_trailer(ofmt_ctx);

    if (ret < 0) {
        log_error("Error occurred: {}", av_err2str(ret));
    }
    return ret ? 1 : 0;
}

TranscodeInternalIOHandler::~TranscodeInternalIOHandler()
{
    av_packet_free(&packet);
    avformat_close_input(&ifmt_ctx);
    if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
}

std::unique_ptr<IOHandler> TranscodeInternalHandler::serveContent(
    const std::shared_ptr<TranscodingProfile>& profile,
    const fs::path& location,
    const std::shared_ptr<CdsObject>& obj,
    const std::string& group,
    const std::string& range)
{
    return std::make_unique<TranscodeInternalIOHandler>(location);
}

#endif
