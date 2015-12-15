/*
 * Copyright (C) 2013 Canonical Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authored by: Jim Hodapp <jim.hodapp@canonical.com>
 *              Ricardo Salveti de Araujo <ricardo.salveti@canonical.com>
 */

// Uncomment to enable verbose debug output
#define LOG_NDEBUG 0

#include <hybris/media/media_format_layer.h>
#include <hybris/media/media_codec_layer.h>
#include <hybris/media/media_codec_list.h>
#include <hybris/media/media_compatibility_layer.h>
#include "direct_media_test.h"

#include <utils/Errors.h>
#include <utils/Log.h>

#include <hybris/surface_flinger/surface_flinger_compatibility_layer.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace android;

// Proprietary color encoding format for the Nexus 4 hardware encoder
#define OMX_QCOM_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka 0x7FA30C03

static float DestWidth = 0.0, DestHeight = 0.0;
// Actual video dimmensions
static int Width = 0, Height = 0;

static GLfloat positionCoordinates[8];

MediaPlayerWrapper *player = NULL;

class VideoEncodeTest
{
    /*
     * See http://androidxref.com/4.2_r1/xref/frameworks/base/media/java/android/media/MediaCodecInfo.java
     */
    struct MediaCodecInfo
    {
        char *name = nullptr;
        size_t name_len = 0;
        bool is_encoder = false;
        uint32_t *color_formats = nullptr;
        size_t num_color_formats = 0;
        int profile = 0;
        int level = 0;

        enum ColorFormats
        {
            COLOR_FormatYUV420Planar = 19,
            COLOR_FormatYUV420PackedPlanar = 20,
            COLOR_FormatYUV420SemiPlanar = 21,
            COLOR_FormatYUV420PackedSemiPlanar = 39,
            COLOR_TI_FormatYUV420PackedSemiPlanar = 0x7f000100
        };
    };

    struct ByteBuffer
    {
        void clear()
        {
            for (size_t i=0; i<size; i++)
                data[i] = 0;
        }

        uint8_t get(size_t index)
        {
            if (data != nullptr && index < size)
                return data[index];

            return 0;
        }

        void put(const uint8_t *data, size_t size)
        {
            for (size_t i=0; i<size; i++)
            {
                this->data[i] = data[i];
                this->size = size;
            }
        }

        uint8_t *data;
        size_t size;
    };

    struct BufferInfo
    {
        int flags;
        int offset;
        int64_t presentation_time_us;
        int size;
    };

public:
    VideoEncodeTest()
        : m_mediaFormat(nullptr),
          m_largestColorDelta(0)
    {}

    virtual ~VideoEncodeTest()
    {
        // select_codec() mallocs memory, free it
        if (m_codecInfo.name)
            free(m_codecInfo.name);
        if (m_codecInfo.color_formats)
            free(m_codecInfo.color_formats);

        if (m_mediaFormat)
            media_format_destroy(m_mediaFormat);
    }

    void set_parameters(int32_t height, int32_t width, int32_t bitrate)
    {
        if ((width % 16) != 0 || (height % 16) != 0)
            ALOGW("WARNING: width or height not multiple of 16\n");

        m_mediaFormat = media_format_create_video_format(m_mimeType, width, height, 0, 0);
        bool ret = media_format_set_bitrate(m_mediaFormat, bitrate);
        if (!ret)
            ALOGW("Failed to set bitrate");
    }

    bool encode_decode_video_from_buffer()
    {
        bool ret = false;
        int err = OK;
        const bool isEncoder = true;
        MediaCodecDelegate encoder = nullptr;
        MediaCodecDelegate decoder = nullptr;
        select_codec(m_mimeType);
        const uint32_t color_format = select_color_format();

        if (!m_mediaFormat)
        {
            ALOGW("m_mediaFormat should not be NULL");
            ret = false;
            goto cleanup;
        }

        // This seems to be the size of each input buffer as reported on mako
        err = media_format_set_max_input_size(m_mediaFormat, 1384448);
        if (!err)
        {
            ALOGW("Failed to set max_input_size");
            ret = false;
            goto cleanup;
        }
        ALOGD("Setting color format: %d", color_format);
        err = media_format_set_color_format(m_mediaFormat, color_format);
        if (!err)
        {
            ALOGW("Failed to set color format");
            ret = false;
            goto cleanup;
        }
        err = media_format_set_stride(m_mediaFormat, media_format_get_width(m_mediaFormat));
        if (!err)
        {
            ALOGW("Failed to set stride");
            ret = false;
            goto cleanup;
        }
        err = media_format_set_slice_height(m_mediaFormat, media_format_get_height(m_mediaFormat));
        if (!err)
        {
            ALOGW("Failed to set slice_height");
            ret = false;
            goto cleanup;
        }
        err = media_format_set_framerate(m_mediaFormat, m_frameRate);
        if (!err)
        {
            ALOGW("Failed to set framerate");
            ret = false;
            goto cleanup;
        }
        // IFrame every 1 second
        err = media_format_set_iframe_interval(m_mediaFormat, 1);
        if (!err)
        {
            ALOGW("Failed to set iframe interval");
            ret = false;
            goto cleanup;
        }
        err = media_format_set_profile_idc(m_mediaFormat, m_codecInfo.profile);
        if (!err)
        {
            ALOGW("Failed to set profile");
            ret = false;
            goto cleanup;
        }
        err = media_format_set_level_idc(m_mediaFormat, m_codecInfo.level);
        if (!err)
        {
            ALOGW("Failed to set profile level");
            ret = false;
            goto cleanup;
        }

        encoder = media_codec_create_by_codec_type(m_mimeType, isEncoder);
        if (!encoder)
        {
            ALOGE("Failed to create encoder instance by mime type");
            ret = false;
            goto cleanup;
        }

        err = media_codec_configure(
                    encoder,
                    m_mediaFormat,
                    nullptr, /* SurfaceTextureClientHybris */
                    MEDIA_CODEC_CONFIGURE_FLAG_ENCODE);
        if (err != android::OK)
        {
            ALOGE("Failed to configure encoder correctly");
            ret = false;
            goto cleanup;
        }

        err = media_codec_start(encoder);
        if (err != android::OK)
        {
            ALOGE("Failed to start encoder");
            ret = false;
            goto cleanup;
        }

        decoder = media_codec_create_by_codec_type(m_mimeType, !isEncoder);
        if (!decoder)
        {
            ALOGE("Failed to create decoder instance by mime type");
            ret = false;
            goto cleanup;
        }

        ret = do_encode_decode_video_from_buffer(encoder, color_format, decoder);
        if (!ret)
        {
            ALOGE("Encoding and decoding video from buffer failed");
            ret = false;
            goto cleanup;
        }

cleanup:
        if (encoder)
        {
            media_codec_stop(encoder);
            media_codec_release(encoder);
        }

        if (decoder)
        {
            media_codec_stop(decoder);
            media_codec_release(decoder);
        }

        return true;
    }

private:
    bool select_codec(const char *mime)
    {
        if (mime == nullptr)
            return false;

        bool found_codec = false;

        const int32_t codec_count = media_codec_list_count_codecs();
        ALOGV("Number of codecs: %d", codec_count);

        for (int i=0; i<codec_count; i++)
        {
            const bool is_encoder = media_codec_list_is_encoder(i);
            if (!is_encoder)
                continue;

            const char *name_str = media_codec_list_get_codec_name(i);
            const int32_t n_supported_types = media_codec_list_get_num_supported_types(i);
            ALOGV("Encoder Codec '%s' has %d supported types\n", name_str, n_supported_types);
            for (int j=0; j<n_supported_types; j++)
            {
                const size_t len = media_codec_list_get_nth_supported_type_len(i, j);
                char supported_type_str[len];
                const int32_t err = media_codec_list_get_nth_supported_type
                        (i, supported_type_str, j);
                supported_type_str[len] = '\0';
                ALOGV("mime: %s, supported_type_str: %s\n", mime, supported_type_str);
                if (!err && strstr(mime, supported_type_str) != nullptr)
                {
                    ALOGD("**** We found a matching codec for the mimetype '%s': %s", mime, name_str);
                    size_t name_len = strlen(name_str);
                    m_codecInfo.name = (char*)malloc(name_len);
                    m_codecInfo.name_len = name_len;
                    strncpy(m_codecInfo.name, name_str, name_len);
                    m_codecInfo.is_encoder = true;
                    // Only interested in the first matching codec, so no need to search further
                    found_codec = true;
                    break;
                }
            }

            bool ret = get_color_formats(i, mime);
            if (!ret)
                return false;

            ret = get_codec_profile_and_level(i, mime);
            if (!ret)
                return false;

            if (found_codec)
                break;
        }

        return found_codec;
    }

    bool get_color_formats(int index, const char *mime)
    {
        const size_t num_colors = media_codec_list_get_num_color_formats(index, mime);
        m_codecInfo.color_formats = (uint32_t*)malloc(num_colors);
        const int ret = media_codec_list_get_codec_color_formats(index, mime, m_codecInfo.color_formats);
        if (ret != OK)
        {
            ALOGE("Failed to get the codec color formats\n");
            return false;
        }

        ALOGV("Number of color formats: %d\n", num_colors);
        for (size_t i=0; i<num_colors; i++)
            ALOGI("Color format: 0x%X\n", m_codecInfo.color_formats[i]);

        m_codecInfo.num_color_formats = num_colors;

        return true;
    }

    uint32_t select_color_format()
    {
        ALOGV("m_codecInfo.color_formats: %p\n", m_codecInfo.color_formats);
        ALOGV("m_codecInfo.num_color_formats: %u\n", m_codecInfo.num_color_formats);
        if (!m_codecInfo.color_formats || m_codecInfo.num_color_formats == 0)
        {
            ALOGW("No loaded color formats to select from");
            return 0;
        }

        for (size_t i=0; i<m_codecInfo.num_color_formats; i++)
        {
            // Use the first known color format from the list of supported formats
            if (is_recognized_format(m_codecInfo.color_formats[i]))
            {
                ALOGD("Selecting color format 0x%X\n", m_codecInfo.color_formats[i]);
                return m_codecInfo.color_formats[i];
            }
        }

        return 0;
    }

    bool get_codec_profile_and_level(int index, const char *mime)
    {
        const size_t num_profiles = media_codec_list_get_num_profile_levels(index, mime);
        ALOGV("mime '%s' has %d profiles\n", mime, num_profiles);

        for (uint32_t i=0; i<num_profiles; i++)
        {
            profile_level pro_level;
            int ret = media_codec_list_get_nth_codec_profile_level(index, mime, &pro_level, i);
            if (ret != OK)
            {
                ALOGE("Failed to get codec profile(s) and level(s)\n");
                return false;
            }
            ALOGV("(%u) Profile %u, Level %u\n", i, pro_level.profile, pro_level.level);
        }

        m_codecInfo.profile = 1;
        m_codecInfo.level = 2048;

        return true;
    }

    bool is_recognized_format(uint32_t color_format)
    {
        switch (color_format)
        {
            // These are the formats we know how to handle for this test (and they are semi-planar)
            case MediaCodecInfo::ColorFormats::COLOR_FormatYUV420Planar:
            case MediaCodecInfo::ColorFormats::COLOR_FormatYUV420PackedPlanar:
            case MediaCodecInfo::ColorFormats::COLOR_FormatYUV420SemiPlanar:
            case MediaCodecInfo::ColorFormats::COLOR_FormatYUV420PackedSemiPlanar:
            case MediaCodecInfo::ColorFormats::COLOR_TI_FormatYUV420PackedSemiPlanar:
            case OMX_QCOM_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka: /* Nexus 4 */
                return true;
            default:
                return false;
        }
    }

    ByteBuffer *get_input_buffers(MediaCodecDelegate delegate)
    {
        const size_t num_input_bufs = media_codec_get_input_buffers_size(delegate);
        if (num_input_bufs == 0)
        {
            ALOGE("Zero encoder/decoder input buffers available");
            return nullptr;
        }

        ByteBuffer *input_bufs = (ByteBuffer*)malloc(num_input_bufs*sizeof(ByteBuffer));
        for (size_t i=0; i<num_input_bufs; i++)
        {
            input_bufs[i].data = media_codec_get_nth_input_buffer(delegate, i);
            input_bufs[i].size = media_codec_get_nth_input_buffer_capacity(delegate, i);
            ALOGD("input buffer[%d] size: %d", i, input_bufs[i].size);
        }

        return input_bufs;
    }

    ByteBuffer *get_output_buffers(MediaCodecDelegate delegate)
    {
        const size_t num_output_bufs = media_codec_get_output_buffers_size(delegate);
        if (num_output_bufs == 0)
        {
            ALOGE("Zero encoder/decoder output buffers available");
            return nullptr;
        }

        ByteBuffer *output_bufs = (ByteBuffer*)malloc(num_output_bufs*sizeof(ByteBuffer));
        for (size_t i=0; i<num_output_bufs; i++)
        {
            output_bufs[i].data = media_codec_get_nth_output_buffer(delegate, i);
            output_bufs[i].size = media_codec_get_nth_output_buffer_capacity(delegate, i);
            ALOGD("output buffer[%d] size: %d", i, output_bufs[i].size);
        }

        return output_bufs;
    }

    int64_t compute_presentation_time(int frame_index)
    {
        return 132 + frame_index * 1000000 / m_frameRate;
    }

    /**
     * Returns true if the actual color value is close to the expected color value.  Updates
     * mLargestColorDelta.
     */
    bool is_color_close(int actual, int expected)
    {
        const int MAX_DELTA = 8;
        const int delta = abs(actual - expected);
        if (delta > m_largestColorDelta) {
            m_largestColorDelta = delta;
        }
        return (delta <= MAX_DELTA);
    }

    /**
     * Generates data for frame N into the supplied buffer.  We have an 8-frame animation
     * sequence that wraps around.  It looks like this:
     * <pre>
     *   0 1 2 3
     *   7 6 5 4
     * </pre>
     * We draw one of the eight rectangles and leave the rest set to the zero-fill color.
     */
    void generate_frame(int frame_index, uint32_t color_format, uint8_t *frame_data, size_t frame_data_size)
    {
        const int32_t height = media_format_get_height(m_mediaFormat);
        const int32_t width = media_format_get_width(m_mediaFormat);
        const int HALF_WIDTH = width / 2;
        const bool semi_planar = is_recognized_format(color_format);

        // Set to zero.  In YUV this is a dull green.
        for (size_t i=0; i<frame_data_size; i++)
            frame_data[i] = 0;

        int start_x, start_y, count_x, count_y;
        frame_index %= 8;
        //frame_index = (frame_index / 8) % 8;    // use this instead for debug -- easier to see
        if (frame_index < 4)
        {
            start_x = frame_index * (width / 4);
            start_y = 0;
        }
        else
        {
            start_x = (7 - frame_index) * (width / 4);
            start_y = height / 2;
        }
        for (int y = start_y + (height/2) - 1; y >= start_y; --y)
        {
            for (int x = start_x + (width/4) - 1; x >= start_x; --x)
            {
                if (semi_planar)
                {
                    // full-size Y, followed by UV pairs at half resolution
                    // e.g. Nexus 4 OMX.qcom.video.encoder.avc COLOR_FormatYUV420SemiPlanar
                    // e.g. Galaxy Nexus OMX.TI.DUCATI1.VIDEO.H264E
                    //        OMX_TI_COLOR_FormatYUV420PackedSemiPlanar
                    frame_data[y * width + x] = (uint8_t) TEST_Y;
                    if ((x & 0x01) == 0 && (y & 0x01) == 0) {
                        frame_data[width*height + y * HALF_WIDTH + x] = (uint8_t) TEST_U;
                        frame_data[width*height + y * HALF_WIDTH + x + 1] = (uint8_t) TEST_V;
                    }
                }
                else
                {
                    // full-size Y, followed by quarter-size U and quarter-size V
                    // e.g. Nexus 10 OMX.Exynos.AVC.Encoder COLOR_FormatYUV420Planar
                    // e.g. Nexus 7 OMX.Nvidia.h264.encoder COLOR_FormatYUV420Planar
                    frame_data[y * width + x] = (uint8_t) TEST_Y;
                    if ((x & 0x01) == 0 && (y & 0x01) == 0) {
                        frame_data[width*height + (y/2) * HALF_WIDTH + (x/2)] = (uint8_t) TEST_U;
                        frame_data[width*height + HALF_WIDTH * (height / 2) +
                                  (y/2) * HALF_WIDTH + (x/2)] = (uint8_t) TEST_V;
                    }
                }
            }
        }
    }

    /**
     * Performs a simple check to see if the frame is more or less right.
     * <p>
     * See {@link #generateFrame} for a description of the layout.  The idea is to sample
     * one pixel from the middle of the 8 regions, and verify that the correct one has
     * the non-background color.  We can't know exactly what the video encoder has done
     * with our frames, so we just check to see if it looks like more or less the right thing.
     *
     * @return true if the frame looks good
     */
    bool check_frame(int frame_index, const MediaFormat &format, ByteBuffer &frame_data)
    {
        // Check for color formats we don't understand.  There is no requirement for video
        // decoders to use a "mundane" format, so we just give a pass on proprietary formats.
        // e.g. Nexus 4 0x7FA30C03 OMX_QCOM_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka
        const int color_format = media_format_get_color_format(format);
        if (!is_recognized_format(color_format))
        {
            ALOGD("Unable to check frame contents for color_format=%d", color_format);
            return true;
        }
        const int m_width = media_format_get_width(m_mediaFormat);
        const int m_height = media_format_get_height(m_mediaFormat);
        bool frame_failed = false;
        const bool semi_planar = is_recognized_format(color_format);
        int width = media_format_get_width(format);
        int height = media_format_get_height(format);
        int half_width = width / 2;
        int crop_left = media_format_get_crop_left(format);
        int crop_right = media_format_get_crop_right(format);
        const int crop_top = media_format_get_crop_top(format);
        const int crop_bottom = media_format_get_crop_bottom(format);
        const int crop_width = media_format_get_crop_width(format);
        const int crop_height = media_format_get_crop_height(format);
        assert(width == crop_width);
        assert(height == crop_height);
        for (int i = 0; i < 8; i++)
        {
            int x, y;
            if (i < 4)
            {
                x = i * (m_width / 4) + (m_width / 8);
                y = m_height / 4;
            }
            else
            {
                x = (7 - i) * (m_width / 4) + (m_width / 8);
                y = (m_height * 3) / 4;
            }
            y += crop_top;
            x += crop_left;
            int test_y, test_u, test_v;
            if (semi_planar)
            {
                // Galaxy Nexus uses OMX_TI_COLOR_FormatYUV420PackedSemiPlanar
                test_y = frame_data.get(y * width + x) & 0xff;
                test_u = frame_data.get(width*height + 2*(y/2) * half_width + 2*(x/2)) & 0xff;
                test_v = frame_data.get(width*height + 2*(y/2) * half_width + 2*(x/2) + 1) & 0xff;
            }
            else
            {
                // Nexus 10, Nexus 7 use COLOR_FormatYUV420Planar
                test_y = frame_data.get(y * width + x) & 0xff;
                test_u = frame_data.get(width*height + (y/2) * half_width + (x/2)) & 0xff;
                test_v = frame_data.get(width*height + half_width * (height / 2) +
                        (y/2) * half_width + (x/2)) & 0xff;
            }
            int exp_y, exp_u, exp_v;
            if (i == frame_index % 8)
            {
                // colored rect
                exp_y = TEST_Y;
                exp_u = TEST_U;
                exp_v = TEST_V;
            }
            else
            {
                // should be our zeroed-out buffer
                exp_y = exp_u = exp_v = 0;
            }
            if (!is_color_close(test_y, exp_y) ||
                    !is_color_close(test_u, exp_u) ||
                    !is_color_close(test_v, exp_v))
            {
                ALOGW("Bad frame  %d (rect=%d: yuv=%d, %d, %d vs. expected %d, %d, %d)",
                        frame_index, i, test_y, test_u, test_v, exp_y, exp_u, exp_v);
                frame_failed = true;
            }
        }
        return !frame_failed;
    }

    void print_format(const MediaFormat format)
    {
        ALOGD("mime: %s", media_format_get_mime(format));
        ALOGD("duration_us: %lld", media_format_get_duration_us(format));
        ALOGD("height: %d", media_format_get_height(format));
        ALOGD("width: %d", media_format_get_width(format));
        ALOGD("max_input_size: %d", media_format_get_max_input_size(format));
        ALOGD("bitrate: %d", media_format_get_bitrate(format));
        ALOGD("bitrate_mode: %d", media_format_get_bitrate_mode(format));
        ALOGD("framerate: %d", media_format_get_framerate(format));
        ALOGD("iframe_interval: %d", media_format_get_iframe_interval(format));
        ALOGD("stride: %d", media_format_get_stride(format));
        ALOGD("slice_height: %d", media_format_get_slice_height(format));
        ALOGD("color_format: %d", media_format_get_color_format(format));
    }

    bool do_encode_decode_video_from_buffer(MediaCodecDelegate encoder, uint32_t encoder_color_format,
            MediaCodecDelegate decoder)
    {
        ByteBuffer *encoder_input_buffers = nullptr;
        ByteBuffer *encoder_output_buffers = nullptr;
        ByteBuffer *decoder_input_buffers = nullptr;
        ByteBuffer *decoder_output_buffers = nullptr;
        size_t num_encoder_input_bufs = 0, num_encoder_output_bufs = 0;

        encoder_input_buffers = get_input_buffers(encoder);
        if (!encoder_input_buffers)
        {
            ALOGE("Zero encoder input buffers available");
            return false;
        }
        encoder_output_buffers = get_output_buffers(encoder);
        if (!encoder_output_buffers)
        {
            ALOGE("Zero encoder output buffers available");
            return false;
        }

        MediaCodecBufferInfo *buf_info = new MediaCodecBufferInfo;
        MediaFormat decoder_output_format = nullptr;
        int generate_index = 0;
        int check_index = 0;
        int bad_frames = 0;
        bool decoder_configured = false;
        const int32_t height = media_format_get_height(m_mediaFormat);
        const int32_t width = media_format_get_width(m_mediaFormat);
        // The size of a frame of video data, in the formats we handle, is stride*sliceHeight
        // for Y, and (stride/2)*(sliceHeight/2) for each of the Cb and Cr channels.  Application
        // of algebra and assuming that stride==width and sliceHeight==height yields:
        const size_t frame_data_size = width * height * 3 / 2;
        uint8_t frame_data[frame_data_size];
        // Just out of curiosity.
        long raw_size = 0;
        long encoded_size = 0;

        // Loop until the output side is done
        bool input_done = false;
        bool encoder_done = false;
        bool output_done = false;
        const int TIMEOUT_USEC = 10000;

        int ret = OK;
        bool status = false;

        while (!output_done)
        {
            // If we're not done submitting frames, generate a new one and submit it.  By
            // doing this on every loop we're working to ensure that the encoder always has
            // work to do.
            //
            // We don't really want a timeout here, but sometimes there's a delay opening
            // the encoder device, so a short timeout can keep us from spinning hard.
            if (!input_done)
            {
                size_t input_buf_index = 0;
                ret = media_codec_dequeue_input_buffer(encoder, &input_buf_index, TIMEOUT_USEC);
                if (ret == OK)
                {
                    ALOGV("input_buf_index = %u", input_buf_index);
                    const int64_t pts_usec = compute_presentation_time(generate_index);
                    if (generate_index == m_numFrames) {
                        // Send an empty frame with the end-of-stream flag set.  If we set EOS
                        // on a frame with data, that frame data will be ignored, and the
                        // output will be short one frame.
                        buf_info->index = input_buf_index;
                        buf_info->offset = 0;
                        buf_info->size = 0;
                        buf_info->presentation_time_us = pts_usec;
                        buf_info->flags = MEDIA_CODEC_BUFFER_FLAG_END_OF_STREAM;
                        ret = media_codec_queue_input_buffer(encoder, buf_info);
                        if (ret != OK)
                            ALOGW("Failed to queue input buffer to encoder");

                        input_done = true;
                        ALOGV("Sent input EOS (with zero-length frame)");
                    }
                    else
                    {
                        // Get a real video frame
                        generate_frame(generate_index, encoder_color_format, frame_data, frame_data_size);
                        ByteBuffer input_buf = encoder_input_buffers[input_buf_index];

                        input_buf.clear();
                        // Copy video frame into input buffer
                        input_buf.put(frame_data, frame_data_size);

                        buf_info->index = input_buf_index;
                        buf_info->offset = 0;
                        buf_info->size = width * height * 3 / 2;
                        buf_info->presentation_time_us = pts_usec;
                        buf_info->flags = 0;
                        ret = media_codec_queue_input_buffer(encoder, buf_info);
                        if (ret != OK)
                            ALOGW("Failed to queue input buffer to encoder");

                        ALOGV("==============================================================");
                        ALOGV("Submitted frame %d to encoder (buf_info->index: %d, pts: %lld)",
                                generate_index, buf_info->index, buf_info->presentation_time_us);
                        ALOGV("==============================================================");
                    }
                    ++generate_index;
                }
                else
                {
                    // Either all in use, or we timed out during initial setup
                    ALOGD("Input buffer not available, ret = %d", ret);
                }
            }

            // Check for output from the encoder.  If there's no output yet, we either need to
            // provide more input, or we need to wait for the encoder to work its magic.  We
            // can't actually tell which is the case, so if we can't get an output buffer right
            // away we loop around and see if it wants more input.
            //
            // Once we get EOS from the encoder, we don't need to do this anymore.
            if (!encoder_done)
            {
                const int encoder_status = media_codec_dequeue_output_buffer(encoder, buf_info, TIMEOUT_USEC);
                if (encoder_status == MediaCodecStatus::INFO_TRY_AGAIN_LATER)
                {
                    // no output available yet
                    ALOGD("No output from encoder available");
                }
                else if (encoder_status == MediaCodecStatus::INFO_OUTPUT_BUFFERS_CHANGED)
                {
                    // not expected for an encoder
                    encoder_output_buffers = get_output_buffers(encoder);
                    ALOGD("Encoder output buffers changed");
                }
                else if (encoder_status == MediaCodecStatus::INFO_OUTPUT_FORMAT_CHANGED)
                {
                    // not expected for an encoder
                    MediaFormat new_format = media_codec_get_output_format(encoder);
                    ALOGD("Encoder output format changed: ");
                    print_format(new_format);
                }
                else if (encoder_status < 0)
                {
                    ALOGE("Unexpected result from media_codec_dequeue_output_buffer: %d", encoder_status);
                    status = false;
                    break;
                }
                else
                { // encoder_status >= 0
                    ByteBuffer encoded_data = encoder_output_buffers[encoder_status];
                    if (!encoded_data.data)
                    {
                        ALOGE("encoder_output_buffer %d was NULL", encoder_status);
                        status = false;
                        break;
                    }
                    encoded_size += buf_info->size;
                    encoded_data.size += buf_info->size;

                    if ((buf_info->flags & MEDIA_CODEC_BUFFER_FLAG_CODEC_CONFIG) != 0)
                    {
                        // Codec config info.  Only expected on first packet.  One way to
                        // handle this is to manually stuff the data into the MediaFormat
                        // and pass that to configure().  We do that here to exercise the API.
                        assert(!decoder_configured);
                        MediaFormat format = media_format_create_video_format(m_mimeType, width, height, 0, 0);
                        bool success = media_format_set_byte_buffer(format, "csd-0", encoded_data.data, encoded_size);
                        if (!success)
                        {
                            ALOGE("Failed to set 'csd-0' byte buffer in the encoder MediaFormat");
                            status = false;
                            break;
                        }
                        ret = media_codec_configure(decoder, format, nullptr, 0);
                        if (ret != OK)
                        {
                            ALOGE("Failed to configure the decoder");
                            status = false;
                            break;
                        }
                        ret = media_codec_start(decoder);
                        if (ret != OK)
                        {
                            ALOGE("Failed to start the decoder");
                            status = false;
                            break;
                        }
                        decoder_input_buffers = get_input_buffers(decoder);
                        decoder_output_buffers = get_output_buffers(decoder);
                        decoder_configured = true;
                        ALOGD("Decoder configured (%d bytes)", buf_info->size);
                    }
                    else
                    {
                        // Get a decoder input buffer, blocking until it's available.
                        assert(decoder_configured);
                        size_t input_buf_index = 0;
                        ret = media_codec_dequeue_input_buffer(decoder, &input_buf_index, TIMEOUT_USEC);
                        if (ret != OK && ret != MediaCodecStatus::INFO_TRY_AGAIN_LATER)
                        {
                            ALOGE("Failed to dequeue input buffer from decoder: %d", ret);
                            status = false;
                            break;
                        }
                        ByteBuffer input_buf = decoder_input_buffers[input_buf_index];
                        input_buf.clear();
                        input_buf.put(encoded_data.data, encoded_size);
                        ret = media_codec_queue_input_buffer(decoder, buf_info);
                        if (ret != OK && ret != -22)
                        {
                            ALOGE("Failed to queue input buffer to decoder, ret: %d", ret);
                            status = false;
                            break;
                        }
                        encoder_done = (buf_info->flags & MEDIA_CODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
                        ALOGD("Passed %d bytes to decoder %s", buf_info->size, (encoder_done ? " (EOS)" : ""));
                    }
                    ret = media_codec_release_output_buffer(encoder, encoder_status, false);
                    if (ret != OK)
                    {
                        ALOGE("Failed to release encoder output buffer #%d, ret: %d", encoder_status, ret);
                        status = false;
                        break;
                    }
                }
            }

            // Check for output from the decoder.  We want to do this on every loop to avoid
            // the possibility of stalling the pipeline.  We use a short timeout to avoid
            // burning CPU if the decoder is hard at work but the next frame isn't quite ready.
            //
            // If we're decoding to a Surface, we'll get notified here as usual but the
            // ByteBuffer references will be null.  The data is sent to Surface instead.
            if (decoder_configured)
            {
                ALOGV("------- Decoder is configured, trying to dequeue output buffer");
                int decoder_status = media_codec_dequeue_output_buffer(decoder, buf_info, TIMEOUT_USEC);
                ALOGV("decoder_status: %d", decoder_status);
                if (decoder_status == MediaCodecStatus::INFO_TRY_AGAIN_LATER)
                {
                    // no output available yet
                    ALOGD("No output from decoder available");
                }
                else if (decoder_status == MediaCodecStatus::INFO_OUTPUT_BUFFERS_CHANGED)
                {
                    // The storage associated with the direct ByteBuffer may already be unmapped,
                    // so attempting to access data through the old output buffer array could
                    // lead to a native crash.
                    ALOGD("Decoder output buffers changed");
                    decoder_output_buffers = get_output_buffers(decoder);
                }
                else if (decoder_status == MediaCodecStatus::INFO_OUTPUT_FORMAT_CHANGED)
                {
                    // this happens before the first frame is returned
                    decoder_output_format = media_codec_get_output_format(decoder);
                    ALOGD("Decoder output format changed: ");
                    print_format(decoder_output_format);
                }
                else if (decoder_status < 0)
                {
                    ALOGE("Unexpected result from deocder.dequeueOutputBuffer: %d", decoder_status);
                    status = false;
                    break;
                }
                else
                {  // decoderStatus >= 0
                    ByteBuffer output_frame = decoder_output_buffers[decoder_status];

                    ALOGD("Getting raw video frame from decoder output, buf_info->size: %d", buf_info->size);
                    raw_size += buf_info->size;
                    if (buf_info->size == 0)
                    {
                        ALOGD("Got empty frame");
                    }
                    else
                    {
                        const int idx = buf_info->index;
                        ALOGD("======================================");
                        ALOGD("Decoded chunk, checking frame %d", check_index);
                        ALOGD("======================================");
                        if (compute_presentation_time(idx) != buf_info->presentation_time_us)
                        {
                            ALOGE("Wrong timestamp: %lld vs %lld", compute_presentation_time(idx), buf_info->presentation_time_us);
                            //status = false;
                            //break;
                        }
                        if (!check_frame(idx, decoder_output_format, output_frame))
                        {
                            ALOGW("Bad frame, content does not agree with original (expected on Nexus 4)");
                            bad_frames++;
                        }

                        ++check_index;
                    }

                    if ((buf_info->flags & MEDIA_CODEC_BUFFER_FLAG_END_OF_STREAM) != 0
                            or input_done)
                    {
                        ALOGD("Output EOS");
                        output_done = true;
                    }

                    ret = media_codec_release_output_buffer(decoder, decoder_status, false);
                    if (ret != OK)
                    {
                        ALOGE("Failed to release decoder output buffer #%d, ret: %d", decoder_status, ret);
                        status = false;
                        break;
                    }
                }
            } // decoder_configured
            ALOGD("------ End of encoder/decoder loop iteration");
        } // while

        if (check_index == m_numFrames)
            status = true;

        // Cleanup
        if (buf_info)
            delete buf_info;
        if (encoder_input_buffers)
            free(encoder_input_buffers);
        if (encoder_output_buffers)
            free(encoder_output_buffers);
        if (decoder_input_buffers)
            free(decoder_input_buffers);
        if (decoder_output_buffers)
            free(decoder_output_buffers);

        return status;
    }

    const char *m_mimeType = "video/avc";
    // 30 FPS
    const int m_frameRate = 30;
    const int m_numFrames = 30;
    const int TEST_Y = 120;                  // YUV values for colored rect
    const int TEST_U = 160;
    const int TEST_V = 200;
    const int TEST_R0 = 0;                   // RGB equivalent of {0,0,0}
    const int TEST_G0 = 136;
    const int TEST_B0 = 0;
    const int TEST_R1 = 236;                 // RGB equivalent of {120,160,200}
    const int TEST_G1 = 50;
    const int TEST_B1 = 186;
    MediaCodecInfo m_codecInfo;
    MediaFormat m_mediaFormat;
    // largest color component delta seen (i.e. actual vs. expected)
    int m_largestColorDelta;
};

void calculate_position_coordinates()
{
    // Assuming cropping output for now
    float x = 1, y = 1;

    // Black borders
    x = float(Width / DestWidth);
    y = float(Height / DestHeight);

    // Make the larger side be 1
    if (x > y) {
        y /= x;
        x = 1;
    } else {
        x /= y;
        y = 1;
    }

    positionCoordinates[0] = -x;
    positionCoordinates[1] = y;
    positionCoordinates[2] = -x;
    positionCoordinates[3] = -y;
    positionCoordinates[4] = x;
    positionCoordinates[5] = -y;
    positionCoordinates[6] = x;
    positionCoordinates[7] = y;
}

    WindowRenderer::WindowRenderer(int width, int height)
: mThreadCmd(CMD_IDLE)
{
    createThread(threadStart, this);
}

WindowRenderer::~WindowRenderer()
{
}

int WindowRenderer::threadStart(void* self)
{
    ((WindowRenderer *)self)->glThread();
    return 0;
}

void WindowRenderer::glThread()
{
    printf("%s\n", __PRETTY_FUNCTION__);

    Mutex::Autolock autoLock(mLock);
}

struct ClientWithSurface
{
    SfClient* client;
    SfSurface* surface;
};

ClientWithSurface client_with_surface(bool setup_surface_with_egl)
{
    ClientWithSurface cs = ClientWithSurface();

    cs.client = sf_client_create();

    if (!cs.client) {
        printf("Problem creating client ... aborting now.");
        return cs;
    }

    static const size_t primary_display = 0;

    DestWidth = sf_get_display_width(primary_display);
    DestHeight = sf_get_display_height(primary_display);
    printf("Primary display width: %f, height: %f\n", DestWidth, DestHeight);

    SfSurfaceCreationParameters params = {
        0,
        0,
        (int) DestWidth,
        (int) DestHeight,
        -1, //PIXEL_FORMAT_RGBA_8888,
        15000,
        0.5f,
        setup_surface_with_egl, // Do not associate surface with egl, will be done by camera HAL
        "MediaCompatLayerTestSurface"
    };

    cs.surface = sf_surface_create(cs.client, &params);

    if (!cs.surface) {
        printf("Problem creating surface ... aborting now.");
        return cs;
    }

    sf_surface_make_current(cs.surface);

    return cs;
}

struct RenderData
{
    static const char *vertex_shader()
    {
        return
            "attribute vec4 a_position;                                  \n"
            "attribute vec2 a_texCoord;                                  \n"
            "uniform mat4 m_texMatrix;                                   \n"
            "varying vec2 v_texCoord;                                    \n"
            "varying float topDown;                                      \n"
            "void main()                                                 \n"
            "{                                                           \n"
            "   gl_Position = a_position;                                \n"
            "   v_texCoord = (m_texMatrix * vec4(a_texCoord, 0.0, 1.0)).xy;\n"
            "}                                                           \n";
    }

    static const char *fragment_shader()
    {
        return
            "#extension GL_OES_EGL_image_external : require      \n"
            "precision mediump float;                            \n"
            "varying vec2 v_texCoord;                            \n"
            "uniform samplerExternalOES s_texture;               \n"
            "void main()                                         \n"
            "{                                                   \n"
            "  gl_FragColor = texture2D( s_texture, v_texCoord );\n"
            "}                                                   \n";
    }

    static GLuint loadShader(GLenum shaderType, const char* pSource)
    {
        GLuint shader = glCreateShader(shaderType);

        if (shader) {
            glShaderSource(shader, 1, &pSource, NULL);
            glCompileShader(shader);
            GLint compiled = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

            if (!compiled) {
                GLint infoLen = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
                if (infoLen) {
                    char* buf = (char*) malloc(infoLen);
                    if (buf) {
                        glGetShaderInfoLog(shader, infoLen, NULL, buf);
                        fprintf(stderr, "Could not compile shader %d:\n%s\n",
                                shaderType, buf);
                        free(buf);
                    }
                    glDeleteShader(shader);
                    shader = 0;
                }
            }
        } else {
            printf("Error, during shader creation: %i\n", glGetError());
        }

        return shader;
    }

    static GLuint create_program(const char* pVertexSource, const char* pFragmentSource)
    {
        GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
        if (!vertexShader) {
            printf("vertex shader not compiled\n");
            return 0;
        }

        GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
        if (!pixelShader) {
            printf("frag shader not compiled\n");
            return 0;
        }

        GLuint program = glCreateProgram();
        if (program) {
            glAttachShader(program, vertexShader);
            glAttachShader(program, pixelShader);
            glLinkProgram(program);
            GLint linkStatus = GL_FALSE;
            glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);

            if (linkStatus != GL_TRUE) {
                GLint bufLength = 0;
                glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
                if (bufLength) {
                    char* buf = (char*) malloc(bufLength);
                    if (buf) {
                        glGetProgramInfoLog(program, bufLength, NULL, buf);
                        fprintf(stderr, "Could not link program:\n%s\n", buf);
                        free(buf);
                    }
                }
                glDeleteProgram(program);
                program = 0;
            }
        }

        return program;
    }

    RenderData() : program_object(create_program(vertex_shader(), fragment_shader()))
    {
        position_loc = glGetAttribLocation(program_object, "a_position");
        tex_coord_loc = glGetAttribLocation(program_object, "a_texCoord");
        sampler_loc = glGetUniformLocation(program_object, "s_texture");
        matrix_loc = glGetUniformLocation(program_object, "m_texMatrix");
    }

    // Handle to a program object
    GLuint program_object;
    // Attribute locations
    GLint position_loc;
    GLint tex_coord_loc;
    // Sampler location
    GLint sampler_loc;
    // Matrix location
    GLint matrix_loc;
};

static int setup_video_texture(ClientWithSurface *cs, GLuint *preview_texture_id)
{
    assert(cs != NULL);
    assert(preview_texture_id != NULL);

    sf_surface_make_current(cs->surface);

    glGenTextures(1, preview_texture_id);
    glClearColor(0, 0, 0, 0);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    android_media_set_preview_texture(player, *preview_texture_id);

    return 0;
}

static void print_gl_error(unsigned int line)
{
    GLint error = glGetError();
    printf("GL error: %#04x (line: %d)\n", error, line);
}

static int update_gl_buffer(RenderData *render_data, EGLDisplay *disp, EGLSurface *surface)
{
    assert(disp != NULL);
    assert(surface != NULL);

    GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

    const GLfloat textureCoordinates[] = {
        1.0f,  1.0f,
        0.0f,  1.0f,
        0.0f,  0.0f,
        1.0f,  0.0f
    };

    calculate_position_coordinates();

    glClear(GL_COLOR_BUFFER_BIT);
    // Use the program object
    glUseProgram(render_data->program_object);
    // Enable attributes
    glEnableVertexAttribArray(render_data->position_loc);
    glEnableVertexAttribArray(render_data->tex_coord_loc);
    // Load the vertex position
    glVertexAttribPointer(render_data->position_loc,
            2,
            GL_FLOAT,
            GL_FALSE,
            0,
            positionCoordinates);
    // Load the texture coordinate
    glVertexAttribPointer(render_data->tex_coord_loc,
            2,
            GL_FLOAT,
            GL_FALSE,
            0,
            textureCoordinates);

    GLfloat matrix[16];
    android_media_surface_texture_get_transformation_matrix(player, matrix);

    glUniformMatrix4fv(render_data->matrix_loc, 1, GL_FALSE, matrix);

    glActiveTexture(GL_TEXTURE0);
    // Set the sampler texture unit to 0
    glUniform1i(render_data->sampler_loc, 0);
    glUniform1i(render_data->matrix_loc, 0);
    android_media_update_surface_texture(player);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    //glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
    glDisableVertexAttribArray(render_data->position_loc);
    glDisableVertexAttribArray(render_data->tex_coord_loc);

    eglSwapBuffers(*disp, *surface);

    return 0;
}

void set_video_size_cb(int height, int width, void *context)
{
    printf("Video height: %d, width: %d\n", height, width);
    printf("Video dest height: %f, width: %f\n", DestHeight, DestWidth);

    Height = height;
    Width = width;
}

/*
 * Feeds simple raw video frames into the encoder and makes sure that it produces
 * sane results. Minimum sane results are that m_numFrames go into the encoder
 * and m_numFrames come out of the decoder.
 *
 * Test is based on:
 * https://android.googlesource.com/platform/cts/+/jb-mr2-release/tests/tests/media/src/android/media/cts/EncodeDecodeTest.java
 */
static bool do_video_encode_decode_test()
{
    VideoEncodeTest test;
    // 720p 6 MB/s
    test.set_parameters(1280, 720, 6000000);
    return test.encode_decode_video_from_buffer();
}

int main(int argc, char **argv)
{
    ALOGE("DIRECT MEDIA TEST\n");
    if (argc < 2) {
        printf("*** Running video encoding/decoding test\n");
        const bool ret = do_video_encode_decode_test();
        if (!ret)
        {
            printf("FAIL: video encoding test failed\n");
            return EXIT_FAILURE;
        }
        else
            printf("*** Video encoding/decoding test PASSED\n");
    }
    else
    {
        player = android_media_new_player();
        if (player == NULL) {
            printf("Problem creating new media player.\n");
            return EXIT_FAILURE;
        }

        // Set player event cb for when the video size is known:
        android_media_set_video_size_cb(player, set_video_size_cb, NULL);

        printf("Setting data source to: %s.\n", argv[1]);

        if (android_media_set_data_source(player, argv[1]) != OK) {
            printf("Failed to set data source: %s\n", argv[1]);
            return EXIT_FAILURE;
        }

        WindowRenderer renderer(DestWidth, DestHeight);

        printf("Creating EGL surface.\n");
        ClientWithSurface cs = client_with_surface(true /* Associate surface with egl. */);
        if (!cs.surface) {
            printf("Problem acquiring surface for preview");
            return EXIT_FAILURE;
        }

        printf("Creating GL texture.\n");
        GLuint preview_texture_id;
        EGLDisplay disp = sf_client_get_egl_display(cs.client);
        EGLSurface surface = sf_surface_get_egl_surface(cs.surface);

        sf_surface_make_current(cs.surface);
        if (setup_video_texture(&cs, &preview_texture_id) != OK) {
            printf("Problem setting up GL texture for video surface.\n");
            return EXIT_FAILURE;
        }

        RenderData render_data;

        printf("Starting video playback.\n");
        android_media_play(player);

        printf("Updating gl buffer continuously...\n");
        while (android_media_is_playing(player)) {
            update_gl_buffer(&render_data, &disp, &surface);
        }

        android_media_stop(player);
    }

	return EXIT_SUCCESS;
}
