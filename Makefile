#OBJS=encode.o
#BIN=v4l2_encode.bin

LDFLAGS+=-lilclient

#
#include ../Makefile.include
#

CFLAGS+=-DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -g -DHAVE_LIBOPENMAX=2 -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM -Wno-psabi

LDFLAGS+=-L$(SDKSTAGE)/opt/vc/lib/ -lGLESv2 -lEGL -lopenmaxil -lbcm_host -lvcos -lvchiq_arm -lpthread -lrt -L../libs/ilclient -L../libs/vgfont

INCLUDES+=-I$(SDKSTAGE)/opt/vc/include/ -I$(SDKSTAGE)/opt/vc/include/interface/vcos/pthreads -I$(SDKSTAGE)/opt/vc/include/interface/vmcs_host/linux -I./ -I../libs/ilclient -I../libs/vgfont

V4L_TEST_DIR+=/home/pi/v4l-utils-temp/v4l-utils/contrib/test/

OBJS+=capture-encode.o encode.o

all: capture-encode

encode.o: encode.c
	@rm -f $@ 
	$(CC) $(CFLAGS) $(INCLUDES) -g -c $< -o $@ -Wno-deprecated-declarations

capture-encode.o: capture-encode.c
	@rm -f $@ 
	#$(CC) $(CFLAGS) -I$(V4L_TEST_DIR). -I$(V4L_TEST_DIR)../.. -I$(V4L_TEST_DIR)../../include -I$(V4L_TEST_DIR)../../lib/include -g -c $< -o $@ -Wno-deprecated-declarations
	$(CC) -std=gnu99 -DHAVE_CONFIG_H -I$(V4L_TEST_DIR). -I$(V4L_TEST_DIR)../..   -I$(V4L_TEST_DIR)../../lib/include -Wall -Wpointer-arith -D_GNU_SOURCE -I$(V4L_TEST_DIR)../../include -g -O2 -MT capture-encode.o -MD -MP -MF ./.deps/capture-encode.Tpo -c -o capture-encode.o capture-encode.c &&\
	mv -f ./.deps/capture-encode.Tpo ./.deps/capture-encode.Po

capture-encode: $(OBJS)
	$(CC) -std=gnu99 -g -O2 -o $@ -Wl,--whole-archive $(OBJS) $(LDFLAGS) -Wl,--no-whole-archive -rdynamic

#%.a: $(OBJS)
#	$(AR) r $@ $^

clean:
	for i in $(OBJS); do (if test -e "$$i"; then ( rm $$i ); fi ); done
	@rm -f capture-encode


