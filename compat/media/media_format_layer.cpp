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

// Uncomment to enable verbose debug output
#define LOG_NDEBUG 0

#undef LOG_TAG
#define LOG_TAG "MediaFormatLayer"

#include <hybris/media/media_format_layer.h>
#include "media_format_layer_priv.h"

#include <assert.h>

#include <utils/Log.h>

#define REPORT_FUNCTION() ALOGV("%s \n", __PRETTY_FUNCTION__);

using namespace android;

static inline _MediaFormat *get_internal_format(MediaFormat format)
{
    if (format == NULL)
    {
        ALOGE("format must not be NULL");
        return NULL;
    }

    _MediaFormat *mf = static_cast<_MediaFormat*>(format);
    assert(mf->refcount >= 1);

    return mf;
}

MediaFormat media_format_create_video_format(const char *mime, int32_t width, int32_t height, int64_t duration_us, int32_t max_input_size)
{
    REPORT_FUNCTION()

    _MediaFormat *format = new _MediaFormat();
    format->mime = AString(mime);
    format->width = width;
    format->height = height;
    format->duration_us = duration_us;
    format->max_input_size = max_input_size;

    return format;
}

void media_format_destroy(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return;

    if (mf->refcount)
        return;

    delete mf;
}

void media_format_ref(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return;

    mf->refcount++;
}

void media_format_unref(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return;

    if (mf->refcount)
        mf->refcount--;
}

bool media_format_set_byte_buffer(MediaFormat format, const char *key, uint8_t *data, size_t size)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return false;
    if (key == NULL || data == NULL || size == 0)
        return false;

    mf->csd_key_name = AString(key);
    mf->csd = sp<ABuffer>(new ABuffer(data, size));
    return true;
}

bool media_format_set_height(MediaFormat format, int32_t height)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return false;

    mf->height = height;
    return true;
}

bool media_format_set_width(MediaFormat format, int32_t width)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return false;

    mf->width = width;
    return true;
}

bool media_format_set_max_input_size(MediaFormat format, int32_t size)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return false;

    mf->max_input_size = size;
    return true;
}

bool media_format_set_bitrate(MediaFormat format, int32_t bitrate)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return false;

    mf->bitrate = bitrate;
    return true;
}

bool media_format_set_framerate(MediaFormat format, int32_t framerate)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return false;

    mf->framerate = framerate;
    return true;
}

bool media_format_set_iframe_interval(MediaFormat format, int32_t interval)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return false;

    mf->iframe_interval = interval;
    return true;
}

bool media_format_set_stride(MediaFormat format, int32_t stride)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return false;

    mf->stride = stride;
    return true;
}

bool media_format_set_slice_height(MediaFormat format, int32_t slice_height)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return false;

    mf->slice_height = slice_height;
    return true;
}

bool media_format_set_color_format(MediaFormat format, int32_t color)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return false;

    mf->color_format = color;
    return true;
}

bool media_format_set_profile_idc(MediaFormat format, int32_t profile)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return false;

    mf->profile_idc = profile;
    return true;
}

bool media_format_set_level_idc(MediaFormat format, int32_t level)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return false;

    mf->level_idc = level;
    return true;
}

const char* media_format_get_mime(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return NULL;

    return mf->mime.c_str();
}

int64_t media_format_get_duration_us(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->duration_us;
}

int32_t media_format_get_width(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->width;
}

int32_t media_format_get_height(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->height;
}

int32_t media_format_get_max_input_size(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->max_input_size;
}

int32_t media_format_get_bitrate(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->bitrate;
}

int32_t media_format_get_bitrate_mode(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->bitrate_mode;
}

int32_t media_format_get_framerate(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->framerate;
}

int32_t media_format_get_iframe_interval(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->iframe_interval;
}

int32_t media_format_get_stride(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->stride;
}

int32_t media_format_get_slice_height(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->slice_height;
}

int32_t media_format_get_color_format(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->color_format;
}

int32_t media_format_get_profile_idc(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->profile_idc;
}

int32_t media_format_get_level_idc(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->level_idc;
}

int32_t media_format_get_crop_left(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->crop_left;
}

int32_t media_format_get_crop_right(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->crop_right;
}

int32_t media_format_get_crop_top(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->crop_top;
}

int32_t media_format_get_crop_bottom(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->crop_bottom;
}

int32_t media_format_get_crop_height(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->crop_height;
}

int32_t media_format_get_crop_width(MediaFormat format)
{
    REPORT_FUNCTION()

    _MediaFormat *mf = get_internal_format(format);
    if (mf == NULL)
        return 0;

    return mf->crop_width;
}
