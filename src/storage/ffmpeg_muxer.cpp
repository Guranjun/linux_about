#include "ffmpeg_muxer.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
}

typedef struct {
    AVFormatContext* fmt_ctx;
    AVStream* video_st;
    int64_t first_timestamp; // 记录第一帧的时间戳作为基准
    int is_init;
} MuxerContext;

static MuxerContext g_ctx = {NULL, NULL, 0, 0};

int ffmpeg_muxer_init(const char* filename, int width, int height, int fps) {
    if (g_ctx.is_init) return -1;

    // 1. 初始化封装上下文
    if (avformat_alloc_output_context2(&g_ctx.fmt_ctx, NULL, NULL, filename) < 0) {
        return -2;
    }

    // 2. 创建视频流
    g_ctx.video_st = avformat_new_stream(g_ctx.fmt_ctx, NULL);
    if (!g_ctx.video_st) return -3;

    // 3. 配置流参数 (MJPEG 格式)
    AVCodecParameters* par = g_ctx.video_st->codecpar;
    par->codec_type = AVMEDIA_TYPE_VIDEO;
    par->codec_id   = AV_CODEC_ID_MJPEG;
    par->width      = width;
    par->height     = height;
    par->format     = AV_PIX_FMT_YUVJ422P; // MJPEG 常用像素格式

    // 设置时间基准：这里设为 1/1000000 (微秒级)，方便后续直接换算 V4L2 时间戳
    g_ctx.video_st->time_base = {1, 1000000}; 

    // 4. 打开文件
    if (!(g_ctx.fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&g_ctx.fmt_ctx->pb, filename, AVIO_FLAG_WRITE) < 0) {
            return -4;
        }
    }

    // 5. 写文件头
    if (avformat_write_header(g_ctx.fmt_ctx, NULL) < 0) {
        return -5;
    }

    g_ctx.first_timestamp = -1; // 标记尚未收到第一帧
    g_ctx.is_init = 1;
    return 0;
}

int ffmpeg_muxer_write(uint8_t* data, size_t len, int64_t timestamp_us) {
    if (!g_ctx.is_init || !data) return -1;

    // 如果是第一帧，记录基准时间
    if (g_ctx.first_timestamp == -1) {
        g_ctx.first_timestamp = timestamp_us;
    }

    AVPacket pkt;
    av_init_packet(&pkt);
    
    pkt.data = data;
    pkt.size = len;
    pkt.stream_index = g_ctx.video_st->index;

    // --- 核心逻辑：时间戳换算 ---
    // 计算相对于第一帧的偏移时间（微秒）
    int64_t relative_pts = timestamp_us - g_ctx.first_timestamp;

    // 将微秒时间换算成流的时间基 (从 1/1000000 换算到 AVI 容器内部的时间基)
    pkt.pts = av_rescale_q(relative_pts, {1, 1000000}, g_ctx.video_st->time_base);
    pkt.dts = pkt.pts;
    pkt.duration = 0; // MJPEG 帧间隔由 PTS 决定

    // 写入文件
    return av_interleaved_write_frame(g_ctx.fmt_ctx, &pkt);
}

void ffmpeg_muxer_close(void) {
    if (!g_ctx.is_init) return;

    // 写文件尾（必须有这一步，否则视频无法播放或无法拖动进度条）
    av_write_trailer(g_ctx.fmt_ctx);

    if (!(g_ctx.fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&g_ctx.fmt_ctx->pb);
    }

    avformat_free_context(g_ctx.fmt_ctx);

    g_ctx.fmt_ctx = NULL;
    g_ctx.video_st = NULL;
    g_ctx.is_init = 0;
}