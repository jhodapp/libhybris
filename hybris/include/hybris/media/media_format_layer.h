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
 */

#ifndef MEDIA_FORMAT_LAYER_H_
#define MEDIA_FORMAT_LAYER_H_

#include <stddef.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef void* MediaFormat;

    MediaFormat media_format_create_video_format(const char *mime, int32_t width, int32_t height, int64_t duration_us, int32_t max_input_size);

    void media_format_destroy(MediaFormat format);
    void media_format_ref(MediaFormat format);
    void media_format_unref(MediaFormat format);

    bool media_format_set_byte_buffer(MediaFormat format, const char *key, uint8_t *data, size_t size);
    bool media_format_set_height(MediaFormat format, int32_t height);
    bool media_format_set_width(MediaFormat format, int32_t width);
    bool media_format_set_max_input_size(MediaFormat format, int32_t size);
    bool media_format_set_bitrate(MediaFormat format, int32_t bitrate);
    bool media_format_set_framerate(MediaFormat format, int32_t framerate);
    bool media_format_set_iframe_interval(MediaFormat format, int32_t interval);
    bool media_format_set_stride(MediaFormat format, int32_t stride);
    bool media_format_set_slice_height(MediaFormat format, int32_t slice_height);
    bool media_format_set_color_format(MediaFormat format, int32_t color);
    bool media_format_set_profile_idc(MediaFormat format, int32_t profile);
    bool media_format_set_level_idc(MediaFormat format, int32_t level);

    const char* media_format_get_mime(MediaFormat format);
    int64_t media_format_get_duration_us(MediaFormat format);
    int32_t media_format_get_width(MediaFormat format);
    int32_t media_format_get_height(MediaFormat format);
    int32_t media_format_get_max_input_size(MediaFormat format);
    int32_t media_format_get_bitrate(MediaFormat format);
    int32_t media_format_get_bitrate_mode(MediaFormat format);
    int32_t media_format_get_framerate(MediaFormat format);
    int32_t media_format_get_iframe_interval(MediaFormat format);
    int32_t media_format_get_stride(MediaFormat format);
    int32_t media_format_get_slice_height(MediaFormat format);
    int32_t media_format_get_color_format(MediaFormat format);
    int32_t media_format_get_profile_idc(MediaFormat format);
    int32_t media_format_get_level_idc(MediaFormat format);
    int32_t media_format_get_crop_left(MediaFormat format);
    int32_t media_format_get_crop_right(MediaFormat format);
    int32_t media_format_get_crop_top(MediaFormat format);
    int32_t media_format_get_crop_bottom(MediaFormat format);
    int32_t media_format_get_crop_width(MediaFormat format);
    int32_t media_format_get_crop_height(MediaFormat format);

    // TODO: Add getter for CSD buffer

#ifdef __cplusplus
}
#endif

#endif // MEDIA_FORMAT_LAYER_H_
