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
static int              output;
static int              force_format;
static int              fps, fps_cur, fps_avg;
static struct timespec  start, end;
static double           fps_total;
static int              fps_count;
static int              encode, tst_enc;
static OMX_COLOR_FORMATTYPE img_fmt = OMX_COLOR_FormatYUV420PackedPlanar;
static int              img_width = 640, img_height = 480;
static int              frame_count = 100;

static void time_diff(struct timespec *start, struct timespec *end, struct timespec *result)
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

static int read_frame(void *out_buf, OMX_U32 *out_size)
{
	struct v4l2_buffer buf;
	unsigned int i;

	switch (io) {
	case IO_METHOD_READ:
		if (NULL == out_buf)
			out_buf = buffers[0].start;
		int size = read(fd, out_buf, buffers[0].length);
		if (-1 == size)
			SWITCH_ERRNO("read")
		process_image(out_buf, size);
		if (out_size)
			*out_size = size;
		break;

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

		process_image((void *)buf.m.userptr, buf.bytesused);

		if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
			errno_exit("VIDIOC_QBUF");
		break;
	}

	return 1;
}

void capture_frame(void *out_buf, OMX_U32 *out_size)
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

		if (read_frame(out_buf, out_size))
			break;
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
		capture_frame(NULL, NULL);

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

static void start_capturing(void)
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

static void uninit_device(void)
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
		for (i = 0; i < n_buffers; ++i)
			free(buffers[i].start);
		break;
	}

	free(buffers);
}

static void init_read(unsigned int buffer_size, int external_read_buffer)
{
	buffers = calloc(1, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	buffers[0].length = buffer_size;
	if (!external_read_buffer) {
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

static void init_userp(unsigned int buffer_size)
{
	struct v4l2_requestbuffers req;

	CLEAR(req);
	req.count  = 4;
	req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_USERPTR;

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s does not support user pointer i/o\n", dev_name);
			exit(EXIT_FAILURE);
		} else
			errno_exit("VIDIOC_REQBUFS");
	}

	buffers = calloc(4, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	for (n_buffers = 0; n_buffers < 4; ++n_buffers) {
		buffers[n_buffers].length = buffer_size;
		buffers[n_buffers].start = malloc(buffer_size);

		if (!buffers[n_buffers].start) {
			fprintf(stderr, "Out of memory\n");
			exit(EXIT_FAILURE);
		}
	}
}

static unsigned int init_device(int external_read_buffer)
{
	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_format fmt;

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

	CLEAR(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (force_format) {
		fmt.fmt.pix.width       = 640;
		fmt.fmt.pix.height      = 480;
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
		fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;

		if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
			errno_exit("VIDIOC_S_FMT");

		/* Note VIDIOC_S_FMT may change width and height. */
	} else {
		/* Preserve original settings as set by v4l2-ctl for example */
		if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt))
			errno_exit("VIDIOC_G_FMT");
	}

	switch (io) {
	case IO_METHOD_READ:
		init_read(fmt.fmt.pix.sizeimage, external_read_buffer);
		break;

	case IO_METHOD_MMAP:
		init_mmap();
		break;

	case IO_METHOD_USERPTR:
		init_userp(fmt.fmt.pix.sizeimage);
		break;
	}

	return fmt.fmt.pix.sizeimage;
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
		 "-n | --encode             Encodes to H.264 to stdout (enforces --read and disables --output)\n"
		 "-i | --img_fmt            Input image format for encoding [%i]\n"
		 "-x | --img_width          Input image width for encoding [%i]\n"
		 "-y | --img_height         Input image height for encoding [%i]\n"
		 "",
		 argv[0], dev_name, frame_count, test_encode_filename, img_fmt, img_width, img_height);
}

static const char short_options[] = "d:hmruofc:pat:ni:x:y:";

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
	{ 0, 0, 0, 0 }
};

extern int
video_encode_test(char *outputfilename);

extern int
capture_encode_loop(int frames, OMX_U32 frameWidth, OMX_U32 frameHeight, uint frameRate, OMX_COLOR_FORMATTYPE colorFormat, unsigned int bufsize);

extern int
capture_encode_jpeg_loop(int frames, OMX_U32 frameWidth, OMX_U32 frameHeight, uint frameRate, OMX_COLOR_FORMATTYPE colorFormat, unsigned int bufsize);

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

		default:
			usage(stderr, argc, argv);
			exit(EXIT_FAILURE);
		}
	}

	if (encode) {
		io = IO_METHOD_READ;
		output = 0;
	}

	if (tst_enc) {
		bcm_host_init();
		int res = video_encode_test(test_encode_filename);
		bcm_host_deinit();
		return res;
	}

	open_device();
	unsigned int bufsize = init_device(encode);
	fprintf(stderr, "capture buffer size: %d\n", bufsize);
	start_capturing();
	if (encode) {
		bcm_host_init();
		capture_encode_jpeg_loop(frame_count, img_width, img_height, 30, img_fmt, bufsize); // OMX_COLOR_FormatYUV420PackedPlanar); // 10, OMX_COLOR_FormatYUV422PackedPlanar);
		bcm_host_deinit();
		if (fps_avg)
			report_fps_avg();
	}
	else
		mainloop();
	stop_capturing();
	uninit_device();
	close_device();
	fprintf(stderr, "\n");

	return 0;
}
