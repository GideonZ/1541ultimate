# Loopback test of the MicroHttpServer response path (software/httpd/c-version/lib):
# a client that goes away before the reply is fully sent. Built with AddressSanitizer
# and leak detection.
#
#   make test            build and run all cases
#   make test CASE=body  run one case (body, stale, static) of the socket test
#   (HTTPD=<dir> names the httpd checkout if software/httpd is not populated)
#
# env -u LD_PRELOAD: the CI image runs with a library preloaded, and an ASan binary then
# refuses to start ("ASan runtime does not come first"); the other ASan tests do the same.
PATH_SW  = ../../../../software
HTTPD   ?= $(PATH_SW)/httpd
LIB      = $(HTTPD)/c-version/lib
FOLDER   = /tmp/httpd_body_static
CC       = gcc
SWDIRS   = application application/ultimate filesystem filemanager filetypes system infra components userinterface \
           io/stream network api io/c64 io/usb drive io/flash io/network
CXXFLAGS = -std=gnu++14 -g -O0 -w -fpermissive -fsanitize=address -fno-omit-frame-pointer -DRUNS_ON_PC -DENABLE_STATIC_FILE=2 \
           -DMHS_PORT=8001 $(addprefix -I$(PATH_SW)/,$(SWDIRS)) -I$(LIB)
CXXSRCS  = $(PATH_SW)/test/httpd_body/routes_body_test.cc $(addprefix $(PATH_SW)/,api/json.cc components/mystring.cc components/pattern.cc system/small_printf.cc)
CFLAGS   = -std=gnu99 -g -O0 -w -fsanitize=address -fno-omit-frame-pointer -DRUNS_ON_PC -DENABLE_STATIC_FILE=1 \
           -DMHS_PORT=18091 '-DSTATIC_FILE_FOLDER="$(FOLDER)"' -I$(LIB) -I$(PATH_SW)/test/httpd_body
SRCS     = $(PATH_SW)/test/httpd_body/httpd_body_test.c $(addprefix $(LIB)/,server.c http_protocol.c middleware.c multipart.c url.c dummy_api.c)

.PHONY: test clean
test: result/httpd_body_test result/routes_body_test
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1 ./result/httpd_body_test $(CASE)
	env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=1 ./result/routes_body_test

result/routes_body_test: $(CXXSRCS) $(PATH_SW)/api/routes.h
	@mkdir -p result
	g++ $(CXXFLAGS) -o $@ $(CXXSRCS)

result/httpd_body_test: $(SRCS) $(wildcard $(LIB)/*.h)
	@mkdir -p result
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -rf result
