/*
 * Copyright (c) 2012 Nikita Kniazev
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

#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/avstring.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <inttypes.h>
#include <stdio.h>
#include <getopt.h>

#define AV_TIME_BASE_SEC    (AVRational){1, 1}

struct PreProcessChanges {
    int width, height;
};

struct PreProcessSettings {
    struct PreProcessChanges source;
    struct PreProcessChanges result;

    int is_deinterlace;

    int is_resize;
    struct PreProcessChanges resize;

    int is_crop;
    struct PreProcessChanges crop;
};

static inline char *av_dict_get_val(AVDictionary *m, const char *key, const AVDictionaryEntry *prev, int flags)
{
    AVDictionaryEntry *e = av_dict_get(m, key, prev, flags);

    return e ? e->value : NULL;
}

static inline char *av_dict_get_fval(AVDictionary *m, const char *key, int flags)
{
    return av_dict_get_val(m, key, NULL, flags);
}

static inline char *av_dict_get_fcval(AVDictionary *m, const char *key)
{
    return av_dict_get_fval(m, key, AV_DICT_MATCH_CASE);
}

static int open_codec_context(int *stream_idx,
                              AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret;
    AVStream *st;
    AVCodecContext *dec_ctx = NULL;
    AVCodec *dec = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "Could not find %s stream in input file\n",
               av_get_media_type_string(type));
        return ret;
    } else {
        *stream_idx = ret;
        st = fmt_ctx->streams[*stream_idx];

        /* find decoder for the stream */
        dec_ctx = st->codec;
        dec = avcodec_find_decoder(dec_ctx->codec_id);
        if (!dec) {
            av_log(NULL, AV_LOG_FATAL, "Failed to find %s codec %s\n",
                   av_get_media_type_string(type), dec->name);
            return 1;
        }

        /* open codec */
        ret = avcodec_open2(dec_ctx, dec, NULL);
        if (ret < 0) {
            av_log(NULL, AV_LOG_FATAL, "Failed to open %s codec %s\n",
                   av_get_media_type_string(type), dec->name);
            return ret;
        }
    }

    return 0;
}

static void deinterlace_video_frame(AVFrame *frame)
{
    AVPicture pic_dst, pic_src;
    uint8_t *buf;

    /* create temporary picture */
    buf = av_malloc(avpicture_get_size(frame->format, frame->width,
                                                      frame->height));
    if (!buf)
        return;

    /* fill in the AVPicture fields */
    avpicture_fill(&pic_src, frame->data[0], frame->format,
                   frame->width, frame->height);
    avpicture_fill(&pic_dst, buf, frame->format,
                   frame->width, frame->height);

    if (avpicture_deinterlace(&pic_dst, &pic_src, frame->format,
                              frame->width, frame->height) < 0) {
        av_log(NULL, AV_LOG_WARNING, "Deinterlacing failed\n");
        av_free(buf);
    } else {
        /*for (int i = 0; i < AV_NUM_DATA_POINTERS; ++i) {
            printf("pointer:%p\t%p\t%p\t%p\n", frame->data[i], frame->base[i], pic_dst.data[i], buf);
            printf("        %d %d\n", frame->linesize[i], pic_dst.linesize[i]);
            //frame->data[i] = pic_dst.data[i];
            //frame->linesize[i] = pic_dst.linesize[i];
        }*/
        /* HACK: frame allocate buffer from base, but we only write to data */
        //av_free(frame->data[0]);
        frame->data[0] = buf;
    }
}

static void choose_pixel_fmt(AVCodecContext *enc_ctx, AVCodec *codec_dst)
{
    if (codec_dst && codec_dst->pix_fmts) {
        const enum PixelFormat *p = codec_dst->pix_fmts;
        int has_alpha = av_pix_fmt_descriptors
                [enc_ctx->pix_fmt].nb_components % 2 == 0;
        enum PixelFormat best = PIX_FMT_NONE;
        if (enc_ctx->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL) {
            if (enc_ctx->codec_id == CODEC_ID_MJPEG) {
                p = (const enum PixelFormat[])
                    { PIX_FMT_YUVJ420P, PIX_FMT_YUVJ422P, PIX_FMT_YUV420P,
                      PIX_FMT_YUV422P, PIX_FMT_NONE };
            } else if (enc_ctx->codec_id == CODEC_ID_LJPEG) {
                p = (const enum PixelFormat[])
                    { PIX_FMT_YUVJ420P, PIX_FMT_YUVJ422P, PIX_FMT_YUVJ444P,
                      PIX_FMT_YUV420P, PIX_FMT_YUV422P, PIX_FMT_YUV444P,
                      PIX_FMT_BGRA, PIX_FMT_NONE };
            }
        }
        for (; *p != PIX_FMT_NONE; p++) {
            best = avcodec_find_best_pix_fmt2(best, *p, enc_ctx->pix_fmt,
                                              has_alpha, NULL);
            if (*p == enc_ctx->pix_fmt)
                break;
        }
        if (*p == PIX_FMT_NONE) {
            if (enc_ctx->pix_fmt != PIX_FMT_NONE)
                av_log(NULL, AV_LOG_WARNING,
                       "Incompatible pixel format '%s' for codec '%s',"
                       " auto-selecting format '%s'\n",
                       av_pix_fmt_descriptors[enc_ctx->pix_fmt].name,
                       codec_dst->name,
                       av_pix_fmt_descriptors[best].name);
            enc_ctx->pix_fmt = best;
        }
    }
}

static AVFrame *aquire_frame(AVFormatContext *fmt_ctx, AVCodecContext *dec_ctx, int stream_idx, int64_t ts)
{
    int ret, got_frame;
    AVFrame *frame;
    AVPacket pkt;
    int64_t *best_effort_timestamp;

    /* initialize packet */
    av_init_packet(&pkt);

    /* allocate memory for frame */
    frame = avcodec_alloc_frame();
    if (!frame) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate frame\n");
        return NULL;
    }

    /* get pointer to best_effort_timestamp variable in AVFrame object */
    best_effort_timestamp = av_opt_ptr(avcodec_get_frame_class(), frame, "best_effort_timestamp");

    /* call av_read_frame until we got whole frame */
    do {
        /* reading packets until we get one from stream of interestead */
        do {
            ret = av_read_frame(fmt_ctx, &pkt);
            //if (pkt.stream_index == stream_idx)
            //    av_log(NULL, AV_LOG_DEBUG, "pkt s:%d/%d size:%d pts:%"PRId64" dts:%"PRId64" goal:%"PRId64"\n", pkt.stream_index, stream_idx, pkt.size, pkt.pts, pkt.dts, ts);
            if (ret < 0) {
                if (ret == (int) AVERROR_EOF)
                    av_log(NULL, AV_LOG_DEBUG, "av_read_frame EOF\n");
                return frame;
            }
        } while (pkt.stream_index != stream_idx);

        /* decode video packet to frame */
        ret = avcodec_decode_video2(dec_ctx, frame, &got_frame, &pkt);
        if (ret < 0) {
            av_log(NULL, AV_LOG_FATAL, "Error decoding video frame\n");
            av_free(frame);
            av_free_packet(&pkt);
            return NULL;
        } else if (!ret)
            av_log(NULL, AV_LOG_DEBUG, "No frame was decoded\n");
        else if (got_frame)
            av_log(NULL, AV_LOG_DEBUG, "Got frame size:%d bet:%"PRId64"\n", ret, *best_effort_timestamp);
    } while (!got_frame || *best_effort_timestamp < ts);

    av_free_packet(&pkt);

    return frame;
}

static int init_encoder_context(const char *filename, AVCodecContext *dec_ctx, AVFormatContext **format_ctx_ptr, AVCodecContext **enc_ctx_ptr)
{
    int ret = 0;
    AVOutputFormat *format;
    AVFormatContext *format_ctx;
    enum CodecID codec_id;
    AVCodec *enc;
    AVCodecContext *enc_ctx;

    format = av_guess_format(NULL, filename, NULL);
    if (!format) {
        av_log(NULL, AV_LOG_FATAL, "Could not guess output format\n");
        return AVERROR_MUXER_NOT_FOUND;
    }

    av_log(NULL, AV_LOG_DEBUG, "output format is %s\n", format->name);

    format_ctx = avformat_alloc_context();
    if (!format_ctx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    format_ctx->oformat = format;

    codec_id = av_guess_codec(format_ctx->oformat,
                              NULL, format_ctx->filename,
                              NULL, dec_ctx->codec_type);
    if (codec_id == CODEC_ID_NONE) {
        av_log(NULL, AV_LOG_FATAL, "Could not guess encoder\n");
        ret = AVERROR_ENCODER_NOT_FOUND;
        goto end;
    }

    enc = avcodec_find_encoder(codec_id);
    if (!enc) {
        av_log(NULL, AV_LOG_FATAL, "Could not found encoder\n");
        ret = AVERROR_ENCODER_NOT_FOUND;
        goto end;
    }

    enc_ctx = avcodec_alloc_context3(enc);
    if (!enc_ctx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    enc_ctx->codec = enc;

    /* fill codec context info */
    enc_ctx->time_base  = dec_ctx->time_base;
    enc_ctx->pix_fmt    = dec_ctx->pix_fmt;
    enc_ctx->width      = dec_ctx->width;
    enc_ctx->height     = dec_ctx->height;
    enc_ctx->codec_type = dec_ctx->codec_type;

    /*
    enc_ctx->qmin    = enc_ctx->qmax = 3;
    enc_ctx->mb_lmin = enc_ctx->lmin = enc_ctx->qmin * FF_QP2LAMBDA;
    enc_ctx->mb_lmax = enc_ctx->lmax = enc_ctx->qmax * FF_QP2LAMBDA;
    enc_ctx->flags |= CODEC_FLAG_QSCALE | CODEC_FLAG_INPUT_PRESERVED;
    */

    if (format_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        enc_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;

    av_log(NULL, AV_LOG_DEBUG, "output codect is %s\n", enc_ctx->codec->name);

    /* choose best pixel format from supported by codec */
    choose_pixel_fmt(enc_ctx, enc_ctx->codec);

    if (enc_ctx->pix_fmt == PIX_FMT_NONE) {
        av_log(NULL, AV_LOG_FATAL, "Video pixel format is unknown,"
                                   " stream cannot be encoded\n");
        ret = AVERROR(EPIPE);
        goto end;
    }

    *enc_ctx_ptr = enc_ctx;
    *format_ctx_ptr = format_ctx;

end:
    return ret;
}

static int encode_video_frame(AVFormatContext *format_ctx, AVCodecContext *enc_ctx, AVFrame *frame)
{
    int ret = 0;
    AVStream *stream;
    int video_outbuf_size;
    uint8_t *video_outbuf;

    stream = avformat_new_stream(format_ctx, NULL);
    if (!stream) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    //av_free(stream->codec);
    AVCodecContext *ctx_tmp = stream->codec;
    stream->codec = enc_ctx;

    ret = avformat_write_header(format_ctx, NULL);
    if (ret != 0) {
        char buf[64];
        av_strerror(ret, (char *)&buf, 64);
        av_log(NULL, AV_LOG_FATAL, "Failed to write header %s\n", buf);
        goto end;
    }

    /* create temporary picture */
    video_outbuf_size = avpicture_get_size(enc_ctx->pix_fmt,
                                           enc_ctx->width, enc_ctx->height);
    video_outbuf = av_malloc(video_outbuf_size);
    if (!video_outbuf) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ret = avcodec_encode_video(enc_ctx, video_outbuf, video_outbuf_size, frame);
    /* if zero size, it means the image was buffered */
    if (ret >= 0) {
        AVPacket pkt;
        av_init_packet(&pkt);

        pkt.pts = enc_ctx->coded_frame->pts;
        if (enc_ctx->coded_frame->key_frame)
            pkt.flags |= AV_PKT_FLAG_KEY;
        pkt.stream_index= stream->index;
        pkt.data = video_outbuf;
        pkt.size = ret;

        /* write the compressed frame in the media file */
        if (av_interleaved_write_frame(format_ctx, &pkt) != 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Can't write a frame\n");
            goto free_pkt;
        }

        ret = av_write_trailer(format_ctx);
        if (ret != 0) {
            av_log(NULL, AV_LOG_FATAL, "Failed to write trailer\n");
        }

        av_log(NULL, AV_LOG_DEBUG, "Sucess at encoding frame to %s\n",
                                    format_ctx->filename);

free_pkt:
        av_free_packet(&pkt);
    } else {
        av_log(NULL, AV_LOG_FATAL, "Encode failed\n");
        ret = -1;
    }
    av_free(video_outbuf);

end:
    stream->codec = ctx_tmp;

    return ret;
}

int resize_frame(AVFrame *src_frame, int width, int height)
{
    static struct SwsContext *sws_ctx = NULL;
    AVPicture pic;
    int ret;

    if (width < 1 && height < 1)
        return -1;
    else if (height < 1)
        height = (double) src_frame->height / src_frame->width * width;
    else if (width < 1)
        width = (double) src_frame->width / src_frame->height * height;

    sws_ctx = sws_getCachedContext(sws_ctx,
                                   src_frame->width, src_frame->height,
                                   src_frame->format,
                                   width, height, src_frame->format,
                                   SWS_BICUBIC, NULL, NULL, NULL);
    if (sws_ctx == NULL)
        return -1;

    avpicture_fill(&pic, *src_frame->data, src_frame->format, width, height);

    ret = sws_scale(sws_ctx, (const uint8_t * const*)
                    src_frame->data, src_frame->linesize,
                    0, src_frame->height, src_frame->data, pic.linesize);

    for (int i = 0; i < AV_NUM_DATA_POINTERS; ++i)
        src_frame->linesize[i] = pic.linesize[i];

    src_frame->width = width;
    src_frame->height = height;

    return ret;
}

static void pre_process_init(AVDictionary *options, struct PreProcessSettings *ppvfs)
{
    char *tmp;

    /* while all resizes & paddings, we should keep align
     * obtain info from output fromat context and encoder
     * use avcodec_align_dimensions2
     */

    ppvfs->result = ppvfs->source;

    /* deinterlace */
    ppvfs->is_deinterlace = !!av_dict_get_fcval(options, "deinterlace");

    /* crop */
    ppvfs->is_crop = 0;
    tmp = av_dict_get_fcval(options, "crop");
    if (tmp) {
        if (atoi(tmp) > 0) {
            ppvfs->crop.width  = (double) ppvfs->result.width
                                        / ppvfs->result.height * atoi(tmp);
            ppvfs->crop.height = atoi(tmp);
            ppvfs->result = ppvfs->crop;
            ppvfs->is_crop = 1;
        } else
            av_log(NULL, AV_LOG_ERROR, "Colud not crop frame"
                                       " to negative or zero resolution\n");
    }

    /* resize */
    ppvfs->is_resize = 0;
    if (av_dict_get_fval(options, "resize_", AV_DICT_IGNORE_SUFFIX)) {
        int *width  = &ppvfs->resize.width;
        int *height = &ppvfs->resize.height;
        tmp = av_dict_get_fcval(options, "resize_width");
        *width = tmp ? atoi(tmp) : -1;
        tmp = av_dict_get_fcval(options, "resize_height");
        *height = tmp ? atoi(tmp) : -1;

        if (*width < 1 && *height < 1)
            av_log(NULL, AV_LOG_ERROR, "Colud not resize frame"
                                       " to negative or zero resolution\n");
        else {
            if (*height < 1)
                *height = (double) ppvfs->result.height
                                 / ppvfs->result.width * *width;
            else if (*width < 1)
                *width = (double) ppvfs->result.width
                                 / ppvfs->result.height * *height;

            ppvfs->result = ppvfs->resize;
            ppvfs->is_resize = 1;
        }
    }
}

static int pre_process_video_frame(struct PreProcessSettings *ppvfs, AVFrame *frame)
{
    int ret = 0;

    /* deinterlace */
    if (ppvfs->is_deinterlace)
        deinterlace_video_frame(frame);

    /* crop */
    if (ppvfs->is_crop) {
        int width  = ppvfs->crop.width;
        int height = ppvfs->crop.height;

        //av_picture_crop((AVPicture *)frame, (AVPicture *)frame, dec_ctx->pix_fmt, width, height);
        /*av_picture_crop(&pic, &pic_tmp, dec_ctx->pix_fmt, width, height);
        for (int i = 0; i < AV_NUM_DATA_POINTERS; ++i)
            frame->linesize[i] = pic.linesize[i];*/

        frame->width = width;
        frame->height = height;
    }

    /* resize */
    if (ppvfs->is_resize) {
        int width  = ppvfs->resize.width;
        int height = ppvfs->resize.height;

        if (width > 0 || height > 0)
            resize_frame(frame, width, height);
    }

    return ret;
}

static inline int process_video(AVDictionary *options)
{
    int ret = 0;
    char *src_filename, *out_template;
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *dec_ctx = NULL;
    AVStream *stream = NULL;
    int stream_idx = -1;
    AVFormatContext *out_fmt_ctx = NULL;
    AVCodecContext *enc_ctx = NULL;
    struct PreProcessSettings ppvfs;
    AVDictionaryEntry *e = NULL;

    /* register all formats and codecs */
    av_register_all();

    /* take pointers to input file and output template */
    src_filename = av_dict_get_fcval(options, "input");
    out_template = av_dict_get_fcval(options, "output");

    /* open input file, and allocate format context */
    if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Could not open source file '%s'\n", src_filename);
        ret = AVERROR(EIO);
        goto end;
    }

    /* retrieve stream information */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Could not find stream information\n");
        ret = AVERROR(EIO);
        goto end;
    }

    /* dump input format information */
    if (av_log_get_level() >= AV_LOG_VERBOSE)
        av_dump_format(fmt_ctx, 0, src_filename, 0);

    /* find best stream and open codec contex */
    ret = open_codec_context(&stream_idx, fmt_ctx, AVMEDIA_TYPE_VIDEO);
    if (ret >= 0) {
        stream = fmt_ctx->streams[stream_idx];
        dec_ctx = stream->codec;

        av_log(NULL, AV_LOG_DEBUG, "Selected stream is %d\n", stream_idx);
    } else
        goto end;

    /* init encoder context */
    init_encoder_context(out_template, dec_ctx, &out_fmt_ctx, &enc_ctx);
    if (!out_fmt_ctx || !enc_ctx) {
        goto end;
    }

    /* calculate preprocess values for opening codec with right values */
    ppvfs.source.width  = dec_ctx->width;
    ppvfs.source.height = dec_ctx->height;
    pre_process_init(options, &ppvfs);
    enc_ctx->width  = ppvfs.result.width;
    enc_ctx->height = ppvfs.result.height;

    /* open encoder codec */
    ret = avcodec_open2(enc_ctx, enc_ctx->codec, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "Could not open codec %s\n", enc_ctx->codec->name);
        goto end;
    }

    /* iterate process procedure on every timestamp */
    while ((e = av_dict_get(options, "timestamp_", e, AV_DICT_IGNORE_SUFFIX))) {
        int64_t sec = atol(e->value), ts = sec;
        AVFrame *frame;

        /* calc frame from seconds */
        //ts = av_rescale_q(ts, AV_TIME_BASE_Q, stream->time_base);
        /* FIXME: check twice why we cant use AV_TIME_BASE_Q */
        ts = av_rescale_q(ts, AV_TIME_BASE_SEC, stream->time_base);
        if (ts > stream->duration) {
            av_log(NULL, AV_LOG_WARNING, "Timestamp %s is out of duration\n", e->value);
            continue;
        }

        /* add stream start offset if present */
        if (stream->start_time != (int64_t) AV_NOPTS_VALUE)
            ts += stream->start_time;

        /* fast seek to timestamp */
        av_log(NULL, AV_LOG_INFO, "seeking to %"PRId64" (%"PRId64"s)\n", ts, sec);
        /* TODO: only use seek for far jump. Look at keyframes (keyint, I-frame) */
        //ret = avformat_seek_file(fmt_ctx, stream_idx, INT64_MIN, ts, INT64_MAX, 0);
        ret = av_seek_frame(fmt_ctx, stream_idx, ts, AVSEEK_FLAG_BACKWARD);
        //ret = av_seek_frame(fmt_ctx, stream_idx, ts, AVSEEK_FLAG_ANY);
        if (ret < 0)
            av_log(NULL, AV_LOG_WARNING, "Error while seeking\n");

        /* TODO: call only when seek was success? */
        avcodec_flush_buffers(dec_ctx);

        /* aquire frame from input at given timestamp */
        frame = aquire_frame(fmt_ctx, dec_ctx, stream_idx, ts);
        if (frame) {
            /* preprocess video: deinterlace, crop, pad, resize and etc. */
            ret = pre_process_video_frame(&ppvfs, frame);

            /* expand template and set output filename  */
            snprintf(out_fmt_ctx->filename, sizeof(out_fmt_ctx->filename),
                     out_template, sec);

            /* encode frame */
            ret = encode_video_frame(out_fmt_ctx, enc_ctx, frame);

            /* HACK: Hack in deinterlace implementation */
            if (av_dict_get_fcval(options, "deinterlace"))
                av_free(frame->data[0]);

            av_free(frame);
        } else
            av_log(NULL, AV_LOG_ERROR, "Frame decoding was failed\n");
    }
end:
    if (out_fmt_ctx)
        avformat_free_context(out_fmt_ctx);
    if (enc_ctx) {
        avcodec_close(enc_ctx);
        av_free(enc_ctx);
    }

    if (fmt_ctx)
        avformat_close_input(&fmt_ctx);
    if (dec_ctx)
        avcodec_close(dec_ctx);

    return ret;
}

static void usage(const char *name)
{
    printf("usage: %s [options] <timestamps>\n"
           "options:\n"
           "-?, --help          this help\n"
           "-i, --input         input file\n"
           "-o, --output        output file or pattern (%%d will be replaced with timestamp)\n"
           "-d, --deinterlace   enables deinterlace\n"
           "-w, --width         set output width\n"
           "-h, --height        set output height\n"
           "-c, --crop          set crop height\n"
           , name);
}

int main(int argc, char **argv)
{
    int ret = EXIT_SUCCESS;
    AVDictionary *options = NULL;

    /* parse arguements passed to program */
    int c = -1, option_index = 0;
    const char *short_options = "?i:o:dv::w:h:c:";
    const struct option long_options[] = {
        {"help",        no_argument,       NULL, '?'},
        {"input",       required_argument, NULL, 'i'},
        {"output",      required_argument, NULL, 'o'},
        {"verbose",     optional_argument, NULL, 'v'},
        {"deinterlace", no_argument,       NULL, 'd'},
        {"width",       required_argument, NULL, 'w'},
        {"height",      required_argument, NULL, 'h'},
        {"crop",        required_argument, NULL, 'c'},
        {NULL,          0,                 NULL, 0}
    };

    while ((c = getopt_long(argc, argv, short_options,
        long_options, &option_index)) != -1) {
        switch (c) {
        case '?':
            usage(argv[0]);
            goto end;
        case 'i':
            av_dict_set(&options, "input", optarg, 0);
            break;
        case 'o':
            av_dict_set(&options, "output", optarg, 0);
            break;
        case 'w':
            av_dict_set(&options, "resize_width", optarg, 0);
            break;
        case 'h':
            av_dict_set(&options, "resize_height", optarg, 0);
            break;
        case 'c':
            av_dict_set(&options, "crop", optarg, 0);
            break;
        case 'd':
            av_dict_set(&options, "deinterlace", "", 0);
            break;
        case 'v':
            av_dict_set(&options, "verbose", optarg, 0);
            if (optarg) {
                av_log_set_level(atoll(optarg)*8-8);
                av_log(NULL, AV_LOG_INFO, "verbosity level changed to %d\n",
                       av_log_get_level());
            } else {
                av_log_set_level(AV_LOG_VERBOSE);
                av_log(NULL, AV_LOG_INFO, "verbose output was enabled\n");
            }
            break;
        default:
            av_log(NULL, AV_LOG_WARNING, "unknown option %s\n",
                    long_options[option_index].name);
            break;
        }
    }

    if (!av_dict_get_fcval(options, "input")) {
        av_log(NULL, AV_LOG_FATAL, "input file must be specified\n");
        ret = EXIT_FAILURE;
    }

    if (!av_dict_get_fcval(options, "output")) {
        av_log(NULL, AV_LOG_FATAL, "output file must be specified\n");
        ret = EXIT_FAILURE;
    }

    if (argc - optind) {
        for (int i = 0; argc > optind; ++optind) {
            char buf[64];
            sprintf(buf, "%ld", atol(argv[optind]));
            if (strcmp(argv[optind], buf) != 0) {
                av_log(NULL, AV_LOG_WARNING, "wrong timestamp %s\n", argv[optind]);
                continue;
            }
            sprintf(buf, "timestamp_%d", i++);
            av_dict_set(&options, (char *)&buf, argv[optind], 0);
        }
    } else {
        av_log(NULL, AV_LOG_FATAL, "at least one timestamp must be specified\n");
        ret = EXIT_FAILURE;
    }

    if (ret) {
        printf("call for help: %s --help\n", argv[0]);
        goto end;
    }

    /* process video with options */
    if (process_video(options) != 0) {
        av_log(NULL, AV_LOG_PANIC, "Processing video was failed\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    av_log(NULL, AV_LOG_INFO, "Done!\n");

end:
    if (options)
        av_dict_free(&options);

    return ret;
}

