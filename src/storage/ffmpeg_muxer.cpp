#include "ffmpeg_muxer.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
}
using namespace std;

/**
 * @brief FFmpeg 封装器上下文
 */
typedef struct {
    AVFormatContext* fmt_ctx;   /**< 封装上下文 */
    AVStream* video_st;         /**< 视频流 */
    int64_t first_timestamp;    /**< 第一帧时间戳（基准） */
    int is_init;                /**< 是否已初始化 */
} MuxerContext;

static MuxerContext g_ctx = {NULL, NULL, 0, 0};

/**
 * @brief 初始化 FFmpeg 封装器，创建 AVI 文件
 * @param filename 输出文件名
 * @param width    视频宽度
 * @param height   视频高度
 * @param fps      基准帧率
 * @return >=0=成功, <0=失败（-1:已初始化, -2:创建上下文失败, -3:创建流失败, -4:打开文件失败, -5:写头失败）
 */
int ffmpeg_muxer_init(const char* filename, int width, int height, int fps) {
    if (g_ctx.is_init) return -1;

    if (avformat_alloc_output_context2(&g_ctx.fmt_ctx, NULL, NULL, filename) < 0) {
        if (g_ctx.fmt_ctx) {
            avformat_free_context(g_ctx.fmt_ctx);
            g_ctx.fmt_ctx = NULL;
        }
        return -2;
    }

    g_ctx.video_st = avformat_new_stream(g_ctx.fmt_ctx, NULL);
    if (!g_ctx.video_st){
        if (g_ctx.fmt_ctx) {
            avformat_free_context(g_ctx.fmt_ctx);
            g_ctx.fmt_ctx = NULL;
        }
        return -3;
    }

    AVCodecParameters* par = g_ctx.video_st->codecpar;
    par->codec_type = AVMEDIA_TYPE_VIDEO;
    par->codec_id   = AV_CODEC_ID_MJPEG;
    par->width      = width;
    par->height     = height;
    par->format     = AV_PIX_FMT_YUVJ422P;

    g_ctx.video_st->time_base = {1, 1000000};

    if (!(g_ctx.fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&g_ctx.fmt_ctx->pb, filename, AVIO_FLAG_WRITE) < 0) {
            if (g_ctx.fmt_ctx) {
                avformat_free_context(g_ctx.fmt_ctx);
                g_ctx.fmt_ctx = NULL;
            }
            return -4;
        }
    }

    if (avformat_write_header(g_ctx.fmt_ctx, NULL) < 0) {
        if (g_ctx.fmt_ctx) {
            avformat_free_context(g_ctx.fmt_ctx);
            g_ctx.fmt_ctx = NULL;
        }
        return -5;
    }

    g_ctx.first_timestamp = -1;
    g_ctx.is_init = 1;
    return 0;
}

/**
 * @brief 写入一帧 MJPEG 图像到 AVI 文件
 * @param data         JPEG 数据指针
 * @param len          数据长度
 * @param timestamp_us 微秒时间戳
 * @return 0=成功, -1=失败
 *
 * 以第一帧的时间戳为基准，计算相对 PTS 写入封装器
 */
int ffmpeg_muxer_write(uint8_t* data, size_t len, uint64_t timestamp_us) {
    if (!g_ctx.is_init || !data) return -1;

    if (g_ctx.first_timestamp == -1) {
        g_ctx.first_timestamp = timestamp_us;
    }

    AVPacket pkt;
    av_init_packet(&pkt);
    
    pkt.data = data;
    pkt.size = len;
    pkt.stream_index = g_ctx.video_st->index;

    int64_t relative_pts = timestamp_us - g_ctx.first_timestamp;
    pkt.pts = av_rescale_q(relative_pts, {1, 1000000}, g_ctx.video_st->time_base);
    pkt.dts = pkt.pts;
    pkt.duration = 0;

    return av_interleaved_write_frame(g_ctx.fmt_ctx, &pkt);
}

/**
 * @brief 关闭 AVI 文件，释放 FFmpeg 资源
 *
 * 写入文件尾（trailer），关闭 IO，释放上下文
 */
void ffmpeg_muxer_close(void) {
    if (!g_ctx.is_init) return;

    av_write_trailer(g_ctx.fmt_ctx);

    if (!(g_ctx.fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&g_ctx.fmt_ctx->pb);
    }

    avformat_free_context(g_ctx.fmt_ctx);

    g_ctx.fmt_ctx = NULL;
    g_ctx.video_st = NULL;
    g_ctx.is_init = 0;
}