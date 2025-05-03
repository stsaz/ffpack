# ffpack test makefile
# 2020, Simon Zolin

ROOT := ..
FFPACK_DIR := $(ROOT)/ffpack
FFBASE_DIR := $(ROOT)/ffbase
HEADERS := $(wildcard $(FFPACK_DIR)/ffpack/*.h)

include $(FFPACK_DIR)/config.mk

TESTER := ffpack-test$(DOTEXE)

TEST_OBJ := \
	7z.o \
	gz.o \
	iso.o \
	main.o \
	tar.o \
	xz.o \
	zip.o \
	zstd.o \
	compat.o \
	\
	crc.o

all: $(TESTER)

clean:
	$(RM) $(TESTER) $(TEST_OBJ)
	cd ../zlib && make -Rr clean
	cd ../lzma && make -Rr clean
	cd ../zstd && make -Rr clean

libs:
	cd ../zlib && make -Rr
	cd ../lzma && make -Rr
	cd ../zstd && make -Rr

TEST_CFLAGS := -I$(FFPACK_DIR) -I$(FFBASE_DIR) \
	-Wall -Wextra
TEST_CFLAGS += -DFF_DEBUG -O0 -g
TEST_CXXFLAGS := $(TEST_CFLAGS)
TEST_CFLAGS += -std=gnu99
# TEST_CFLAGS += -fsanitize=address
# TEST_LDFLAGS += -fsanitize=address
TEST_LDFLAGS += $(LINK_RPATH_ORIGIN)

crc.o: $(FFPACK_DIR)/crc/crc.c $(FFPACK_DIR)/test/Makefile
	$(C) $(TEST_CFLAGS) $< -o $@

%.o: $(FFPACK_DIR)/test/%.c $(HEADERS) $(FFPACK_DIR)/test/Makefile
	$(C) $(TEST_CFLAGS) $< -o $@

zip.o: $(FFPACK_DIR)/test/zip.c $(HEADERS) $(FFPACK_DIR)/test/Makefile
	$(C) $(TEST_CFLAGS) -DFFPACK_ZIPWRITE_ZLIB -DFFPACK_ZIPWRITE_ZSTD -DFFPACK_ZIPWRITE_CRC32 $< -o $@

%.o: $(FFPACK_DIR)/test/%.cpp $(HEADERS) $(FFPACK_DIR)/test/Makefile
	$(CXX) $(TEST_CXXFLAGS) $< -o $@

$(TESTER): $(TEST_OBJ)
	$(CP) \
		$(FFPACK_DIR)/lzma/liblzma-ff.$(SO) \
		$(FFPACK_DIR)/zlib/libz-ff.$(SO) \
		$(FFPACK_DIR)/zstd/libzstd-ffpack.$(SO) \
		.
	$(LINK) $(TEST_LDFLAGS) $+ -L. -llzma-ff -lz-ff -lzstd-ffpack -o $@
