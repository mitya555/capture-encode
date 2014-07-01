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
#include "interface/vcos/vcos_assert.h"

#include <time.h>

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
generate_test_card(void *buf, OMX_U32 * filledLen, int frame) {
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
print_def_(OMX_PARAM_PORTDEFINITIONTYPE *def_ptr, FILE *out, int image) {
	fprintf(out, "Port %u: %s %u/%u %u %u %s, %s, %s",
			def_ptr->nPortIndex,
			def_ptr->eDir == OMX_DirInput ? "in" : "out",
			def_ptr->nBufferCountActual,
			def_ptr->nBufferCountMin,
			def_ptr->nBufferSize,
			def_ptr->nBufferAlignment,
			def_ptr->bEnabled ? "enabled" : "disabled",
			def_ptr->bPopulated ? "populated" : "not populated",
			def_ptr->bBuffersContiguous ? "contiguous" : "not contiguous");
if (image)
	fprintf(out, " Image: %ux%u %ux%u fmt=%u %u\n",
			def_ptr->format.image.nFrameWidth,
			def_ptr->format.image.nFrameHeight,
			def_ptr->format.image.nStride,
			def_ptr->format.image.nSliceHeight,
			def_ptr->format.image.eCompressionFormat,
			def_ptr->format.image.eColorFormat);
else
	fprintf(out, " Video: %ux%u %ux%u @%u %u\n",
			def_ptr->format.video.nFrameWidth,
			def_ptr->format.video.nFrameHeight,
			def_ptr->format.video.nStride,
			def_ptr->format.video.nSliceHeight,
			def_ptr->format.video.xFramerate >> 16,
			def_ptr->format.video.eColorFormat);
}

extern void 
time_diff(struct timespec *start, struct timespec *end, struct timespec *result);

static void
get_time_diff(struct timespec *start, struct timespec *result) {
	struct timespec cur_time;
	clock_gettime(CLOCK_MONOTONIC, &cur_time);
	time_diff(start, &cur_time, result);
}

static void
wait_timeout(__time_t sec, __suseconds_t usec) {
	struct timeval timeout;
	timeout.tv_sec = sec;
	timeout.tv_usec = usec;
	select(0, NULL, NULL, NULL, &timeout);
}

static void
print_def(OMX_PARAM_PORTDEFINITIONTYPE *def_ptr, FILE *out) {
	print_def_(def_ptr, out, 0); // video by default
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

	print_def(&def, stdout);

	// Port 200: in 1/1 115200 16 enabled,not pop.,not cont. 320x240 320x240 @1966080 20
	def.format.video.nFrameWidth = WIDTH;
	def.format.video.nFrameHeight = HEIGHT;
	def.format.video.xFramerate = 30 << 16;
	def.format.video.nSliceHeight = def.format.video.nFrameHeight;
	def.format.video.nStride = def.format.video.nFrameWidth;
	def.format.video.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;

	print_def(&def, stdout);

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

#define INIT_OMX_TYPE_PTR(var_ptr, type, port) memset((var_ptr), 0, sizeof(type));\
	(var_ptr)->nSize = sizeof(type);\
	(var_ptr)->nVersion.nVersion = OMX_VERSION;\
	(var_ptr)->nPortIndex = (port);

int
capture_encode_loop(int frames, OMX_U32 frameWidth, OMX_U32 frameHeight, uint frameRate, OMX_COLOR_FORMATTYPE colorFormat, unsigned int bufsize) {
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
	if ((r_il = ilclient_create_component(client, &video_encode, "video_encode", 
		ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS | ILCLIENT_ENABLE_OUTPUT_BUFFERS)) != 0)
		ILC_ERR_EXIT("%s:%d: ilclient_create_component() for video_encode failed (%d)!\n")

	list[0] = video_encode;

	// get current settings of video_encode component from port 200
	INIT_OMX_TYPE(def, OMX_PARAM_PORTDEFINITIONTYPE, 200)

	if ((r = OMX_GetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamPortDefinition, &def)) != OMX_ErrorNone)
		OMX_ERR_EXIT("%s:%d: OMX_GetParameter() for video_encode port 200 failed with %x!\n")

	print_def(&def, stderr);
	// Port 200: in 1/1 115200 16 enabled,not pop.,not cont. 320x240 320x240 @1966080 20

	def.nBufferSize = bufsize;
	def.format.video.nFrameWidth = frameWidth;
	def.format.video.nFrameHeight = frameHeight;
	def.format.video.xFramerate = frameRate << 16; // 30 << 16;
	def.format.video.nSliceHeight = def.format.video.nFrameHeight;
	def.format.video.nStride = def.format.video.nFrameWidth;
	def.format.video.eColorFormat = colorFormat; // OMX_COLOR_FormatYUV420PackedPlanar;

	print_def(&def, stderr);

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
	if ((r_il = ilclient_enable_port_buffers(video_encode, 200, NULL, NULL, NULL)) != 0)
		ILC_ERR_EXIT("%s:%d: enabling port buffers for 200 failed (%d)!\n")

	fprintf(stderr, "enabling port buffers for 201...\n");
	if ((r_il = ilclient_enable_port_buffers(video_encode, 201, NULL, NULL, NULL)) != 0)
		ILC_ERR_EXIT("%s:%d: enabling port buffers for 201 failed (%d)!\n")

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

static void
get_portdef(OMX_PARAM_PORTDEFINITIONTYPE *portdef_ptr, COMPONENT_T *comp, OMX_U32 port, int image) {
	INIT_OMX_TYPE_PTR(portdef_ptr, OMX_PARAM_PORTDEFINITIONTYPE, port)
	OMX_GetParameter(ILC_GET_HANDLE(comp), OMX_IndexParamPortDefinition, portdef_ptr);
	print_def_(portdef_ptr, stderr, image);
}

static void
set_portdef(OMX_PARAM_PORTDEFINITIONTYPE *portdef_ptr, COMPONENT_T *comp, OMX_U32 port, int image) {
	OMX_ERRORTYPE r = OMX_SetParameter(ILC_GET_HANDLE(comp), OMX_IndexParamPortDefinition, portdef_ptr);
	vc_assert(r == OMX_ErrorNone);
	INIT_OMX_TYPE_PTR(portdef_ptr, OMX_PARAM_PORTDEFINITIONTYPE, port)
	OMX_GetParameter(ILC_GET_HANDLE(comp), OMX_IndexParamPortDefinition, portdef_ptr);
	print_def_(portdef_ptr, stderr, image);
}

static void 
disable_port_buffers(COMPONENT_T *comp, OMX_U32 port) {
	fprintf(stderr, "disabling port buffers for %d... ", port);
	ilclient_disable_port_buffers(comp, port, NULL, NULL, NULL);
	fprintf(stderr, "Done.\n");
}

//#define TUNNEL

//#define DEBUG

//#define INFO

#ifdef DEBUG
#define DEBUG_PRINT(x) fprintf(stderr, (x));
#define DEBUG_PRINT_1(x,y) fprintf(stderr, (x), (y));
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINT_1(x,y)
#endif /* DEBUG */

#ifdef INFO
#define INFO_PRINT_2(x,y,z) fprintf(stderr, (x), (y), (z));
#else
#define INFO_PRINT_2(x,y,z)
#endif /* INFO */

static int
image_decode_init(COMPONENT_T *image_decode, uint bufsize) {

	OMX_PARAM_PORTDEFINITIONTYPE portdef;
	int r_il;

	// get image_decode input port definition - port 320
	get_portdef(&portdef, image_decode, 320, 1);
	// set image_decode input buffer size and image format - port 320
	if (portdef.nBufferSize < bufsize)
		portdef.nBufferSize = bufsize;
	//portdef.nBufferCountMin = portdef.nBufferCountActual = 1;
	portdef.format.image.eCompressionFormat = OMX_IMAGE_CodingJPEG;
	set_portdef(&portdef, image_decode, 320, 1);

	int inputbuffernumber = portdef.nBufferCountActual;

	// get image_decode output port definition - port 321
	get_portdef(&portdef, image_decode, 321, 1);
	// set image_decode output image format - port 321
	portdef.format.image.eCompressionFormat = OMX_IMAGE_CodingAutoDetect;
	set_portdef(&portdef, image_decode, 321, 1);

	// create image_decode input buffers - port 320
	if ((r_il = ilclient_enable_port_buffers(image_decode, 320, NULL, NULL, NULL)) != 0)
		ILC_ERR_EXIT("%s:%d: enabling port buffers for 320 failed (%d)!\n")

	// move image_decode to executing
	fprintf(stderr, "move image_decode to executing\n");
	ilclient_change_component_state(image_decode, OMX_StateExecuting);

	return inputbuffernumber;
}

static void
video_encode_set_input_image_format(COMPONENT_T *video_encode, OMX_U32 frameWidth, OMX_U32 frameHeight, OMX_COLOR_FORMATTYPE colorFormat) {

	OMX_PARAM_PORTDEFINITIONTYPE portdef;

	// get current settings of video_encode component from port 200
	get_portdef(&portdef, video_encode, 200, 0);
	// set video_encode input image format - port 200
	portdef.format.video.nFrameWidth = frameWidth;
	portdef.format.video.nFrameHeight = frameHeight;
	portdef.format.video.nSliceHeight = portdef.format.video.nFrameHeight;
	portdef.format.video.nStride = portdef.format.video.nFrameWidth;
	portdef.format.video.eColorFormat = colorFormat; // OMX_COLOR_FormatYUV420PackedPlanar;
	set_portdef(&portdef, video_encode, 200, 0);
}

static void 
video_encode_init(COMPONENT_T *video_encode, OMX_U32 frameWidth, OMX_U32 frameHeight, OMX_COLOR_FORMATTYPE colorFormat) {

	OMX_VIDEO_PARAM_PORTFORMATTYPE format;
	OMX_ERRORTYPE r;
	int r_il;

	//// get current settings of video_encode component from port 200
	//get_portdef(&portdef, video_encode, 200, 0);
	//// set video_encode input frame rate - port 200
	//portdef.format.video.xFramerate = frameRate << 16; // 30 << 16;
	//set_portdef(&portdef, video_encode, 200, 0);

	// set video_encode input image format - port 200
	video_encode_set_input_image_format(video_encode, frameWidth, frameHeight, colorFormat);

	// set video_encode codec to H.264
	INIT_OMX_TYPE(format, OMX_VIDEO_PARAM_PORTFORMATTYPE, 201)
		format.eCompressionFormat = OMX_VIDEO_CodingAVC;
	r = OMX_SetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoPortFormat, &format);
	vc_assert(r == OMX_ErrorNone);

	// set current bitrate to 1Mbit
	OMX_VIDEO_PARAM_BITRATETYPE bitrateType;
	INIT_OMX_TYPE(bitrateType, OMX_VIDEO_PARAM_BITRATETYPE, 201)
		bitrateType.eControlRate = OMX_Video_ControlRateVariable;
	bitrateType.nTargetBitrate = 1000000;
	r = OMX_SetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoBitrate, &bitrateType);
	vc_assert(r == OMX_ErrorNone);

	// get current bitrate
	INIT_OMX_TYPE(bitrateType, OMX_VIDEO_PARAM_BITRATETYPE, 201)
		OMX_GetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoBitrate, &bitrateType);
	fprintf(stderr, "Current Bitrate=%u\n", bitrateType.nTargetBitrate);

	// create video_encode input buffers - port 200
	DEBUG_PRINT("create video_encode input buffers - port 200\n")
	if ((r_il = ilclient_enable_port_buffers(video_encode, 200, NULL, NULL, NULL)) != 0)
		ILC_ERR_EXIT("%s:%d: enabling port buffers for 200 failed (%d)!\n")

	// create video_encode output buffers - port 201
	DEBUG_PRINT("create video_encode output buffers - port 201\n")
	if ((r_il = ilclient_enable_port_buffers(video_encode, 201, NULL, NULL, NULL)) != 0)
		ILC_ERR_EXIT("%s:%d: enabling port buffers for 201 failed (%d)!\n")

	// move video_encode to executing
	fprintf(stderr, "move video_encode to executing\n");
	ilclient_change_component_state(video_encode, OMX_StateExecuting);
}

static void 
capture_encode_jpeg_port_settings(void *userdata, COMPONENT_T *comp, OMX_U32 port) {

	TUNNEL_T *tunnel = (TUNNEL_T *)userdata;

	if (port == 321 && comp == /*image_decode*/tunnel->source) {
		int r_il;
		OMX_PARAM_PORTDEFINITIONTYPE portdef;

		// get image_decode output port definition - port 321
		DEBUG_PRINT("get image_decode output port definition - port 321\n")
		get_portdef(&portdef, comp, 321, 1);

		video_encode_init(/*video_encode*/tunnel->sink, portdef.format.image.nFrameWidth, portdef.format.image.nFrameHeight, portdef.format.image.eColorFormat);

#ifdef TUNNEL
		fprintf(stderr, "setup 321 -> 200 tunnel\n");
		// setup tunnel
		if ((r_il = ilclient_setup_tunnel(tunnel, 0, 0)) != 0)
			fprintf(stderr, "Error setting up tunnel: %d\n", r_il);
		// enable ports
		//if ((r_il = ilclient_enable_tunnel(tunnel)) != 0)
		//	fprintf(stderr, "Error enabling tunnel: %d\n", r_il);
#else
		// allocate buffers for port 321
		DEBUG_PRINT("allocate buffers for port 321\n")
		if ((r_il = ilclient_enable_port_buffers(comp, 321, NULL, NULL, NULL)) != 0)
			ILC_ERR_EXIT("%s:%d: enabling port buffers for 321 failed (%d)!\n")
		// move image_decode to executing
		//fprintf(stderr, "move image_decode to executing\n");
		//ilclient_change_component_state(comp, OMX_StateExecuting);
#endif /* TUNNEL */
	}
}

static OMX_BUFFERHEADERTYPE *
tunnel_buffer(COMPONENT_T *image_decode, COMPONENT_T *video_encode, int *copybuffernumber, int block) {
#ifndef TUNNEL
	OMX_ERRORTYPE r;
	OMX_BUFFERHEADERTYPE *buf;
	OMX_BUFFERHEADERTYPE *out;

	DEBUG_PRINT("4. get 321 out buffers from image_decode queue\n")
	while ((out = ilclient_get_output_buffer(image_decode, 321, block)) != NULL && out->nFilledLen == 0) {
		DEBUG_PRINT("5. send empty 321 out buffer to image_decode processor\n")
		if ((r = OMX_FillThisBuffer(ILC_GET_HANDLE(image_decode), out)) != OMX_ErrorNone) {
			fprintf(stderr, "Error filling 321 out buffer: %x\n", r);
		}
	}
	if (out != NULL) {
		DEBUG_PRINT("6. get 200 in buffer from video_encode queue (blocking)\n")
		buf = ilclient_get_input_buffer(video_encode, 200, 1);

		/* fill it */
		//generate_test_card(buf->pBuffer, &buf->nFilledLen, framenumber++);
		DEBUG_PRINT_1("7. copy buffer 321->200 (%d bytes)\n", out->nFilledLen)
		memcpy(buf->pBuffer, out->pBuffer, out->nFilledLen);
		buf->nFilledLen = out->nFilledLen;
		out->nFilledLen = 0;
		(*copybuffernumber)++;
		INFO_PRINT_2("copied frame %d (%d bytes)\n", (*copybuffernumber), buf->nFilledLen)

		DEBUG_PRINT("8. send emptied 321 out buffer to image_decode processor\n")
		if ((r = OMX_FillThisBuffer(ILC_GET_HANDLE(image_decode), out)) != OMX_ErrorNone) {
			fprintf(stderr, "Error filling 321 out buffer: %x\n", r);
		}

		DEBUG_PRINT("9. send filled 200 in buffer to video_encode processor\n")
		if ((r = OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_encode), buf)) != OMX_ErrorNone) {
			fprintf(stderr, "Error emptying 200 in buffer: %x!\n", r);
		}
	}

	return out;

#endif /* TUNNEL */
}

typedef struct _FILL_BUFFER_DONE_DATA {
	TUNNEL_T *tunnel;
	int      *copybuffernumber;
} FILL_BUFFER_DONE_DATA;

static void
capture_encode_jpeg_fill_buffer_done(void *data, COMPONENT_T *comp) {
	FILL_BUFFER_DONE_DATA *filldata = (FILL_BUFFER_DONE_DATA *)data;
	if (comp == /*image_decode*/filldata->tunnel->source)
		tunnel_buffer(comp, /*video_encode*/filldata->tunnel->sink, filldata->copybuffernumber, 0);
}

static uint 
buffer_list_count(OMX_BUFFERHEADERTYPE *list)
{
	uint cnt = 0;
	while (list) {
		cnt++;
		list = list->pAppPrivate;
	}
	return cnt;
}

static OMX_BUFFERHEADERTYPE *
take_buffer_out_of_list(OMX_BUFFERHEADERTYPE **list) {
	/* take a buffer out of list */
	OMX_BUFFERHEADERTYPE *buf = *list;
	*list = buf->pAppPrivate;
	buf->pAppPrivate = NULL;
	return buf;
}

static int
get_input_buffers(COMPONENT_T *image_decode, OMX_U32 port, int block, int buffernumber, OMX_BUFFERHEADERTYPE **list) {
	OMX_BUFFERHEADERTYPE *buf;
	int cnt = buffer_list_count(*list);
	while (cnt < buffernumber && (buf = ilclient_get_input_buffer(image_decode, port, block)) != NULL) {
		buf->pAppPrivate = *list;
		*list = buf;
		cnt++;
	}
	return cnt;
}

int
capture_encode_jpeg_loop(int frames, /*OMX_U32 frameWidth, OMX_U32 frameHeight, uint frameRate, OMX_COLOR_FORMATTYPE colorFormat,*/ uint bufsize) {
	COMPONENT_T *video_encode = NULL, *image_decode = NULL;
	COMPONENT_T *list[5];
	TUNNEL_T tunnel[4];
	OMX_BUFFERHEADERTYPE *buf;
	OMX_BUFFERHEADERTYPE *out;
	OMX_ERRORTYPE r;
	int r_il = 0;
	ILCLIENT_T *client;
	int status = 0;
	int framenumber = 0, outframenumber = 0, copybuffernumber = 0;
	struct timespec diff;

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
	if ((r_il = ilclient_create_component(client, &image_decode, "image_decode", ILCLIENT_DISABLE_ALL_PORTS
		| ILCLIENT_ENABLE_INPUT_BUFFERS
#ifndef TUNNEL
		| ILCLIENT_ENABLE_OUTPUT_BUFFERS
#endif /* TUNNEL */
		)) != 0)
		ILC_ERR_EXIT("%s:%d: ilclient_create_component() for image_decode failed (%d)!\n")
	list[0] = image_decode;

	// move image_decode to idle
	ilclient_change_component_state(image_decode, OMX_StateIdle);

	int inputbuffernumber = image_decode_init(image_decode, bufsize);

	// create video_encode
	if ((r_il = ilclient_create_component(client, &video_encode, "video_encode", ILCLIENT_DISABLE_ALL_PORTS
#ifndef TUNNEL
		| ILCLIENT_ENABLE_INPUT_BUFFERS
#endif /* TUNNEL */
		| ILCLIENT_ENABLE_OUTPUT_BUFFERS
		)) != 0)
		ILC_ERR_EXIT("%s:%d: ilclient_create_component() for video_encode failed (%d)!\n")
	list[1] = video_encode;

	// move video_encode to idle
	ilclient_change_component_state(video_encode, OMX_StateIdle);

	//video_encode_init(video_encode, frameWidth, frameHeight, colorFormat);

	// create tunnel 321 -> 200
	set_tunnel(tunnel, image_decode, 321, video_encode, 200);

	// set set_port_settings_callback
	ilclient_set_port_settings_callback(client, capture_encode_jpeg_port_settings, tunnel);

	// set set_port_settings_callback
	FILL_BUFFER_DONE_DATA filldata;
	filldata.tunnel = tunnel;
	filldata.copybuffernumber = &copybuffernumber;
	ilclient_set_fill_buffer_done_callback(client, capture_encode_jpeg_fill_buffer_done, &filldata);

	OMX_BUFFERHEADERTYPE *inputbufferlist = NULL;

	do {
		struct timespec capture_time;

		//DEBUG_PRINT("1. move image_decode to executing\n")
		//ilclient_change_component_state(image_decode, OMX_StateExecuting);

		DEBUG_PRINT("2. get 320 in buffer from image_decode queue\n")
		//if ((buf = ilclient_get_input_buffer(image_decode, 320, 0)) != NULL) {
		//	vc_assert(buf->nAllocLen >= bufsize);
		//
		if (get_input_buffers(image_decode, 320, 0, inputbuffernumber, &inputbufferlist) == inputbuffernumber) {

			if (framenumber < frames) {

				/* take a buffer out of inputbufferlist */
				buf = take_buffer_out_of_list(&inputbufferlist);

				/* fill it */
				//generate_test_card(buf->pBuffer, &buf->nFilledLen, framenumber++);
				capture_frame(buf->pBuffer, &buf->nFilledLen);
				buf->nFlags = OMX_BUFFERFLAG_EOS;
				framenumber++;
				clock_gettime(CLOCK_MONOTONIC, &capture_time);
				INFO_PRINT_2("captured frame %d (%d bytes)\n", framenumber, buf->nFilledLen)

				DEBUG_PRINT("3. send filled 320 in buffer to image_decode processor\n")
				if ((r = OMX_EmptyThisBuffer(ILC_GET_HANDLE(image_decode), buf)) != OMX_ErrorNone)
					fprintf(stderr, "Error emptying buffer: %x\n", r);
			}
		}

		OMX_STATETYPE state;
		// check component is in the right state to accept buffers
		DEBUG_PRINT("check video_encode component is in the right state to accept buffers\n")
		if (OMX_GetState(ILC_GET_HANDLE(video_encode), &state) != OMX_ErrorNone || state != OMX_StateExecuting)
			continue;

		while (tunnel_buffer(image_decode, video_encode, &copybuffernumber, 0) != NULL)
			wait_timeout(0, 5);

		DEBUG_PRINT("10. get 201 out buffers from video_encode queue\n")
		while ((out = ilclient_get_output_buffer(video_encode, 201, 0)) != NULL && out->nFilledLen == 0) {
			DEBUG_PRINT("11. send empty 201 out buffer to video_encode processor\n")
			if ((r = OMX_FillThisBuffer(ILC_GET_HANDLE(video_encode), out)) != OMX_ErrorNone) {
				fprintf(stderr, "Error filling 201 out buffer: %x\n", r);
			}
		}
		if (out != NULL) {

#ifdef DEBUG
			if (out->nFlags & OMX_BUFFERFLAG_CODECCONFIG) {
				int i;
				for (i = 0; i < out->nFilledLen; i++)
					fprintf(stderr, "%x ", out->pBuffer[i]);
				fprintf(stderr, "\n");
			}
#endif

			DEBUG_PRINT_1("12. write frame to stdout (%d bytes)\n", out->nFilledLen)
			if ((r = fwrite(out->pBuffer, 1, out->nFilledLen, stdout)) != out->nFilledLen) {
				fprintf(stderr, "fwrite: Error writing buffer to stdout: %d!\n", r);
			}
			else {
				outframenumber++;
				INFO_PRINT_2("output frame %d (%d bytes)\n", outframenumber, out->nFilledLen)
			}
			fflush(stdout);
			out->nFilledLen = 0;

			DEBUG_PRINT("13. send emptied 201 out buffer to video_encode processor\n")
			if ((r = OMX_FillThisBuffer(ILC_GET_HANDLE(video_encode), out)) != OMX_ErrorNone) {
				fprintf(stderr, "Error filling 201 out buffer: %x\n", r);
			}
		}

		if (framenumber >= frames) {
			get_time_diff(&capture_time, &diff);
			wait_timeout(0, 1000);
			fprintf(stderr, "\r%.2f ", (1000000000.0 * diff.tv_sec + diff.tv_nsec) / 1000000000.0);
		}
	}
	while (framenumber < frames || out != NULL || diff.tv_sec < 1);

	fprintf(stderr, "\r          \ninput frames: %d\ncopied frames: %d\noutput frames: %d\n\n", framenumber, copybuffernumber, outframenumber);

	fprintf(stderr, "Teardown.\n");

	// remove callback function
	ilclient_set_fill_buffer_done_callback(client, NULL, NULL);

	// release empty port 320 input buffers back to image_decode processor
	DEBUG_PRINT("release empty port 320 input buffers back to image_decode processor\n")
	while (inputbufferlist) {
		/* take a buffer out of inputbufferlist */
		buf = take_buffer_out_of_list(&inputbufferlist);
		/* release it */
		if ((r = OMX_EmptyThisBuffer(ILC_GET_HANDLE(image_decode), buf)) != OMX_ErrorNone)
			fprintf(stderr, "Error emptying buffer: %x\n", r);
	}

	disable_port_buffers(image_decode, 320);
	disable_port_buffers(image_decode, 321);
	disable_port_buffers(video_encode, 200);
	disable_port_buffers(video_encode, 201);

#ifdef TUNNEL
	ilclient_disable_tunnel(tunnel);
	ilclient_teardown_tunnels(tunnel);
#endif /* TUNNEL */

	ilclient_state_transition(list, OMX_StateIdle);
	ilclient_state_transition(list, OMX_StateLoaded);

	ilclient_cleanup_components(list);

	OMX_Deinit();

	ilclient_destroy(client);
	return status;
}

//int
//main(int argc, char **argv) {
//	if (argc < 2) {
//		printf("Usage: %s <filename>\n", argv[0]);
//		exit(1);
//	}
//	bcm_host_init();
//	return video_encode_test(argv[1]);
//	bcm_host_deinit();
//}
