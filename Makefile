# rt-tests for QNX 8.0: builds the upstream sources (upstream/, see scripts/fetch-upstream.sh)
# against the shim layer in qnx/ with qcc.
#
#   make                      # aarch64le -> build/aarch64le/
#   make ARCH=x86_64          # x86_64    -> build/x86_64/
#   make STAGE=<dir> install
#
# Needs the QNX SDP 8.0 environment (source qnxsdp-env.sh) so that qcc is on PATH.
# SPDX-License-Identifier: GPL-2.0-or-later

ARCH     ?= aarch64le
QCC_VAR  = $(if $(filter $(ARCH),aarch64le),gcc_ntoaarch64le,gcc_ntox86_64)
CC       = qcc -V$(QCC_VAR)
AR       = $(if $(filter $(ARCH),aarch64le),ntoaarch64-ar,ntox86_64-ar)
VERSION  = 2.11
UP       = upstream
OUT      = build/$(ARCH)
STAGE   ?= stage/$(ARCH)

CPPFLAGS = -D_GNU_SOURCE -DVERSION=$(VERSION) -Iqnx/include -I$(UP)/src/include -include qnx-compat.h
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter
LIBS     = -lm

UPTOOLS  = cyclictest signaltest ptsematest sigwaittest pmqtest \
           pi_stress pip_stress hackbench oslat rt-migrate-test
NATIVE   = msglat
TOOLS    = $(UPTOOLS) $(NATIVE)

VPATH = $(UP)/src/cyclictest:$(UP)/src/signaltest:$(UP)/src/pi_tests:$(UP)/src/rt-migrate-test:\
        $(UP)/src/ptsematest:$(UP)/src/sigwaittest:$(UP)/src/svsematest:$(UP)/src/pmqtest:\
        $(UP)/src/lib:$(UP)/src/hackbench:$(UP)/src/oslat:qnx/src:qnx/tools

.PHONY: all clean install tools
all: $(addprefix $(OUT)/,$(TOOLS))

$(OUT):
	mkdir -p $@

$(OUT)/%.o: %.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

LIBRT = $(addprefix $(OUT)/,rt-error.o rt-get_cpu.o rt-sched.o rt-utils.o histogram.o rt-numa.o)
SHIM  = $(addprefix $(OUT)/,qnx-compat.o qnx-numa.o)

$(OUT)/librttest.a: $(LIBRT) $(SHIM)
	rm -f $@ && $(AR) rcs $@ $^

$(OUT)/hackbench: $(OUT)/hackbench.o $(OUT)/librttest.a
	$(CC) $(CFLAGS) -o $@ $< $(OUT)/librttest.a $(LIBS) -lsocket

$(addprefix $(OUT)/,$(filter-out hackbench,$(TOOLS))): $(OUT)/%: $(OUT)/%.o $(OUT)/librttest.a
	$(CC) $(CFLAGS) -o $@ $< $(OUT)/librttest.a $(LIBS)

install: all
	mkdir -p $(STAGE)
	cp $(addprefix $(OUT)/,$(TOOLS)) $(STAGE)/

clean:
	rm -rf build stage
