# ffpack: lzma compression and filters from xz
# 2020, Simon Zolin

VER := 5.2.4
URL := https://tukaani.org/xz/xz-$(VER).tar.xz

include ../makeconf

all: xz-$(VER)/config.h
	make -rR liblzma-ff.$(SO)

# download
xz-$(VER).tar.xz:
	$(WGET) $(URL)

# unpack, patch
xz-$(VER): xz-$(VER).tar.xz
	$(UNTAR_XZ) xz-$(VER).tar.xz
	cd xz-$(VER)/ && patch -p1 <../0001-x86-filter.patch

# configure
CONFIGURE_ARGS :=
ifneq ($(CROSS_PREFIX),)
	CONFIGURE_ARGS += --host=x86_64-w64-mingw32
endif

xz-$(VER)/config.h: xz-$(VER)
	cd xz-$(VER) && ./configure $(CONFIGURE_ARGS) && \
		sed 's/#define HAVE_VISIBILITY 1/#define HAVE_VISIBILITY 0/' -i ./config.h

# build
SRC := \
	xz-$(VER)/src/common/tuklib_cpucores.c \
	xz-$(VER)/src/common/tuklib_physmem.c \
	\
	xz-$(VER)/src/liblzma/check/check.c \
	xz-$(VER)/src/liblzma/check/crc32_fast.c \
	xz-$(VER)/src/liblzma/check/crc32_table.c \
	xz-$(VER)/src/liblzma/check/crc64_fast.c \
	xz-$(VER)/src/liblzma/check/crc64_table.c \
	xz-$(VER)/src/liblzma/check/sha256.c \
	xz-$(VER)/src/liblzma/common/alone_decoder.c \
	xz-$(VER)/src/liblzma/common/alone_encoder.c \
	xz-$(VER)/src/liblzma/common/auto_decoder.c \
	xz-$(VER)/src/liblzma/common/block_buffer_encoder.c \
	xz-$(VER)/src/liblzma/common/block_decoder.c \
	xz-$(VER)/src/liblzma/common/block_encoder.c \
	xz-$(VER)/src/liblzma/common/block_header_encoder.c \
	xz-$(VER)/src/liblzma/common/block_util.c \
	xz-$(VER)/src/liblzma/common/common.c \
	xz-$(VER)/src/liblzma/common/easy_buffer_encoder.c \
	xz-$(VER)/src/liblzma/common/easy_decoder_memusage.c \
	xz-$(VER)/src/liblzma/common/easy_encoder.c \
	xz-$(VER)/src/liblzma/common/easy_encoder_memusage.c \
	xz-$(VER)/src/liblzma/common/easy_preset.c \
	xz-$(VER)/src/liblzma/common/filter_buffer_decoder.c \
	xz-$(VER)/src/liblzma/common/filter_buffer_encoder.c \
	xz-$(VER)/src/liblzma/common/filter_common.c \
	xz-$(VER)/src/liblzma/common/filter_decoder.c \
	xz-$(VER)/src/liblzma/common/filter_encoder.c \
	xz-$(VER)/src/liblzma/common/filter_flags_decoder.c \
	xz-$(VER)/src/liblzma/common/filter_flags_encoder.c \
	xz-$(VER)/src/liblzma/common/hardware_cputhreads.c \
	xz-$(VER)/src/liblzma/common/hardware_physmem.c \
	xz-$(VER)/src/liblzma/common/index.c \
	xz-$(VER)/src/liblzma/common/index_decoder.c \
	xz-$(VER)/src/liblzma/common/index_encoder.c \
	xz-$(VER)/src/liblzma/common/index_hash.c \
	xz-$(VER)/src/liblzma/common/outqueue.c \
	xz-$(VER)/src/liblzma/common/stream_buffer_encoder.c \
	xz-$(VER)/src/liblzma/common/stream_encoder.c \
	xz-$(VER)/src/liblzma/common/stream_flags_common.c \
	xz-$(VER)/src/liblzma/common/stream_flags_encoder.c \
	xz-$(VER)/src/liblzma/common/vli_decoder.c \
	xz-$(VER)/src/liblzma/common/vli_encoder.c \
	xz-$(VER)/src/liblzma/common/vli_size.c \
	xz-$(VER)/src/liblzma/delta/delta_common.c \
	xz-$(VER)/src/liblzma/delta/delta_decoder.c \
	xz-$(VER)/src/liblzma/delta/delta_encoder.c \
	xz-$(VER)/src/liblzma/lz/lz_decoder.c \
	xz-$(VER)/src/liblzma/lz/lz_encoder.c \
	xz-$(VER)/src/liblzma/lz/lz_encoder_mf.c \
	xz-$(VER)/src/liblzma/lzma/fastpos_table.c \
	xz-$(VER)/src/liblzma/lzma/lzma2_decoder.c \
	xz-$(VER)/src/liblzma/lzma/lzma2_encoder.c \
	xz-$(VER)/src/liblzma/lzma/lzma_decoder.c \
	xz-$(VER)/src/liblzma/lzma/lzma_encoder.c \
	xz-$(VER)/src/liblzma/lzma/lzma_encoder_optimum_fast.c \
	xz-$(VER)/src/liblzma/lzma/lzma_encoder_optimum_normal.c \
	xz-$(VER)/src/liblzma/lzma/lzma_encoder_presets.c \
	xz-$(VER)/src/liblzma/rangecoder/price_table.c \
	xz-$(VER)/src/liblzma/simple/arm.c \
	xz-$(VER)/src/liblzma/simple/armthumb.c \
	xz-$(VER)/src/liblzma/simple/ia64.c \
	xz-$(VER)/src/liblzma/simple/powerpc.c \
	xz-$(VER)/src/liblzma/simple/simple_coder.c \
	xz-$(VER)/src/liblzma/simple/simple_decoder.c \
	xz-$(VER)/src/liblzma/simple/simple_encoder.c \
	xz-$(VER)/src/liblzma/simple/sparc.c \
	xz-$(VER)/src/liblzma/simple/x86.c
OBJ := lzma-ff.o $(SRC:.c=.o)

CFLAGS := -I. -Ixz-$(VER) \
	-Ixz-$(VER)/src/liblzma/ \
	-Ixz-$(VER)/src/liblzma/api \
	-Ixz-$(VER)/src/liblzma/check \
	-Ixz-$(VER)/src/liblzma/common \
	-Ixz-$(VER)/src/liblzma/delta \
	-Ixz-$(VER)/src/liblzma/lz \
	-Ixz-$(VER)/src/liblzma/lzma \
	-Ixz-$(VER)/src/liblzma/rangecoder \
	-Ixz-$(VER)/src/liblzma/simple \
	-Ixz-$(VER)/src/common \
	-DHAVE_CONFIG_H -DTUKLIB_SYMBOL_PREFIX=lzma_ \
	-Wall -Wextra -Wvla -Wformat=2 -Winit-self -Wmissing-include-dirs -Wstrict-aliasing -Wfloat-equal \
	-Wundef -Wshadow -Wpointer-arith -Wbad-function-cast -Wwrite-strings -Wlogical-op -Waggregate-return \
	-Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes -Wmissing-declarations -Wmissing-noreturn -Wredundant-decls \
	-O3 -flto -fvisibility=hidden -fno-asynchronous-unwind-tables -fpic -pthread

LDFLAGS := $(LINK_INSTALLNAME_LOADERPATH) -shared -s

lzma-ff.o: lzma-ff.c
	$(C) $(CFLAGS) $< -o$@

xz-$(VER)/src/common/%.o: xz-$(VER)/src/common/%.c
	$(C) $(CFLAGS) $< -o$@

HEADERS := $(wildcard xz-$(VER)/src/liblzma/check/*.h) \
	$(wildcard xz-$(VER)/src/liblzma/common/*.h) \
	$(wildcard xz-$(VER)/src/liblzma/delta/*.h) \
	$(wildcard xz-$(VER)/src/liblzma/lz/*.h) \
	$(wildcard xz-$(VER)/src/liblzma/lzma/*.h) \
	$(wildcard xz-$(VER)/src/liblzma/rangecoder/*.h) \
	$(wildcard xz-$(VER)/src/liblzma/simple/*.h)

xz-$(VER)/src/liblzma/%.o: xz-$(VER)/src/liblzma/%.c $(HEADERS)
	$(C) $(CFLAGS) $< -o $@

liblzma-ff.$(SO): $(OBJ)
	$(LINK) $(LDFLAGS) $+ -o $@

clean:
	$(RM) $(OBJ) xz-$(VER)

distclean: clean
	$(RM) liblzma-ff.so liblzma-ff.dll liblzma-ff.dylib
