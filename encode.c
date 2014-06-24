/*
Copyright (c) 2012, Broadcom Europe Ltd
Copyright (c) 2012, Kalle Vahlman <zuh@iki>
                    Tuomas Kulve <tuomas@kulve.fi>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Video encode demo using OpenMAX IL though the ilcient helper library

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bcm_host.h"
#include "ilclient.h"

#define NUMFRAMES 300
#define WIDTH     640
#define PITCH     ((WIDTH+31)&~31)
#define HEIGHT    ((WIDTH)*9/16)
#define HEIGHT16  ((HEIGHT+15)&~15)
#define SIZE      ((WIDTH * HEIGHT16 * 3)/2)

// generate an animated test card in YUV format
static int
generate_test_card(void *buf, OMX_U32 * filledLen, int frame)
{
	int i, j;
	char *y = buf, *u = y + PITCH * HEIGHT16, *v =
		u + (PITCH >> 1) * (HEIGHT16 >> 1);

	for (j = 0; j < HEIGHT / 2; j++) {
		char *py = y + 2 * j * PITCH;
		char *pu = u + j * (PITCH >> 1);
		char *pv = v + j * (PITCH >> 1);
		for (i = 0; i < WIDTH / 2; i++) {
			int z = (((i + frame) >> 4) ^ ((j + frame) >> 4)) & 15;
			py[0] = py[1] = py[PITCH] = py[PITCH + 1] = 0x80 + z * 0x8;
			pu[0] = 0x00 + z * 0x10;
			pv[0] = 0x80 + z * 0x30;
			py += 2;
			pu++;
			pv++;
		}
	}
	*filledLen = SIZE;
	return 1;
}

static void
print_def_(OMX_PARAM_PORTDEFINITIONTYPE def, FILE *out, int image)
{
	fprintf(out, "Port %u: %s %u/%u %u %u %s, %s, %s",
			def.nPortIndex,
			def.eDir == OMX_DirInput ? "in" : "out",
			def.nBufferCountActual,
			def.nBufferCountMin,
			def.nBufferSize,
			def.nBufferAlignment,
			def.bEnabled ? "enabled" : "disabled",
			def.bPopulated ? "populated" : "not populated",
			def.bBuffersContiguous ? "contiguous" : "not contiguous");
if (image)
	fprintf(out, " Image: %ux%u %ux%u fmt=%u %u\n",
			def.format.image.nFrameWidth,
			def.format.image.nFrameHeight,
			def.format.image.nStride,
			def.format.image.nSliceHeight,
			def.format.image.eCompressionFormat,
			def.format.image.eColorFormat);
else
	fprintf(out, " Video: %ux%u %ux%u @%u %u\n",
			def.format.video.nFrameWidth,
			def.format.video.nFrameHeight,
			def.format.video.nStride,
			def.format.video.nSliceHeight,
			def.format.video.xFramerate >> 16,
			def.format.video.eColorFormat);
}

static void
print_def(OMX_PARAM_PORTDEFINITIONTYPE def, FILE *out)
{
	print_def_(def, out, 0); // video by default
}

int
video_encode_test(char *outputfilename)
{
	OMX_VIDEO_PARAM_PORTFORMATTYPE format;
	OMX_PARAM_PORTDEFINITIONTYPE def;
	COMPONENT_T *video_encode = NULL;
	COMPONENT_T *list[5];
	OMX_BUFFERHEADERTYPE *buf;
	OMX_BUFFERHEADERTYPE *out;
	OMX_ERRORTYPE r;
	ILCLIENT_T *client;
	int status = 0;
	int framenumber = 0;
	FILE *outf;

	memset(list, 0, sizeof(list));

	if ((client = ilclient_init()) == NULL) {
		return -3;
	}

	if (OMX_Init() != OMX_ErrorNone) {
		ilclient_destroy(client);
		return -4;
	}

	// create video_encode
	r = ilclient_create_component(client, &video_encode, "video_encode",
									ILCLIENT_DISABLE_ALL_PORTS |
									ILCLIENT_ENABLE_INPUT_BUFFERS |
									ILCLIENT_ENABLE_OUTPUT_BUFFERS);
	if (r != 0) {
		printf
			("ilclient_create_component() for video_encode failed with %x!\n",
			 r);
		exit(1);
	}
	list[0] = video_encode;

	// get current settings of video_encode component from port 200
	memset(&def, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
	def.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	def.nVersion.nVersion = OMX_VERSION;
	def.nPortIndex = 200;

	if (OMX_GetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamPortDefinition,
						 &def) != OMX_ErrorNone) {
		printf("%s:%d: OMX_GetParameter() for video_encode port 200 failed!\n",
			   __FUNCTION__, __LINE__);
		exit(1);
	}

	print_def(def, stdout);

	// Port 200: in 1/1 115200 16 enabled,not pop.,not cont. 320x240 320x240 @1966080 20
	def.format.video.nFrameWidth = WIDTH;
	def.format.video.nFrameHeight = HEIGHT;
	def.format.video.xFramerate = 30 << 16;
	def.format.video.nSliceHeight = def.format.video.nFrameHeight;
	def.format.video.nStride = def.format.video.nFrameWidth;
	def.format.video.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;

	print_def(def, stdout);

	r = OMX_SetParameter(ILC_GET_HANDLE(video_encode),
						 OMX_IndexParamPortDefinition, &def);
	if (r != OMX_ErrorNone) {
		printf("%s:%d: OMX_SetParameter() for video_encode port 200 failed with %x!\n",
			   __FUNCTION__, __LINE__, r);
		exit(1);
	}

	memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
	format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
	format.nVersion.nVersion = OMX_VERSION;
	format.nPortIndex = 201;
	format.eCompressionFormat = OMX_VIDEO_CodingAVC; // OMX_VIDEO_CodingMPEG4 fails, see https://github.com/raspberrypi/firmware/issues/162

	printf("OMX_SetParameter for video_encode:201...\n");
	r = OMX_SetParameter(ILC_GET_HANDLE(video_encode),
						 OMX_IndexParamVideoPortFormat, &format);
	if (r != OMX_ErrorNone) {
		printf("%s:%d: OMX_SetParameter() for video_encode port 201 failed with %x!\n",
			   __FUNCTION__, __LINE__, r);
		exit(1);
	}

	OMX_VIDEO_PARAM_BITRATETYPE bitrateType;
	// set current bitrate to 1Mbit
	memset(&bitrateType, 0, sizeof(OMX_VIDEO_PARAM_BITRATETYPE));
	bitrateType.nSize = sizeof(OMX_VIDEO_PARAM_BITRATETYPE);
	bitrateType.nVersion.nVersion = OMX_VERSION;
	bitrateType.eControlRate = OMX_Video_ControlRateVariable;
	bitrateType.nTargetBitrate = 1000000;
	bitrateType.nPortIndex = 201;
	r = OMX_SetParameter(ILC_GET_HANDLE(video_encode),
						 OMX_IndexParamVideoBitrate, &bitrateType);
	if (r != OMX_ErrorNone) {
		printf("%s:%d: OMX_SetParameter() for bitrate for video_encode port 201 failed with %x!\n",
			   __FUNCTION__, __LINE__, r);
		exit(1);
	}


	// get current bitrate
	memset(&bitrateType, 0, sizeof(OMX_VIDEO_PARAM_BITRATETYPE));
	bitrateType.nSize = sizeof(OMX_VIDEO_PARAM_BITRATETYPE);
	bitrateType.nVersion.nVersion = OMX_VERSION;
	bitrateType.nPortIndex = 201;

	if (OMX_GetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoBitrate,
						 &bitrateType) != OMX_ErrorNone) {
		printf("%s:%d: OMX_GetParameter() for video_encode for bitrate port 201 failed!\n",
			   __FUNCTION__, __LINE__);
		exit(1);
	}
	printf("Current Bitrate=%u\n",bitrateType.nTargetBitrate);


	printf("encode to idle...\n");
	if (ilclient_change_component_state(video_encode, OMX_StateIdle) == -1) {
		printf("%s:%d: ilclient_change_component_state(video_encode, OMX_StateIdle) failed",
			   __FUNCTION__, __LINE__);
	}

	printf("enabling port buffers for 200...\n");
	if (ilclient_enable_port_buffers(video_encode, 200, NULL, NULL, NULL) != 0) {
		printf("enabling port buffers for 200 failed!\n");
		exit(1);
	}

	printf("enabling port buffers for 201...\n");
	if (ilclient_enable_port_buffers(video_encode, 201, NULL, NULL, NULL) != 0) {
		printf("enabling port buffers for 201 failed!\n");
		exit(1);
	}

	printf("encode to executing...\n");
	ilclient_change_component_state(video_encode, OMX_StateExecuting);

	outf = fopen(outputfilename, "w");
	if (outf == NULL) {
		printf("Failed to open '%s' for writing video\n", outputfilename);
		exit(1);
	}

	printf("looping for buffers...\n");
	do {
		buf = ilclient_get_input_buffer(video_encode, 200, 1);
		if (buf == NULL) {
			printf("Doh, no buffers for me!\n");
		}
		else {
			/* fill it */
			generate_test_card(buf->pBuffer, &buf->nFilledLen, framenumber++);

			if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_encode), buf) !=
				OMX_ErrorNone) {
				printf("Error emptying buffer!\n");
			}

			out = ilclient_get_output_buffer(video_encode, 201, 1);

			r = OMX_FillThisBuffer(ILC_GET_HANDLE(video_encode), out);
			if (r != OMX_ErrorNone) {
				printf("Error filling buffer: %x\n", r);
			}

			if (out != NULL) {
				if (out->nFlags & OMX_BUFFERFLAG_CODECCONFIG) {
					int i;
					for (i = 0; i < out->nFilledLen; i++)
						printf("%x ", out->pBuffer[i]);
					printf("\n");
				}

				r = fwrite(out->pBuffer, 1, out->nFilledLen, outf);
				if (r != out->nFilledLen) {
					printf("fwrite: Error emptying buffer: %d!\n", r);
				}
				else {
					printf("Writing frame %d/%d\n", framenumber, NUMFRAMES);
				}
				out->nFilledLen = 0;
			}
			else {
				printf("Not getting it :(\n");
			}
		}
	}
	while (framenumber < NUMFRAMES);

	fclose(outf);

	printf("Teardown.\n");

	printf("disabling port buffers for 200 and 201...\n");
	ilclient_disable_port_buffers(video_encode, 200, NULL, NULL, NULL);
	ilclient_disable_port_buffers(video_encode, 201, NULL, NULL, NULL);

	ilclient_state_transition(list, OMX_StateIdle);
	ilclient_state_transition(list, OMX_StateLoaded);

	ilclient_cleanup_components(list);

	OMX_Deinit();

	ilclient_destroy(client);
	return status;
}

extern void
capture_frame(void *out_buf, OMX_U32 *out_size);

#define OMX_ERR_EXIT(fmt_str) {\
		fprintf(stderr, fmt_str, __FUNCTION__, __LINE__, r);\
		exit(1);\
	}

#define ILC_ERR_EXIT(fmt_str) {\
		fprintf(stderr, fmt_str, __FUNCTION__, __LINE__, r_il);\
		exit(1);\
	}

#define INIT_OMX_TYPE(var, type, port) memset(&(var), 0, sizeof(type));\
	(var).nSize = sizeof(type);\
	(var).nVersion.nVersion = OMX_VERSION;\
	(var).nPortIndex = (port);

int
capture_encode_loop(int frames, OMX_U32 frameWidth, OMX_U32 frameHeight, uint frameRate, OMX_COLOR_FORMATTYPE colorFormat, unsigned int bufsize)
{
	OMX_VIDEO_PARAM_PORTFORMATTYPE format;
	OMX_PARAM_PORTDEFINITIONTYPE def;
	COMPONENT_T *video_encode = NULL;
	COMPONENT_T *list[5];
	OMX_BUFFERHEADERTYPE *buf;
	OMX_BUFFERHEADERTYPE *out;
	OMX_ERRORTYPE r;
	int r_il = 0;
	ILCLIENT_T *client;
	int status = 0;
	int framenumber = 0;

	memset(list, 0, sizeof(list));

	if ((client = ilclient_init()) == NULL) {
		fprintf(stderr, "ilclient_init() for video_encode failed!\n");
		return -3;
	}

	if ((r = OMX_Init()) != OMX_ErrorNone) {
		ilclient_destroy(client);
		fprintf(stderr, "OMX_Init() for video_encode failed with %x!\n", r);
		return -4;
	}

	// create video_encode
	if (ilclient_create_component(client, &video_encode, "video_encode", 
		ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS | ILCLIENT_ENABLE_OUTPUT_BUFFERS) != 0)
		ILC_ERR_EXIT("%s:%d: ilclient_create_component() for video_encode failed!\n")

	list[0] = video_encode;

	// get current settings of video_encode component from port 200
	INIT_OMX_TYPE(def, OMX_PARAM_PORTDEFINITIONTYPE, 200)

	if ((r = OMX_GetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamPortDefinition, &def)) != OMX_ErrorNone)
		OMX_ERR_EXIT("%s:%d: OMX_GetParameter() for video_encode port 200 failed with %x!\n")

	print_def(def, stderr);
	// Port 200: in 1/1 115200 16 enabled,not pop.,not cont. 320x240 320x240 @1966080 20

	def.nBufferSize = bufsize;
	def.format.video.nFrameWidth = frameWidth;
	def.format.video.nFrameHeight = frameHeight;
	def.format.video.xFramerate = frameRate << 16; // 30 << 16;
	def.format.video.nSliceHeight = def.format.video.nFrameHeight;
	def.format.video.nStride = def.format.video.nFrameWidth;
	def.format.video.eColorFormat = colorFormat; // OMX_COLOR_FormatYUV420PackedPlanar;

	print_def(def, stderr);

	if ((r = OMX_SetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamPortDefinition, &def)) != OMX_ErrorNone)
		OMX_ERR_EXIT("%s:%d: OMX_SetParameter() for video_encode port 200 failed with %x!\n")

	INIT_OMX_TYPE(format, OMX_VIDEO_PARAM_PORTFORMATTYPE, 201)
	format.eCompressionFormat = OMX_VIDEO_CodingAVC; // OMX_VIDEO_CodingMPEG4 fails, see https://github.com/raspberrypi/firmware/issues/162

	fprintf(stderr, "OMX_SetParameter for video_encode:201...\n");
	if ((r = OMX_SetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoPortFormat, &format)) != OMX_ErrorNone)
		OMX_ERR_EXIT("%s:%d: OMX_SetParameter() for format for video_encode port 201 failed with %x!\n")

	OMX_VIDEO_PARAM_BITRATETYPE bitrateType;
	// set current bitrate to 1Mbit
	INIT_OMX_TYPE(bitrateType, OMX_VIDEO_PARAM_BITRATETYPE, 201)
	bitrateType.eControlRate = OMX_Video_ControlRateVariable;
	bitrateType.nTargetBitrate = 1000000;

	if ((r = OMX_SetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoBitrate, &bitrateType)) != OMX_ErrorNone)
		OMX_ERR_EXIT("%s:%d: OMX_SetParameter() for bitrate for video_encode port 201 failed with %x!\n")

	// get current bitrate
	INIT_OMX_TYPE(bitrateType, OMX_VIDEO_PARAM_BITRATETYPE, 201)

	if ((r = OMX_GetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoBitrate, &bitrateType)) != OMX_ErrorNone)
		OMX_ERR_EXIT("%s:%d: OMX_GetParameter() for bitrate for video_encode port 201 failed with %x!\n")
	fprintf(stderr, "Current Bitrate=%u\n",bitrateType.nTargetBitrate);

	fprintf(stderr, "encode to idle...\n");
	if (ilclient_change_component_state(video_encode, OMX_StateIdle) == -1)
		fprintf(stderr, "%s:%d: ilclient_change_component_state(video_encode, OMX_StateIdle) failed", __FUNCTION__, __LINE__);

	fprintf(stderr, "enabling port buffers for 200...\n");
	if (ilclient_enable_port_buffers(video_encode, 200, NULL, NULL, NULL) != 0)
		ILC_ERR_EXIT("%s:%d: enabling port buffers for 200 failed!\n")

	fprintf(stderr, "enabling port buffers for 201...\n");
	if (ilclient_enable_port_buffers(video_encode, 201, NULL, NULL, NULL) != 0)
		ILC_ERR_EXIT("%s:%d: enabling port buffers for 201 failed!\n")

	fprintf(stderr, "encode to executing...\n");
	ilclient_change_component_state(video_encode, OMX_StateExecuting);

	fprintf(stderr, "looping for buffers...\n");
	do {
		buf = ilclient_get_input_buffer(video_encode, 200, 1);
		if (buf == NULL) {
			fprintf(stderr, "Doh, no buffers for me!\n");
		}
		else {
			/* fill it */
			//generate_test_card(buf->pBuffer, &buf->nFilledLen, framenumber++);
			capture_frame(buf->pBuffer, &buf->nFilledLen);
			framenumber++;

			if ((r = OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_encode), buf)) != OMX_ErrorNone)
				fprintf(stderr, "Error emptying buffer: %x\n", r);

			out = ilclient_get_output_buffer(video_encode, 201, 1);

			if ((r = OMX_FillThisBuffer(ILC_GET_HANDLE(video_encode), out)) != OMX_ErrorNone)
				fprintf(stderr, "Error filling buffer: %x\n", r);

			if (out != NULL) {
				if (out->nFlags & OMX_BUFFERFLAG_CODECCONFIG) {
					int i;
					for (i = 0; i < out->nFilledLen; i++)
						fprintf(stderr, "%x ", out->pBuffer[i]);
					fprintf(stderr, "\n");
				}

				size_t res = fwrite(out->pBuffer, 1, out->nFilledLen, stdout);
				if (res != out->nFilledLen)
					fprintf(stderr, "fwrite: Error emptying buffer: %d\n", res);
				//else
				//	fprintf(stderr, "Writing frame %d/%d\n", framenumber, frames);

				out->nFilledLen = 0;
			}
			else
				fprintf(stderr, "Not getting it :(\n");
		}
	}
	while (framenumber < frames);

	fprintf(stderr, "Teardown.\n");

	fprintf(stderr, "disabling port buffers for 200 and 201...\n");
	ilclient_disable_port_buffers(video_encode, 200, NULL, NULL, NULL);
	ilclient_disable_port_buffers(video_encode, 201, NULL, NULL, NULL);

	ilclient_state_transition(list, OMX_StateIdle);
	ilclient_state_transition(list, OMX_StateLoaded);

	ilclient_cleanup_components(list);

	OMX_Deinit();

	ilclient_destroy(client);
	return status;
}

int
capture_encode_jpeg_loop(int frames, OMX_U32 frameWidth, OMX_U32 frameHeight, uint frameRate, OMX_COLOR_FORMATTYPE colorFormat, unsigned int bufsize)
{
	OMX_VIDEO_PARAM_PORTFORMATTYPE format;
	OMX_PARAM_PORTDEFINITIONTYPE def;
	COMPONENT_T *video_encode = NULL, *image_decode = NULL;
	COMPONENT_T *list[5];
	TUNNEL_T tunnel[4];
	OMX_BUFFERHEADERTYPE *buf;
	OMX_BUFFERHEADERTYPE *out;
	OMX_ERRORTYPE r;
	int r_il = 0;
	ILCLIENT_T *client;
	int status = 0;
	int framenumber = 0;

	memset(list, 0, sizeof(list));

	if ((client = ilclient_init()) == NULL) {
		fprintf(stderr, "ilclient_init() for video_encode failed!\n");
		return -3;
	}

	if ((r = OMX_Init()) != OMX_ErrorNone) {
		ilclient_destroy(client);
		fprintf(stderr, "OMX_Init() for video_encode failed with %x!\n", r);
		return -4;
	}

	// create image_decode
	if (ilclient_create_component(client, &image_decode, "image_decode", 
		ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS) != 0)
		ILC_ERR_EXIT("%s:%d: ilclient_create_component() for image_decode failed!\n")
	list[0] = image_decode;
	// get current settings of image_decode component from port 320
	INIT_OMX_TYPE(def, OMX_PARAM_PORTDEFINITIONTYPE, 320)
	if ((r = OMX_GetParameter(ILC_GET_HANDLE(image_decode), OMX_IndexParamPortDefinition, &def)) != OMX_ErrorNone)
		OMX_ERR_EXIT("%s:%d: OMX_GetParameter() for image_decode port 320 failed with %x!\n")
	print_def_(def, stderr, 1);
	// Port 320: in 3/2 81920 16 disabled, not populated, not contiguous Image: 0x0 0x0 fmt=6 0
	// set eCompressionFormat = 2 (JPEG) and input buffer size
	def.nBufferSize = bufsize;
	def.format.image.eCompressionFormat = OMX_IMAGE_CodingJPEG;
	if ((r = OMX_SetParameter(ILC_GET_HANDLE(image_decode), OMX_IndexParamPortDefinition, &def)) != OMX_ErrorNone)
		OMX_ERR_EXIT("%s:%d: OMX_SetParameter() for image_decode port 320 failed with %x!\n")

	// create video_encode
	if (ilclient_create_component(client, &video_encode, "video_encode", 
		ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_OUTPUT_BUFFERS) != 0)
		ILC_ERR_EXIT("%s:%d: ilclient_create_component() for video_encode failed!\n")
	list[1] = video_encode;
	// get current settings of video_encode component from port 200
	INIT_OMX_TYPE(def, OMX_PARAM_PORTDEFINITIONTYPE, 200)
	if ((r = OMX_GetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamPortDefinition, &def)) != OMX_ErrorNone)
		OMX_ERR_EXIT("%s:%d: OMX_GetParameter() for video_encode port 200 failed with %x!\n")
	print_def(def, stderr);
	// Port 200: in 1/1 115200 16 enabled,not pop.,not cont. 320x240 320x240 @1966080 20
	def.format.video.nFrameWidth = frameWidth;
	def.format.video.nFrameHeight = frameHeight;
	def.format.video.xFramerate = frameRate << 16; // 30 << 16;
	def.format.video.nSliceHeight = def.format.video.nFrameHeight;
	def.format.video.nStride = def.format.video.nFrameWidth;
	def.format.video.eColorFormat = colorFormat; // OMX_COLOR_FormatYUV420PackedPlanar;
	print_def(def, stderr);
	if ((r = OMX_SetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamPortDefinition, &def)) != OMX_ErrorNone)
		OMX_ERR_EXIT("%s:%d: OMX_SetParameter() for video_encode port 200 failed with %x!\n")
	// set video_encode codec to H.264
	INIT_OMX_TYPE(format, OMX_VIDEO_PARAM_PORTFORMATTYPE, 201)
	format.eCompressionFormat = OMX_VIDEO_CodingAVC;
	fprintf(stderr, "OMX_SetParameter for video_encode:201...\n");
	if ((r = OMX_SetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoPortFormat, &format)) != OMX_ErrorNone)
		OMX_ERR_EXIT("%s:%d: OMX_SetParameter() for format for video_encode port 201 failed with %x!\n")
	// set current bitrate to 1Mbit
	OMX_VIDEO_PARAM_BITRATETYPE bitrateType;
	INIT_OMX_TYPE(bitrateType, OMX_VIDEO_PARAM_BITRATETYPE, 201)
	bitrateType.eControlRate = OMX_Video_ControlRateVariable;
	bitrateType.nTargetBitrate = 1000000;
	if ((r = OMX_SetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoBitrate, &bitrateType)) != OMX_ErrorNone)
		OMX_ERR_EXIT("%s:%d: OMX_SetParameter() for bitrate for video_encode port 201 failed with %x!\n")
	// get current bitrate
	INIT_OMX_TYPE(bitrateType, OMX_VIDEO_PARAM_BITRATETYPE, 201)
	if ((r = OMX_GetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoBitrate, &bitrateType)) != OMX_ErrorNone)
		OMX_ERR_EXIT("%s:%d: OMX_GetParameter() for bitrate for video_encode port 201 failed with %x!\n")
	fprintf(stderr, "Current Bitrate=%u\n",bitrateType.nTargetBitrate);

	//set_tunnel(tunnel, image_decode, 321, video_encode, 200);

	fprintf(stderr, "image decode to idle...\n");
	if (ilclient_change_component_state(image_decode, OMX_StateIdle) == -1)
		fprintf(stderr, "%s:%d: ilclient_change_component_state(image_decode, OMX_StateIdle) failed", __FUNCTION__, __LINE__);

	fprintf(stderr, "enabling port buffers for 320...\n");
	if (ilclient_enable_port_buffers(image_decode, 320, NULL, NULL, NULL) != 0)
		ILC_ERR_EXIT("%s:%d: enabling port buffers for 320 failed!\n")

	fprintf(stderr, "video encode to idle...\n");
	if (ilclient_change_component_state(video_encode, OMX_StateIdle) == -1)
		fprintf(stderr, "%s:%d: ilclient_change_component_state(video_encode, OMX_StateIdle) failed", __FUNCTION__, __LINE__);

	fprintf(stderr, "enabling port buffers for 201...\n");
	if (ilclient_enable_port_buffers(video_encode, 201, NULL, NULL, NULL) != 0)
		ILC_ERR_EXIT("%s:%d: enabling port buffers for 201 failed!\n")

	//if ((r_il = ilclient_setup_tunnel(tunnel, 0, 0)) != 0)
	//	ILC_ERR_EXIT("%s:%d: setting up tunnel failed (%i)!\n")

	fprintf(stderr, "image decode to executing...\n");
	ilclient_change_component_state(image_decode, OMX_StateExecuting);

	fprintf(stderr, " video encode to executing...\n");
	ilclient_change_component_state(video_encode, OMX_StateExecuting);

	fprintf(stderr, "looping for buffers...\n");
	do {
		buf = ilclient_get_input_buffer(image_decode, 320, 1);
		if (buf == NULL) {
			fprintf(stderr, "Doh, no buffers for me!\n");
		}
		else {
			/* fill it */
			//generate_test_card(buf->pBuffer, &buf->nFilledLen, framenumber++);
			capture_frame(buf->pBuffer, &buf->nFilledLen);
			framenumber++;

			if ((r = OMX_EmptyThisBuffer(ILC_GET_HANDLE(image_decode), buf)) != OMX_ErrorNone)
				fprintf(stderr, "Error emptying buffer: %x\n", r);

			out = ilclient_get_output_buffer(image_decode, 321, 1);

			if ((r = OMX_FillThisBuffer(ILC_GET_HANDLE(image_decode), out)) != OMX_ErrorNone)
				fprintf(stderr, "Error filling buffer: %x\n", r);

			buf = ilclient_get_input_buffer(video_encode, 200, 1);

			while (out->nFilledLen == 0) {}
			memcpy(buf->pBuffer, out->pBuffer, out->nFilledLen);
			out->nFilledLen = 0;

			if ((r = OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_encode), buf)) != OMX_ErrorNone)
				fprintf(stderr, "Error emptying buffer: %x\n", r);

			out = ilclient_get_output_buffer(video_encode, 201, 1);

			if ((r = OMX_FillThisBuffer(ILC_GET_HANDLE(video_encode), out)) != OMX_ErrorNone)
				fprintf(stderr, "Error filling buffer: %x\n", r);

			if (out != NULL) {
				if (out->nFlags & OMX_BUFFERFLAG_CODECCONFIG) {
					int i;
					for (i = 0; i < out->nFilledLen; i++)
						fprintf(stderr, "%x ", out->pBuffer[i]);
					fprintf(stderr, "\n");
				}

				size_t res = fwrite(out->pBuffer, 1, out->nFilledLen, stdout);
				if (res != out->nFilledLen)
					fprintf(stderr, "fwrite: Error emptying buffer: %d\n", res);
				//else
				//	fprintf(stderr, "Writing frame %d/%d\n", framenumber, frames);

				out->nFilledLen = 0;
			}
			else
				fprintf(stderr, "Not getting it :(\n");
		}
	}
	while (framenumber < frames);

	fprintf(stderr, "Teardown.\n");

	fprintf(stderr, "disabling port buffers for 320 and 201...\n");
	ilclient_disable_port_buffers(image_decode, 320, NULL, NULL, NULL);
	ilclient_disable_port_buffers(video_encode, 201, NULL, NULL, NULL);

	ilclient_disable_tunnel(tunnel);
	ilclient_teardown_tunnels(tunnel);

	ilclient_state_transition(list, OMX_StateIdle);
	ilclient_state_transition(list, OMX_StateLoaded);

	ilclient_cleanup_components(list);

	OMX_Deinit();

	ilclient_destroy(client);
	return status;
}

//int
//main(int argc, char **argv)
//{
//	if (argc < 2) {
//		printf("Usage: %s <filename>\n", argv[0]);
//		exit(1);
//	}
//	bcm_host_init();
//	return video_encode_test(argv[1]);
//	bcm_host_deinit();
//}
