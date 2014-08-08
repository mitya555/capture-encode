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
#include <signal.h>

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
	fprintf(out, " Video: %ux%u %ux%u fmt=%u %u @%u\n",
			def_ptr->format.video.nFrameWidth,
			def_ptr->format.video.nFrameHeight,
			def_ptr->format.video.nStride,
			def_ptr->format.video.nSliceHeight,
			def_ptr->format.video.eCompressionFormat,
			def_ptr->format.video.eColorFormat,
			def_ptr->format.video.xFramerate >> 16);
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

extern OMX_BUFFERHEADERTYPE *
capture_frame(OMX_BUFFERHEADERTYPE *buf_list);

#define OMX_ERR_EXIT(fmt_str) {\
		fprintf(stderr, fmt_str, __FUNCTION__, __LINE__, r);\
		exit(1);\
	}

#define ILC_ERR_EXIT(fmt_str) {\
		fprintf(stderr, fmt_str, __FUNCTION__, __LINE__, r_il);\
		exit(1);\
	}

#define INIT_OMX_TYPE_NO_PORT(var, type) memset(&(var), 0, sizeof(type));\
	(var).nSize = sizeof(type);\
	(var).nVersion.nVersion = OMX_VERSION;

#define INIT_OMX_TYPE(var, type, port) INIT_OMX_TYPE_NO_PORT(var, type)\
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
			capture_frame(buf);
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
	get_portdef(portdef_ptr, comp, port, image);
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
#define DEBUG_PRINT_3(s,x,y,z) fprintf(stderr, (s), (x), (y), (z));
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINT_1(x,y)
#define DEBUG_PRINT_3(s,x,y,z)
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
video_encode_set_input_image_format(OMX_PARAM_PORTDEFINITIONTYPE *portdef_ptr, COMPONENT_T *video_encode, OMX_U32 frameWidth, OMX_U32 frameHeight, OMX_COLOR_FORMATTYPE colorFormat) {
	// get current settings of video_encode component from port 200
	get_portdef(portdef_ptr, video_encode, 200, VC_FALSE);
	// set video_encode input image format - port 200
	portdef_ptr->format.video.nFrameWidth =  frameWidth;
	portdef_ptr->format.video.nFrameHeight = frameHeight;
	portdef_ptr->format.video.nSliceHeight = portdef_ptr->format.video.nFrameHeight;
	portdef_ptr->format.video.nStride =      portdef_ptr->format.video.nFrameWidth;
	portdef_ptr->format.video.eColorFormat = colorFormat; // OMX_COLOR_FormatYUV420PackedPlanar;
	set_portdef(portdef_ptr, video_encode, 200, VC_FALSE);
}

static void
write_media_set_input_video_format(OMX_PARAM_PORTDEFINITIONTYPE *portdef_ptr, COMPONENT_T *write_media, OMX_U32 frameWidth, OMX_U32 frameHeight, OMX_VIDEO_CODINGTYPE codingType) {
	// get current settings of write_media component from port 171
	get_portdef(portdef_ptr, write_media, 171, VC_FALSE);
	// set write_media input video format - port 171
	portdef_ptr->format.video.nFrameWidth = frameWidth;
	portdef_ptr->format.video.nFrameHeight = frameHeight;
	//portdef_ptr->format.video.nSliceHeight = portdef_ptr->format.video.nFrameHeight;
	//portdef_ptr->format.video.nStride = portdef_ptr->format.video.nFrameWidth;
	//portdef_ptr->format.video.eColorFormat = colorFormat; // OMX_COLOR_FormatYUV420PackedPlanar;
	portdef_ptr->format.video.eCompressionFormat = codingType;
	set_portdef(portdef_ptr, write_media, 171, VC_FALSE);
}

extern int
psips, bitrate, codec;

extern char *write_media_file;

static void 
video_encode_init(COMPONENT_T *video_encode) {

	OMX_VIDEO_PARAM_PORTFORMATTYPE format;
	OMX_ERRORTYPE r;

	//// get current settings of video_encode component from port 200
	//get_portdef(&portdef, video_encode, 200, 0);
	//// set video_encode input frame rate - port 200
	//portdef.format.video.xFramerate = frameRate << 16; // 30 << 16;
	//set_portdef(&portdef, video_encode, 200, 0);

	// set video_encode codec to H.264
	INIT_OMX_TYPE(format, OMX_VIDEO_PARAM_PORTFORMATTYPE, 201)
	switch (codec) {
		case  0: format.eCompressionFormat = OMX_VIDEO_CodingMPEG2; break;      /**< AKA: H.262 */
		case  1: format.eCompressionFormat = OMX_VIDEO_CodingH263; break;       /**< H.263 */
		case  2: format.eCompressionFormat = OMX_VIDEO_CodingMPEG4; break;      /**< MPEG-4 */
		case  3: format.eCompressionFormat = OMX_VIDEO_CodingWMV; break;        /**< all versions of Windows Media Video */
		case  4: format.eCompressionFormat = OMX_VIDEO_CodingRV; break;         /**< all versions of Real Video */
		case  5: format.eCompressionFormat = OMX_VIDEO_CodingAVC; break;        /**< H.264/AVC */
		case  6: format.eCompressionFormat = OMX_VIDEO_CodingMJPEG; break;      /**< Motion JPEG */
		case  7: format.eCompressionFormat = OMX_VIDEO_CodingVP6; break;        /**< On2 VP6 */
		case  8: format.eCompressionFormat = OMX_VIDEO_CodingVP7; break;        /**< On2 VP7 */
		case  9: format.eCompressionFormat = OMX_VIDEO_CodingVP8; break;        /**< On2 VP8 */
		case 10: format.eCompressionFormat = OMX_VIDEO_CodingYUV; break;        /* raw YUV video */
		case 11: format.eCompressionFormat = OMX_VIDEO_CodingSorenson; break;   /**< Sorenson */
		case 12: format.eCompressionFormat = OMX_VIDEO_CodingTheora; break;     /**< Theora */
		case 13: format.eCompressionFormat = OMX_VIDEO_CodingMVC; break;        /**< H.264/MVC */
		default: format.eCompressionFormat = OMX_VIDEO_CodingAVC; break;        /**< H.264/AVC */
	}
	r = OMX_SetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoPortFormat, &format);
	vc_assert(r == OMX_ErrorNone);

	OMX_VIDEO_PARAM_BITRATETYPE bitrateType;
	if (bitrate) {
		// set current bitrate to 1Mbit // 0 that means that the output will either use VBR, or no rate control at all
		INIT_OMX_TYPE(bitrateType, OMX_VIDEO_PARAM_BITRATETYPE, 201)
		bitrateType.eControlRate = OMX_Video_ControlRateVariable;
		bitrateType.nTargetBitrate = bitrate; // 1000000; // 0;
		r = OMX_SetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoBitrate, &bitrateType);
		vc_assert(r == OMX_ErrorNone);
	}
	// get current bitrate
	INIT_OMX_TYPE(bitrateType, OMX_VIDEO_PARAM_BITRATETYPE, 201)
	OMX_GetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoBitrate, &bitrateType);
	fprintf(stderr, "Current Bitrate=%u\n", bitrateType.nTargetBitrate);

	// set psips
	if (psips) {
		OMX_CONFIG_PORTBOOLEANTYPE portBoolType;
		INIT_OMX_TYPE(portBoolType, OMX_CONFIG_PORTBOOLEANTYPE, 201)
		r = OMX_SetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamBrcmVideoAVCInlineHeaderEnable, &portBoolType);
		vc_assert(r == OMX_ErrorNone);
	}
}

static COMPONENT_T *_comp[5];
static TUNNEL_T     _tunnel[4];
static int          _copybuffernumber;

static void
capture_encode_jpeg_error_callback(void *userdata, COMPONENT_T *comp, OMX_U32 error) {
	INFO_PRINT_2("Error: 0x%X (%s)\n", error, comp == _tunnel->source ? "image_decode" : comp == _tunnel->sink ? "video_encode" : "write_media");
}

static void 
capture_encode_jpeg_port_settings_callback(void *userdata, COMPONENT_T *comp, OMX_U32 port) {

	DEBUG_PRINT_1("port settings changed event - port %d\n", port)

	if (port == 321 && comp == /*image_decode*/_tunnel->source) {
		int r_il;
		OMX_PARAM_PORTDEFINITIONTYPE portdef;

		// get image_decode output port definition - port 321
		DEBUG_PRINT("get image_decode output port definition - port 321\n")
		get_portdef(&portdef, comp, 321, VC_TRUE);

		// set video_encode input image format - port 200
		video_encode_set_input_image_format(&portdef, /*video_encode*/_tunnel->sink, portdef.format.image.nFrameWidth, portdef.format.image.nFrameHeight, portdef.format.image.eColorFormat);

		video_encode_init(/*video_encode*/_tunnel->sink);

#ifdef TUNNEL
		fprintf(stderr, "setup 321 -> 200 tunnel\n");
		// setup tunnel
		if ((r_il = ilclient_setup_tunnel(_tunnel, 0, 0)) != 0)
			fprintf(stderr, "Error setting up tunnel: %d\n", r_il);
#else
		// create image_decode output buffers - port 321
		DEBUG_PRINT("create image_decode output buffers - port 321\n")
		if ((r_il = ilclient_enable_port_buffers(comp, 321, NULL, NULL, NULL)) != 0)
			ILC_ERR_EXIT("%s:%d: enabling port buffers for 321 failed (%d)!\n")

		// create video_encode input buffers - port 200
		DEBUG_PRINT("create video_encode input buffers - port 200\n")
		if ((r_il = ilclient_enable_port_buffers(/*video_encode*/_tunnel->sink, 200, NULL, NULL, NULL)) != 0)
			ILC_ERR_EXIT("%s:%d: enabling port buffers for 200 failed (%d)!\n")

		// create video_encode output buffers - port 201
		DEBUG_PRINT("create video_encode output buffers - port 201\n")
		if ((r_il = ilclient_enable_port_buffers(/*video_encode*/_tunnel->sink, 201, NULL, NULL, NULL)) != 0)
			ILC_ERR_EXIT("%s:%d: enabling port buffers for 201 failed (%d)!\n")
#endif /* TUNNEL */

		if (write_media_file) {

			get_portdef(&portdef, /*video_encode*/(_tunnel + 1)->source, 201, VC_FALSE);

			write_media_set_input_video_format(&portdef, /*write_media*/(_tunnel + 1)->sink, portdef.format.video.nFrameWidth, portdef.format.video.nFrameHeight, portdef.format.video.eCompressionFormat);

			OMX_ERRORTYPE r;
			typedef struct _OMX_PARAM_CONTENTURITYPE
			{
				OMX_U32 nSize;            /**< size of the structure in bytes, including actual URI name */
				OMX_VERSIONTYPE nVersion; /**< OMX specification version information */
				OMX_U8 contentURI[500];   /**< The URI name */
			} _OMX_PARAM_CONTENTURITYPE;
			_OMX_PARAM_CONTENTURITYPE contentUri;
			INIT_OMX_TYPE_NO_PORT(contentUri, _OMX_PARAM_CONTENTURITYPE)
			strcpy((char *)contentUri.contentURI, write_media_file);
			contentUri.nSize -= (500 - strlen(write_media_file));
			if ((r = OMX_SetParameter(ILC_GET_HANDLE(/*write_media*/(_tunnel + 1)->sink), OMX_IndexParamContentURI, &contentUri)) != OMX_ErrorNone)
				OMX_ERR_EXIT("%s:%d: OMX_SetParameter() for content URI for write_media failed with %x!\n")

#ifdef TUNNEL
			fprintf(stderr, "setup 201 -> 171 tunnel\n");
			// setup tunnel
			if ((r_il = ilclient_setup_tunnel(_tunnel + 1, 0, 0)) != 0)
				fprintf(stderr, "Error setting up tunnel: %d\n", r_il);
#else
			// create write_media input buffers - port 171
			DEBUG_PRINT("create write_media input buffers - port 171\n")
			if ((r_il = ilclient_enable_port_buffers(/*write_media*/(_tunnel + 1)->sink, 171, NULL, NULL, NULL)) != 0)
				ILC_ERR_EXIT("%s:%d: enabling port buffers for 171 failed (%d)!\n")
#endif /* TUNNEL */
		}

		// move all components except image_decode to executing
		fprintf(stderr, "move video_encode %sto executing\n", write_media_file ? "and write_media " : "");
		ilclient_state_transition(_comp + 1, OMX_StateExecuting);
	}
}

static OMX_U8  *_swap;

static OMX_BUFFERHEADERTYPE *
tunnel_buffer(TUNNEL_T *tunnel, int *copybuffernumber, int block) {
#ifndef TUNNEL
	OMX_ERRORTYPE r;
	OMX_BUFFERHEADERTYPE *buf;
	OMX_BUFFERHEADERTYPE *out;

	DEBUG_PRINT_1("4. get %d out buffers from output component queue\n", tunnel->source_port)
	while ((out = ilclient_get_output_buffer(tunnel->source, tunnel->source_port, block)) != NULL && out->nFilledLen == 0) {
		DEBUG_PRINT_1("5. send empty %d out buffer to output component processor\n", tunnel->source_port)
		if ((r = OMX_FillThisBuffer(ILC_GET_HANDLE(tunnel->source), out)) != OMX_ErrorNone)
			fprintf(stderr, "Error filling %d out buffer: %x\n", tunnel->source_port, r);
	}
	if (out != NULL) {
		DEBUG_PRINT_1("6. get %d in buffer from input component queue (blocking)\n", tunnel->sink_port)
		buf = ilclient_get_input_buffer(tunnel->sink, tunnel->sink_port, VC_TRUE);

		/* fill it */
		//generate_test_card(buf->pBuffer, &buf->nFilledLen, framenumber++);
		DEBUG_PRINT_3("7. copy buffer %d->%d (%d bytes)\n", tunnel->source_port, tunnel->sink_port, out->nFilledLen)

		//memcpy(buf->pBuffer, out->pBuffer, out->nFilledLen);
		_swap = buf->pBuffer;
		buf->pBuffer = out->pBuffer;
		out->pBuffer = _swap;
		_swap = NULL;

		buf->nFilledLen = out->nFilledLen;
		out->nFilledLen = 0;
		if (copybuffernumber) {
			(*copybuffernumber)++;
			INFO_PRINT_2("copied frame %d (%d bytes)\n", (*copybuffernumber), buf->nFilledLen)
		}

		DEBUG_PRINT_1("8. send emptied %d out buffer to output component processor\n", tunnel->source_port)
		if ((r = OMX_FillThisBuffer(ILC_GET_HANDLE(tunnel->source), out)) != OMX_ErrorNone)
			fprintf(stderr, "Error filling %d out buffer: %x\n", tunnel->source_port, r);

		DEBUG_PRINT_1("9. send filled %d in buffer to input component processor\n", tunnel->sink_port)
		if ((r = OMX_EmptyThisBuffer(ILC_GET_HANDLE(tunnel->sink), buf)) != OMX_ErrorNone)
			fprintf(stderr, "Error emptying %d in buffer: %x!\n", tunnel->sink_port, r);
	}

	return out;

#else
	OMX_ERRORTYPE r;
	OMX_BUFFERHEADERTYPE *buf;
	OMX_BUFFERHEADERTYPE *out;

	DEBUG_PRINT_1("4. get %d out buffers from output component queue\n", tunnel->source_port)
	while ((out = ilclient_get_output_buffer(, tunnel->source, , tunnel->source_port, block)) != NULL) {
		DEBUG_PRINT_1("5. send %d out buffer to output component processor\n", tunnel->source_port)
		if ((r = OMX_FillThisBuffer(ILC_GET_HANDLE(tunnel->source), out)) != OMX_ErrorNone)
			fprintf(stderr, "Error filling %d out buffer: %x\n", tunnel->source_port, r);
	}
	DEBUG_PRINT_1("6. get %d in buffers from input component queue\n", tunnel->sink_port)
	while ((buf = ilclient_get_input_buffer(tunnel->sink, tunnel->sink_port, block)) != NULL) {
		DEBUG_PRINT_1("7. send %d in buffer to input component processor\n", tunnel->sink_port)
		if ((r = OMX_EmptyThisBuffer(ILC_GET_HANDLE(tunnel->sink), buf)) != OMX_ErrorNone)
			fprintf(stderr, "Error emptying %d in buffer: %x!\n", tunnel->sink_port, r);
	}

	return NULL;

#endif /* TUNNEL */
}

static void
wait_tunnel_buffer(TUNNEL_T *tunnel, int *copybuffernumber) {
	while (tunnel_buffer(tunnel, copybuffernumber, VC_FALSE) != NULL)
		wait_timeout(0, 5);
}

static void
capture_encode_jpeg_fill_buffer_done_callback(void *data, COMPONENT_T *comp) {
	if (comp == /*image_decode*/_tunnel->source)
		tunnel_buffer(_tunnel, &_copybuffernumber, VC_FALSE);
	else if (write_media_file && comp == /*video_encode*/(_tunnel + 1)->source)
		tunnel_buffer(_tunnel + 1, NULL, VC_FALSE);
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
buffer_list_get_buf_remove(OMX_BUFFERHEADERTYPE **list, OMX_BUFFERHEADERTYPE *buf) {
	/* take the buf buffer out of the list */
	if (list != NULL && *list != NULL) {
		if (buf == *list) {
			*list = buf->pAppPrivate;
			buf->pAppPrivate = NULL;
			return buf;
		}
		else {
			OMX_BUFFERHEADERTYPE *ptr = *list;
			while (ptr != NULL && ptr->pAppPrivate != buf)
				ptr = ptr->pAppPrivate;
			if (ptr != NULL) {
				ptr->pAppPrivate = buf->pAppPrivate;
				buf->pAppPrivate = NULL;
				return buf;
			}
		}
	}
	return NULL;
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

static void
wait_for_StateExecuting(COMPONENT_T *comp) {
	// check component is in the right state to accept buffers
	OMX_STATETYPE state;
	while (OMX_GetState(ILC_GET_HANDLE(comp), &state) != OMX_ErrorNone || state != OMX_StateExecuting)
		wait_timeout(0, 5);
}

extern void 
close_device(void),
open_device(void), 
uninit_device(int external_buffers), 
init_buffers(unsigned int buffer_size, OMX_BUFFERHEADERTYPE *external_buffers),
stop_capturing(void), 
start_capturing(void);

extern unsigned int 
init_device(void);

static ILCLIENT_T           *_client;
static OMX_U32               _port[5][2][3];
static OMX_BUFFERHEADERTYPE *_inputbufferlist;
static int                   _framenumber, _outframenumber;
static int                   _torndown;

static void
release_input_buffers(void) {
	OMX_BUFFERHEADERTYPE *buf;
	OMX_ERRORTYPE r;

	// release empty port 320 input buffers back to image_decode processor
	DEBUG_PRINT("release empty port 320 input buffers back to image_decode processor\n")
	while (_inputbufferlist) {
		/* take a buffer out of inputbufferlist */
		buf = buffer_list_get_buf_remove(&_inputbufferlist, _inputbufferlist);
		DEBUG_PRINT_1("buffer 0x%x ", (unsigned int)buf)
		/* release it */
		if ((r = OMX_EmptyThisBuffer(ILC_GET_HANDLE(_comp[0]), buf)) != OMX_ErrorNone)
			fprintf(stderr, "Error emptying buffer: %x\n", r);
		DEBUG_PRINT("released\n")
	}
}

static void
capture_encode_jpeg_teardown(void) {
	if (_torndown) {
		fprintf(stderr, "Torn down.\n");
		return;
	}

	stop_capturing();
	uninit_device(1);
	close_device();

	fprintf(stderr, "\r          \ninput frames: %d\ncopied frames: %d\noutput frames: %d\n\n", _framenumber, _copybuffernumber, _outframenumber);

	fprintf(stderr, "Teardown.\n");

	// remove callback functions
	ilclient_set_fill_buffer_done_callback(_client, NULL, NULL);
	ilclient_set_port_settings_callback(_client, NULL, NULL);
	ilclient_set_error_callback(_client, NULL, NULL);

	// release empty port 320 input buffers back to image_decode processor
	release_input_buffers();

	for (int i_comp = 0; i_comp < 5 && _comp[i_comp]; i_comp++)
		for (int i_port_inout = 0; i_port_inout < 2; i_port_inout++)
			for (int i_port = 0; i_port < 3 && _port[i_comp][i_port_inout][i_port]; i_port++)
				disable_port_buffers(_comp[i_comp], _port[i_comp][i_port_inout][i_port]);

#ifdef TUNNEL
	ilclient_disable_tunnel(_tunnel);
	ilclient_teardown_tunnels(_tunnel);
#endif /* TUNNEL */

	ilclient_state_transition(_comp, OMX_StateIdle);
	ilclient_state_transition(_comp, OMX_StateLoaded);

	ilclient_cleanup_components(_comp);

	if (_swap)
		free(_swap);

	OMX_Deinit();

	ilclient_destroy(_client);

	bcm_host_deinit();

	_torndown = 1;
}

static void 
intHandler(int dummy) { exit(0); }

int
capture_encode_jpeg_loop(int frames/*, OMX_U32 frameWidth, OMX_U32 frameHeight, uint frameRate, OMX_COLOR_FORMATTYPE colorFormat, uint bufsize*/) {
	COMPONENT_T *video_encode = NULL, *image_decode = NULL;
	COMPONENT_T *write_media = NULL;
	OMX_BUFFERHEADERTYPE *buf;
	OMX_BUFFERHEADERTYPE *out;
	OMX_ERRORTYPE r;
	int r_il = 0;
	int status = 0;
	struct timespec diff;

	memset(_comp, 0, sizeof(_comp));
	memset(_port, 0, sizeof(_port));
	_framenumber = _outframenumber = _copybuffernumber = 0;
	_torndown = 0;

	bcm_host_init();

	if ((_client = ilclient_init()) == NULL) {
		fprintf(stderr, "ilclient_init() for video_encode failed!\n");
		return -3;
	}

	if ((r = OMX_Init()) != OMX_ErrorNone) {
		ilclient_destroy(_client);
		fprintf(stderr, "OMX_Init() for video_encode failed with %x!\n", r);
		return -4;
	}

	open_device();

	atexit(capture_encode_jpeg_teardown);
	signal(SIGINT, intHandler);

	// create image_decode
	if ((r_il = ilclient_create_component(_client, &image_decode, "image_decode", ILCLIENT_DISABLE_ALL_PORTS
		| ILCLIENT_ENABLE_INPUT_BUFFERS
#ifndef TUNNEL
		| ILCLIENT_ENABLE_OUTPUT_BUFFERS
#endif /* TUNNEL */
		)) != 0)
		ILC_ERR_EXIT("%s:%d: ilclient_create_component() for image_decode failed (%d)!\n")
	_comp[0] = image_decode;
	_port[0][0][0] = 320;
	_port[0][1][0] = 321;

	// create video_encode
	if ((r_il = ilclient_create_component(_client, &video_encode, "video_encode", ILCLIENT_DISABLE_ALL_PORTS
#ifndef TUNNEL
		| ILCLIENT_ENABLE_INPUT_BUFFERS
		| ILCLIENT_ENABLE_OUTPUT_BUFFERS
#endif /* TUNNEL */
		)) != 0)
		ILC_ERR_EXIT("%s:%d: ilclient_create_component() for video_encode failed (%d)!\n")
	_comp[1] = video_encode;
	_port[1][0][0] = 200;
	_port[1][1][0] = 201;

	// create tunnel 321 -> 200
	set_tunnel(_tunnel, image_decode, 321, video_encode, 200);

	if (write_media_file) {

		// create write_media
		if ((r_il = ilclient_create_component(_client, &write_media, "write_media", ILCLIENT_DISABLE_ALL_PORTS
#ifndef TUNNEL
			| ILCLIENT_ENABLE_INPUT_BUFFERS
#endif /* TUNNEL */
			)) != 0)
			ILC_ERR_EXIT("%s:%d: ilclient_create_component() for write_media failed (%d)!\n")
		_comp[2] = write_media;
		_port[2][0][0] = 170; // audio
		_port[2][0][1] = 171; // video

		// create tunnel 201 -> 171
		set_tunnel(_tunnel + 1, video_encode, 201, write_media, 171);
	}

	// set set_port_settings_callback
	ilclient_set_port_settings_callback(_client, capture_encode_jpeg_port_settings_callback, NULL);
	// set fill_buffer_done_callback
	ilclient_set_fill_buffer_done_callback(_client, capture_encode_jpeg_fill_buffer_done_callback, NULL);
	// set error_callback
	ilclient_set_error_callback(_client, capture_encode_jpeg_error_callback, NULL);

	// move components to idle
	ilclient_state_transition(_comp, OMX_StateIdle);

	unsigned int bufsize = init_device();
	fprintf(stderr, "capture buffer size: %d\n", bufsize);

	int inputbuffernumber = image_decode_init(image_decode, bufsize);

	_inputbufferlist = NULL;
	int capturing_initialized = 0;

	do {
		struct timespec capture_time;

		//DEBUG_PRINT("1. move image_decode to executing\n")
		//ilclient_change_component_state(image_decode, OMX_StateExecuting);

		DEBUG_PRINT("2. get 320 in buffer from image_decode queue\n")
		//if ((buf = ilclient_get_input_buffer(image_decode, 320, 0)) != NULL) {
		//	vc_assert(buf->nAllocLen >= bufsize);
		//
		if (get_input_buffers(image_decode, 320, 0, inputbuffernumber, &_inputbufferlist) == inputbuffernumber) {

			if (_framenumber < frames) {

				if (!capturing_initialized) {
					DEBUG_PRINT("initialize buffers and start capturing\n")
					init_buffers(bufsize, _inputbufferlist);
					start_capturing();
					capturing_initialized = 1;
				}

				/* fill it */
				//generate_test_card(buf->pBuffer, &buf->nFilledLen, framenumber++);
				buf = capture_frame(_inputbufferlist);

				/* take a buffer out of inputbufferlist */
				buffer_list_get_buf_remove(&_inputbufferlist, buf);

				buf->nFlags = OMX_BUFFERFLAG_EOS;
				_framenumber++;
				clock_gettime(CLOCK_MONOTONIC, &capture_time);
				INFO_PRINT_2("captured frame %d (%d bytes)\n", _framenumber, buf->nFilledLen)

				DEBUG_PRINT("3. send filled 320 in buffer to image_decode processor\n")
				if ((r = OMX_EmptyThisBuffer(ILC_GET_HANDLE(image_decode), buf)) != OMX_ErrorNone)
					fprintf(stderr, "Error emptying buffer: %x\n", r);
			}
		}

		if (_framenumber >= frames) {
			get_time_diff(&capture_time, &diff);
			wait_timeout(0, 1000);
			fprintf(stderr, "\r%.2f ", (1000000000.0 * diff.tv_sec + diff.tv_nsec) / 1000000000.0);
		}

		// check video_encode component is in the right state to accept buffers
		DEBUG_PRINT("check video_encode component is in the right state to accept buffers\n")
		wait_for_StateExecuting(video_encode);

		wait_tunnel_buffer(_tunnel, &_copybuffernumber);

		if (write_media_file) {

			// check write_media component is in the right state to accept buffers
			DEBUG_PRINT("check write_media component is in the right state to accept buffers\n")
			wait_for_StateExecuting(write_media);

			wait_tunnel_buffer(_tunnel + 1, NULL);
		}
		else {

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
					_outframenumber++;
					INFO_PRINT_2("output frame %d (%d bytes)\n", _outframenumber, out->nFilledLen)
				}
				fflush(stdout);
				out->nFilledLen = 0;

				DEBUG_PRINT("13. send emptied 201 out buffer to video_encode processor\n")
				if ((r = OMX_FillThisBuffer(ILC_GET_HANDLE(video_encode), out)) != OMX_ErrorNone)
					fprintf(stderr, "Error filling 201 out buffer: %x\n", r);
			}
		}
	}
	while (_framenumber < frames || /*out != NULL ||*/ diff.tv_sec < 1);

	capture_encode_jpeg_teardown();

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
