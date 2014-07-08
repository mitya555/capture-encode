/*
 *  V4L2 video capture example
 *
 *  This program can be used and distributed without restrictions.
 *
 *      This program is provided with the V4L2 API
 * see http://linuxtv.org/docs.php for more information
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>

#include <time.h>

#include "bcm_host.h"
#include "ilclient.h"

/*
* MJPEG/AVI1 to JPEG/JFIF bitstream format filter
* Copyright (c) 2010 Adrian Daerr and Nicolas George
*
* This file is part of Libav.
*
* Libav is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* Libav is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with Libav; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

/*
* Adapted from mjpeg2jpeg.c, with original copyright:
* Paris 2010 Adrian Daerr, public domain
*/

//#include <string.h>
//#include "avcodec.h"
//#include "mjpeg.h"

/* Set up the standard Huffman tables (cf. JPEG standard section K.3) */
/* IMPORTANT: these are only valid for 8-bit data precision! */
const uint8_t ff_mjpeg_bits_dc_luminance[17] =
{ /* 0-base */ 0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
const uint8_t ff_mjpeg_val_dc[12] =
{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

const uint8_t ff_mjpeg_bits_dc_chrominance[17] =
{ /* 0-base */ 0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };

const uint8_t ff_mjpeg_bits_ac_luminance[17] =
{ /* 0-base */ 0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
const uint8_t ff_mjpeg_val_ac_luminance[] =
{ 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
  0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
  0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
  0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
  0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
  0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
  0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
  0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
  0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
  0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
  0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
  0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
  0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
  0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
  0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
  0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
  0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
  0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
  0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
  0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
  0xf9, 0xfa
};

const uint8_t ff_mjpeg_bits_ac_chrominance[17] =
{ /* 0-base */ 0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };

const uint8_t ff_mjpeg_val_ac_chrominance[] =
{ 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
  0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
  0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
  0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
  0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
  0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
  0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
  0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
  0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
  0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
  0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
  0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
  0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
  0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
  0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
  0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
  0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
  0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
  0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
  0xf9, 0xfa
};

static const uint8_t jpeg_header[] = {
	0xff, 0xd8,                     // SOI
	0xff, 0xe0,                     // APP0
	0x00, 0x10,                     // APP0 header size (including
	// this field, but excluding preceding)
	0x4a, 0x46, 0x49, 0x46, 0x00,   // ID string 'JFIF\0'
	0x01, 0x01,                     // version
	0x00,                           // bits per type
	0x00, 0x00,                     // X density
	0x00, 0x00,                     // Y density
	0x00,                           // X thumbnail size
	0x00,                           // Y thumbnail size
};

static const int dht_segment_size = 420;
static const uint8_t dht_segment_head[] = { 0xFF, 0xC4, 0x01, 0xA2, 0x00 };
static const uint8_t dht_segment_frag[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
	0x0a, 0x0b, 0x01, 0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static uint8_t *append(uint8_t *buf, const uint8_t *src, int size)
{
	memcpy(buf, src, size);
	return buf + size;
}

static uint8_t *append_dht_segment(uint8_t *buf)
{
	buf = append(buf, dht_segment_head, sizeof(dht_segment_head));
	buf = append(buf, ff_mjpeg_bits_dc_luminance + 1, 16);
	buf = append(buf, dht_segment_frag, sizeof(dht_segment_frag));
	buf = append(buf, ff_mjpeg_val_dc, 12);
	*(buf++) = 0x10;
	buf = append(buf, ff_mjpeg_bits_ac_luminance + 1, 16);
	buf = append(buf, ff_mjpeg_val_ac_luminance, 162);
	*(buf++) = 0x11;
	buf = append(buf, ff_mjpeg_bits_ac_chrominance + 1, 16);
	buf = append(buf, ff_mjpeg_val_ac_chrominance, 162);
	return buf;
}

static int mjpeg2jpeg_filter(/*AVBitStreamFilterContext *bsfc,
	AVCodecContext *avctx, const char *args,
	uint8_t **poutbuf, int *poutbuf_size,
	const*/ uint8_t *buf, int buf_size/*,
	int keyframe*/)
{
	int input_skip, output_size;
	//uint8_t *output, *out;

	if (buf_size < 12) {
		/*av_log(avctx, AV_LOG_ERROR, */fprintf(stderr, "input is truncated\n");
		return -1; // AVERROR_INVALIDDATA;
	}
	if (buf && memcmp("AVI1", buf + 6, 4)) {
		/*av_log(avctx, AV_LOG_ERROR, */fprintf(stderr, "input is not MJPEG/AVI1\n");
		return -2; // AVERROR_INVALIDDATA;
	}
	input_skip = buf ? (buf[4] << 8) + buf[5] + 4 : 4;
	if (buf_size < input_skip) {
		/*av_log(avctx, AV_LOG_ERROR, */fprintf(stderr, "input is truncated\n");
		return -3; // AVERROR_INVALIDDATA;
	}
	output_size = buf_size - input_skip + sizeof(jpeg_header) + dht_segment_size;
	/*output = out = av_malloc(output_size);
	if (!output)
		return AVERROR(ENOMEM);
	out = append(out, jpeg_header, sizeof(jpeg_header));
	out = append_dht_segment(out);
	out = append(out, buf + input_skip, buf_size - input_skip);
	*poutbuf = output;
	*poutbuf_size = output_size;
	return 1;*/
	if (buf) {
		memmove(buf + sizeof(jpeg_header) + dht_segment_size, buf + input_skip, buf_size - input_skip);
		buf = append(buf, jpeg_header, sizeof(jpeg_header));
		append_dht_segment(buf);
	}
	return output_size;
}

//AVBitStreamFilter ff_mjpeg2jpeg_bsf = {
//	.name = "mjpeg2jpeg",
//	.filter = mjpeg2jpeg_filter,
//};

#define CLEAR(x) memset(&(x), 0, sizeof(x))

enum io_method {
	IO_METHOD_READ,
	IO_METHOD_MMAP,
	IO_METHOD_USERPTR,
};

struct buffer {
	void   *start;
	size_t  length;
};

static char            *dev_name = "/dev/video0", 
                       *test_encode_filename = "test.h264";
static enum io_method   io = IO_METHOD_MMAP;
static int              fd = -1;
static struct buffer   *buffers;
static unsigned int     n_buffers;
static int              output, force_format, fps, fps_cur, fps_avg, encode, tst_enc, m2jpeg = 1;
static struct timespec  start, end;
static double           fps_total;
static int              fps_count;
static OMX_COLOR_FORMATTYPE img_fmt = OMX_COLOR_FormatYUV420PackedPlanar;
static int              img_width = 640, img_height = 480;
static int              frame_count = 100;
static struct v4l2_format v4l2_fmt;

void time_diff(struct timespec *start, struct timespec *end, struct timespec *result)
{
	if (end->tv_nsec < start->tv_nsec) {
		result->tv_sec = end->tv_sec - start->tv_sec - 1;
		result->tv_nsec = 1000000000 - start->tv_nsec + end->tv_nsec;
	}
	else {
		result->tv_sec = end->tv_sec - start->tv_sec;
		result->tv_nsec = end->tv_nsec - start->tv_nsec;
	}
}

static void errno_exit(const char *s)
{
	fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
	exit(EXIT_FAILURE);
}

static int xioctl(int fh, unsigned long int request, void *arg)
{
	int r;

	do
		r = ioctl(fh, request, arg);
	while (-1 == r && EINTR == errno);

	return r;
}

static void process_image(const void *p, int size)
{
	if (output) {
		fwrite(p, size, 1, stdout);
		fflush(stdout);
	}
	if (fps) {
		clock_gettime(CLOCK_MONOTONIC, &end);
		if (start.tv_sec + start.tv_nsec > 0) {
			struct timespec diff;
			time_diff(&start, &end, &diff);
			//fprintf(stderr, "%ld.%09ld ", diff.tv_sec, diff.tv_nsec);
			double fps_current  = 1000000000.0 / (1000000000.0 * diff.tv_sec + diff.tv_nsec);
			if (fps_cur) {
				fprintf(stderr, "\r%.2f ", fps_current);
				fflush(stderr);
			}
			if (fps_avg) {
				fps_total += fps_current;
				fps_count++;
			}
		}
		start = end;
	}
}

#define SWITCH_ERRNO(str) switch (errno) { \
			case EAGAIN: \
				return 0; \
			case EIO: \
				/* Could ignore EIO, see spec. */ \
				/* fall through */ \
			default: \
				errno_exit(str); \
			}

static OMX_BUFFERHEADERTYPE *read_frame(OMX_BUFFERHEADERTYPE *buf_list)
{
	struct v4l2_buffer buf;
	unsigned int i;
	void *out_buf;

	switch (io) {
	case IO_METHOD_READ:
		out_buf = buf_list == NULL ? buffers[0].start : buf_list->pBuffer;
		int size = read(fd, out_buf, buffers[0].length);
		if (-1 == size)
			SWITCH_ERRNO("read")
		if (v4l2_fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG && m2jpeg)
			size = mjpeg2jpeg_filter(out_buf, size);
		process_image(out_buf, size);
		if (buf_list != NULL)
			buf_list->nFilledLen = size;
		return buf_list;

	case IO_METHOD_MMAP:
		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;

		if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
			SWITCH_ERRNO("VIDIOC_DQBUF")

		assert(buf.index < n_buffers);

		process_image(buffers[buf.index].start, buf.bytesused);

		if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
			errno_exit("VIDIOC_QBUF");
		break;

	case IO_METHOD_USERPTR:
		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_USERPTR;

		if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
			SWITCH_ERRNO("VIDIOC_DQBUF")

		for (i = 0; i < n_buffers; ++i)
			if (buf.m.userptr == (unsigned long)buffers[i].start && 
				buf.length == buffers[i].length)
				break;

		assert(i < n_buffers);

		if (v4l2_fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG && m2jpeg)
			buf.bytesused = mjpeg2jpeg_filter((void *)buf.m.userptr, buf.bytesused);

		uint bytesused = buf.bytesused;

		process_image((void *)buf.m.userptr, buf.bytesused);

		if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
			errno_exit("VIDIOC_QBUF");
		
		if (buf_list != NULL) {
			while (buf_list != NULL) {
				if (buf_list->pBuffer == buffers[i].start &&
					buf_list->nAllocLen >= buffers[i].length) {
					buf_list->nFilledLen = bytesused;
					return buf_list;
				}
				buf_list = buf_list->pAppPrivate;
			}
			assert(buf_list != NULL);
		}
	}

	return NULL;
}

OMX_BUFFERHEADERTYPE *capture_frame(OMX_BUFFERHEADERTYPE *buf_list)
{
	for (;;) {
		fd_set fds;
		struct timeval tv;

		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		/* Timeout. */
		tv.tv_sec = 5;
		tv.tv_usec = 0;

		switch (select(fd + 1, &fds, NULL, NULL, &tv)) {
		case -1:
			if (EINTR == errno)
				continue;
			errno_exit("select");
		case 0:
			fprintf(stderr, "select timeout\n");
			exit(EXIT_FAILURE);
		}

		return read_frame(buf_list);

		/* EAGAIN - continue select loop. */
	}
}

static inline void report_fps_avg()
{
	fprintf(stderr, "%sAverage frame rate: %.2f fps\n", fps_cur ? "\n" : "", fps_total / fps_count);
	fflush(stderr);
}

static void mainloop(void)
{
	unsigned int count = frame_count;

	while (count-- > 0)
		capture_frame(NULL);

	if (fps_avg)
		report_fps_avg();
}

static void stop_capturing(void)
{
	enum v4l2_buf_type type;

	switch (io) {
	case IO_METHOD_READ:
		/* Nothing to do. */
		break;

	case IO_METHOD_MMAP:
	case IO_METHOD_USERPTR:
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
			errno_exit("VIDIOC_STREAMOFF");
		break;
	}
}

void start_capturing(void)
{
	unsigned int i;
	enum v4l2_buf_type type;

	switch (io) {
	case IO_METHOD_READ:
		/* Nothing to do. */
		break;

	case IO_METHOD_MMAP:
		for (i = 0; i < n_buffers; ++i) {
			struct v4l2_buffer buf;

			CLEAR(buf);
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;
			buf.index = i;

			if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
				errno_exit("VIDIOC_QBUF");
		}
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
			errno_exit("VIDIOC_STREAMON");
		break;

	case IO_METHOD_USERPTR:
		for (i = 0; i < n_buffers; ++i) {
			struct v4l2_buffer buf;

			CLEAR(buf);
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_USERPTR;
			buf.index = i;
			buf.m.userptr = (unsigned long)buffers[i].start;
			buf.length = buffers[i].length;

			if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
				errno_exit("VIDIOC_QBUF");
		}
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
			errno_exit("VIDIOC_STREAMON");
		break;
	}
}

static void uninit_device(int external_buffers)
{
	unsigned int i;

	switch (io) {
	case IO_METHOD_READ:
		if (buffers[0].start)
			free(buffers[0].start);
		break;

	case IO_METHOD_MMAP:
		for (i = 0; i < n_buffers; ++i)
			if (-1 == munmap(buffers[i].start, buffers[i].length))
				errno_exit("munmap");
		break;

	case IO_METHOD_USERPTR:
		if (!external_buffers)
			for (i = 0; i < n_buffers; ++i)
				free(buffers[i].start);
		break;
	}

	free(buffers);
}

static void init_read(unsigned int buffer_size, int external_buffers)
{
	buffers = calloc(1, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	buffers[0].length = buffer_size;
	if (!external_buffers) {
		buffers[0].start = malloc(buffer_size);

		if (!buffers[0].start) {
			fprintf(stderr, "Out of memory\n");
			exit(EXIT_FAILURE);
		}
	}
}

static void init_mmap(void)
{
	struct v4l2_requestbuffers req;

	CLEAR(req);
	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s does not support "
				 "memory mapping\n", dev_name);
			exit(EXIT_FAILURE);
		} else
			errno_exit("VIDIOC_REQBUFS");
	}

	if (req.count < 2) {
		fprintf(stderr, "Insufficient buffer memory on %s\n", dev_name);
		exit(EXIT_FAILURE);
	}

	buffers = calloc(req.count, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		struct v4l2_buffer buf;

		CLEAR(buf);
		buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory      = V4L2_MEMORY_MMAP;
		buf.index       = n_buffers;

		if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
			errno_exit("VIDIOC_QUERYBUF");

		buffers[n_buffers].length = buf.length;
		buffers[n_buffers].start = mmap(NULL /* start anywhere */,
			buf.length,
			PROT_READ | PROT_WRITE /* required */,
			MAP_SHARED /* recommended */,
			fd, buf.m.offset);

		if (MAP_FAILED == buffers[n_buffers].start)
			errno_exit("mmap");
	}
}

static uint buffer_count(OMX_BUFFERHEADERTYPE *external_buffers)
{
	if (!external_buffers)
		return 4;
	uint cnt = 0;
	do {
		cnt++;
	} while ((external_buffers = external_buffers->pAppPrivate) != NULL);
	return cnt;
}

static void init_userp(unsigned int buffer_size, OMX_BUFFERHEADERTYPE *external_buffers)
{
	struct v4l2_requestbuffers req;

	uint buffers_count = buffer_count(external_buffers);

	CLEAR(req);
	req.count = buffers_count; // 4;
	req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_USERPTR;

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s does not support user pointer i/o\n", dev_name);
			exit(EXIT_FAILURE);
		} else
			errno_exit("VIDIOC_REQBUFS");
	}

	buffers = calloc(buffers_count, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	for (n_buffers = 0; n_buffers < /*4*/buffers_count; ++n_buffers) {
		buffers[n_buffers].length = buffer_size;
		if (external_buffers) {
			buffers[n_buffers].start = external_buffers->pBuffer;
			external_buffers = external_buffers->pAppPrivate;
		}
		else
			buffers[n_buffers].start = malloc(buffer_size);

		if (!buffers[n_buffers].start) {
			fprintf(stderr, "Out of memory\n");
			exit(EXIT_FAILURE);
		}
	}
}

unsigned int init_device(void)
{
	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;

	if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s is no V4L2 device\n", dev_name);
			exit(EXIT_FAILURE);
		} else
			errno_exit("VIDIOC_QUERYCAP");
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf(stderr, "%s is no video capture device\n", dev_name);
		exit(EXIT_FAILURE);
	}

	switch (io) {
	case IO_METHOD_READ:
		if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
			fprintf(stderr, "%s does not support read i/o\n", dev_name);
			exit(EXIT_FAILURE);
		}
		break;

	case IO_METHOD_MMAP:
	case IO_METHOD_USERPTR:
		if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
			fprintf(stderr, "%s does not support streaming i/o\n", dev_name);
			exit(EXIT_FAILURE);
		}
		break;
	}

	/* Select video input, video standard and tune here. */

	CLEAR(cropcap);
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c = cropcap.defrect; /* reset to default */

		if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop))
			switch (errno) {
			case EINVAL:
				/* Cropping not supported. */
				break;
			default:
				/* Errors ignored. */
				break;
			}
	} else {
		/* Errors ignored. */
	}

	CLEAR(v4l2_fmt);
	v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (force_format) {
		v4l2_fmt.fmt.pix.width       = 640;
		v4l2_fmt.fmt.pix.height      = 480;
		v4l2_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
		v4l2_fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;

		if (-1 == xioctl(fd, VIDIOC_S_FMT, &v4l2_fmt))
			errno_exit("VIDIOC_S_FMT");

		/* Note VIDIOC_S_FMT may change width and height. */
	} else {
		/* Preserve original settings as set by v4l2-ctl for example */
		if (-1 == xioctl(fd, VIDIOC_G_FMT, &v4l2_fmt))
			errno_exit("VIDIOC_G_FMT");
	}

	return v4l2_fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG && m2jpeg ?
		mjpeg2jpeg_filter(NULL, v4l2_fmt.fmt.pix.sizeimage) : v4l2_fmt.fmt.pix.sizeimage;
}

void init_buffers(unsigned int buffer_size, OMX_BUFFERHEADERTYPE *external_buffers)
{
	switch (io) {
	case IO_METHOD_READ:
		fprintf(stderr, "capture method: IO_METHOD_READ\n");
		init_read(buffer_size, external_buffers != NULL);
		break;

	case IO_METHOD_MMAP:
		fprintf(stderr, "capture method: IO_METHOD_MMAP\n");
		init_mmap();
		break;

	case IO_METHOD_USERPTR:
		fprintf(stderr, "capture method: IO_METHOD_USERPTR\n");
		init_userp(buffer_size, external_buffers);
		break;
	}
}

static void close_device(void)
{
	if (-1 == close(fd))
		errno_exit("close");

	fd = -1;
}

static void open_device(void)
{
	struct stat st;

	if (-1 == stat(dev_name, &st)) {
		fprintf(stderr, "Cannot identify '%s': %d, %s\n", dev_name, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (!S_ISCHR(st.st_mode)) {
		fprintf(stderr, "%s is no device\n", dev_name);
		exit(EXIT_FAILURE);
	}

	fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

	if (-1 == fd) {
		fprintf(stderr, "Cannot open '%s': %d, %s\n", dev_name, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

static void usage(FILE *fp, int argc, char **argv)
{
	fprintf(fp,
		 "Usage: %s [options]\n\n"
		 "Version 1.3\n"
		 "Options:\n"
		 "-d | --device name        Video device name [%s]\n"
		 "-h | --help               Print this message\n"
		 "-m | --mmap               Use memory mapped buffers [default]\n"
		 "-r | --read               Use read() calls\n"
		 "-u | --userp              Use application allocated buffers\n"
		 "-o | --output             Outputs stream to stdout\n"
		 "-f | --format             Force camera format to 640x480 YUYV\n"
		 "-c | --count              Number of frames to grab [%i]\n"
		 "-p | --fps_cur            Print current FPS (Frames Per Second)\n"
		 "-a | --fps_avg            Print average FPS (Frames Per Second)\n"
		 "-t | --tst_enc filename   Tests encoding to H.264 to filename [%s]\n"
		 "-n | --encode             Encodes to H.264 to stdout (uses --read or --userp[default] only (no --mmap) and disables --output)\n"
		 "-i | --img_fmt            Input image format for encoding [%i]\n"
		 "-x | --img_width          Input image width for encoding [%i]\n"
		 "-y | --img_height         Input image height for encoding [%i]\n"
		 "-z | --no_m2jpeg          No MJPEG to JPEG conversion\n"
		 "",
		 argv[0], dev_name, frame_count, test_encode_filename, img_fmt, img_width, img_height);
}

static const char short_options[] = "d:hmruofc:pat:ni:x:y:z";

static const struct option
long_options[] = {
	{ "device",    required_argument, NULL, 'd' },
	{ "help",      no_argument,       NULL, 'h' },
	{ "mmap",      no_argument,       NULL, 'm' },
	{ "read",      no_argument,       NULL, 'r' },
	{ "userp",     no_argument,       NULL, 'u' },
	{ "output",    no_argument,       NULL, 'o' },
	{ "format",    no_argument,       NULL, 'f' },
	{ "count",     required_argument, NULL, 'c' },
	{ "fps_cur",   no_argument,       NULL, 'p' },
	{ "fps_avg",   no_argument,       NULL, 'a' },
	{ "tst_enc",   optional_argument, NULL, 't' },
	{ "encode",    no_argument,       NULL, 'n' },
	{ "img_fmt",   required_argument, NULL, 'i' },
	{ "img_width", required_argument, NULL, 'x' },
	{ "img_height",required_argument, NULL, 'y' },
	{ "no_m2jpeg", no_argument,       NULL, 'z' },
	{ 0, 0, 0, 0 }
};

extern int
video_encode_test(char *outputfilename);

extern int
capture_encode_loop(int frames, OMX_U32 frameWidth, OMX_U32 frameHeight, uint frameRate, OMX_COLOR_FORMATTYPE colorFormat, unsigned int bufsize);

extern int
capture_encode_jpeg_loop(int frames/*, OMX_U32 frameWidth, OMX_U32 frameHeight, uint frameRate, OMX_COLOR_FORMATTYPE colorFormat, unsigned int bufsize*/);

int main(int argc, char **argv)
{
	for (;;) {
		int idx;
		int c;

		if (-1 == (c = getopt_long(argc, argv, short_options, long_options, &idx)))
			break;

		switch (c) {
		case 0: /* getopt_long() flag */
			break;

		case 'd':
			dev_name = optarg;
			break;

		case 'h':
			usage(stdout, argc, argv);
			exit(EXIT_SUCCESS);

		case 'm':
			io = IO_METHOD_MMAP;
			break;

		case 'r':
			io = IO_METHOD_READ;
			break;

		case 'u':
			io = IO_METHOD_USERPTR;
			break;

		case 'o':
			output++;
			break;

		case 'f':
			force_format++;
			break;

		case 'c':
			errno = 0;
			frame_count = strtol(optarg, NULL, 0);
			if (errno)
				errno_exit(optarg);
			break;

		case 'p':
			fps_cur++;
			fps++;
			break;

		case 'a':
			fps_avg++;
			fps++;
			break;

		case 't':
			tst_enc++;
			if (optarg)
				test_encode_filename = optarg;
			break;

		case 'n':
			encode++;
			break;

		case 'i':
			errno = 0;
			img_fmt = strtol(optarg, NULL, 0);
			if (errno)
				errno_exit(optarg);
			break;

		case 'x':
			errno = 0;
			img_width = strtol(optarg, NULL, 0);
			if (errno)
				errno_exit(optarg);
			break;

		case 'y':
			errno = 0;
			img_height = strtol(optarg, NULL, 0);
			if (errno)
				errno_exit(optarg);
			break;

		case 'z':
			m2jpeg = 0;
			break;

		default:
			usage(stderr, argc, argv);
			exit(EXIT_FAILURE);
		}
	}

	if (encode) {
		if (io == IO_METHOD_MMAP)
			io = IO_METHOD_USERPTR; // default for --encode
		output = 0;
	}

	if (tst_enc) {
		bcm_host_init();
		int res = video_encode_test(test_encode_filename);
		bcm_host_deinit();
		return res;
	}

	open_device();
	if (encode) {
		bcm_host_init();
		capture_encode_jpeg_loop(frame_count/*, img_width, img_height, 14, img_fmt, bufsize*/); // OMX_COLOR_FormatYUV420PackedPlanar); // 10, OMX_COLOR_FormatYUV422PackedPlanar);
		bcm_host_deinit();
		if (fps_avg)
			report_fps_avg();
	}
	else {
		unsigned int bufsize = init_device();
		fprintf(stderr, "capture buffer size: %d\n", bufsize);
		init_buffers(bufsize, NULL);
		start_capturing();
		mainloop();
	}
	stop_capturing();
	uninit_device(encode);
	close_device();
	fprintf(stderr, "\n");

	return 0;
}
