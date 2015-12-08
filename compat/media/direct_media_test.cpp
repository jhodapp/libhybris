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

public:
    VideoEncodeTest()
        : m_mediaFormat(nullptr)
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
        select_codec(m_mimeType);
        const uint32_t color_format = select_color_format();

        if (!m_mediaFormat)
        {
            ALOGW("m_mediaFormat should not be NULL");
            return false;
        }

        ret = media_format_set_max_input_size(m_mediaFormat, 1024);
        if (!ret)
        {
            ALOGW("Failed to set max_input_size");
            return false;
        }
        ret = media_format_set_color_format(m_mediaFormat, color_format);
        if (!ret)
        {
            ALOGW("Failed to set color format");
            return false;
        }
        ret = media_format_set_stride(m_mediaFormat, media_format_get_width(m_mediaFormat));
        if (!ret)
        {
            ALOGW("Failed to set stride");
            return false;
        }
        ret = media_format_set_slice_height(m_mediaFormat, media_format_get_height(m_mediaFormat));
        if (!ret)
        {
            ALOGW("Failed to set slice_height");
            return false;
        }
        // 30 fps
        ret = media_format_set_framerate(m_mediaFormat, 30);
        if (!ret)
        {
            ALOGW("Failed to set framerate");
            return false;
        }
        // IFrame every 1 second
        ret = media_format_set_iframe_interval(m_mediaFormat, 1);
        if (!ret)
        {
            ALOGW("Failed to set iframe interval");
            return false;
        }

        const bool isEncoder = true;
        MediaCodecDelegate encoder = media_codec_create_by_codec_type(m_mimeType, isEncoder);
        ret = media_codec_configure(
                    encoder,
                    m_mediaFormat,
                    nullptr, /* SurfaceTextureClientHybris */
                    MEDIA_CODEC_CONFIGURE_FLAG_ENCODE);
        if (ret != android::OK)
        {
            ALOGE("Failed to configure encoder correctly");
            return false;
        }

        ret = media_codec_start(encoder);
        if (ret != android::OK)
        {
            ALOGE("Failed to start encoder");
            return false;
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

            const bool ret = get_color_formats(i, mime);
            if (!ret)
                return false;

            if (found_codec)
                break;
        }

        return found_codec;
    }

    bool get_color_formats(int index, const char *mime)
    {
        size_t num_colors = media_codec_list_get_num_color_formats(index, mime);
        m_codecInfo.color_formats = (uint32_t*)malloc(num_colors);
        const int ret = media_codec_list_get_codec_color_formats(index, mime, m_codecInfo.color_formats);
        if (ret != OK)
        {
            ALOGW("Failed to get the codec color formats\n");
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

    bool is_recognized_format(uint32_t color_format)
    {
        switch (color_format)
        {
            // These are the formats we know how to handle for this test
            case MediaCodecInfo::ColorFormats::COLOR_FormatYUV420Planar:
            case MediaCodecInfo::ColorFormats::COLOR_FormatYUV420PackedPlanar:
            case MediaCodecInfo::ColorFormats::COLOR_FormatYUV420SemiPlanar:
            case MediaCodecInfo::ColorFormats::COLOR_FormatYUV420PackedSemiPlanar:
            case MediaCodecInfo::ColorFormats::COLOR_TI_FormatYUV420PackedSemiPlanar:
                return true;
            default:
                return false;
        }
    }

    const char *m_mimeType = "video/avc";
    MediaCodecInfo m_codecInfo;
    MediaFormat m_mediaFormat;
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
 * sane results.
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
	}

#if 0
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
#endif

	return EXIT_SUCCESS;
}
