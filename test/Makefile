# ffpack test makefile
# 2020, Simon Zolin

# set OS
ifndef $(OS)
	uname := $(shell uname)
	ifeq ($(uname),Linux)
		OS := linux
	else ifeq ($(uname),FreeBSD)
		OS := freebsd
	else ifeq ($(uname),Darwin)
		OS := apple
	else
		OS := win
	endif
endif

# set compiler
CPREFIX :=
# CPREFIX := x86_64-w64-mingw32-
COMPILER := gcc
ifeq ($(OS),freebsd)
	COMPILER := clang
endif
ifeq ($(OS),apple)
	COMPILER := clang
endif
C := $(CPREFIX)gcc -c
CXX := $(CPREFIX)g++ -c
LINKER := $(CPREFIX)gcc
ifeq ($(COMPILER),clang)
	C := clang -c
	CXX := clang++ -c
	LINKER := clang
endif
ifeq ($(OS),linux)
	LD_RPATH_ORIGIN := '-Wl,-rpath,$$ORIGIN' -Wl,--disable-new-dtags
else ifeq ($(OS),bsd)
	LD_RPATH_ORIGIN := '-Wl,-rpath,$$ORIGIN'
endif

# set utils
RM := rm -f
CP := cp -u
SO := so
ifeq ($(OS),win)
	SO := dll
else ifeq ($(OS),apple)
	SO := dylib
endif

ROOT := ..
FFPACK_DIR := $(ROOT)/ffpack
FFBASE_DIR := $(ROOT)/ffbase
HDR := $(wildcard $(FFPACK_DIR)/ffpack/*.h)

OUT_DIR := $(FFPACK_DIR)/test

TESTER := $(OUT_DIR)/ffpack-test
ifeq ($(OS),win)
	TESTER := $(OUT_DIR)/ffpack-test.exe
endif

TEST_OBJ := \
	$(OUT_DIR)/7z.o \
	$(OUT_DIR)/gz.o \
	$(OUT_DIR)/iso.o \
	$(OUT_DIR)/main.o \
	$(OUT_DIR)/tar.o \
	$(OUT_DIR)/xz.o \
	$(OUT_DIR)/zip.o \
	$(OUT_DIR)/compat.o \
	\
	$(OUT_DIR)/crc.o

all: $(TESTER)

clean:
	$(RM) $(TESTER) $(TEST_OBJ)

depend:
	cd ../zlib && make -Rr
	cd ../lzma && make -Rr

TEST_CFLAGS := -I$(FFPACK_DIR) -I$(FFBASE_DIR) \
	-Wall -Wextra -Werror -fvisibility=hidden
TEST_CFLAGS += -DFF_DEBUG -O0 -g
TEST_CXXFLAGS := $(TEST_CFLAGS)
TEST_CFLAGS += -std=gnu99
# TEST_CFLAGS += -fsanitize=address
# TEST_LDFLAGS += -fsanitize=address
TEST_LDFLAGS += $(LD_RPATH_ORIGIN)

$(OUT_DIR)/crc.o: $(FFPACK_DIR)/crc/crc.c $(FFPACK_DIR)/test/Makefile
	$(C) $(TEST_CFLAGS) $< -o $@

$(OUT_DIR)/%.o: $(FFPACK_DIR)/test/%.c $(HDR) $(FFPACK_DIR)/test/Makefile
	$(C) $(TEST_CFLAGS) $< -o $@

$(OUT_DIR)/%.o: $(FFPACK_DIR)/test/%.cpp $(HDR) $(FFPACK_DIR)/test/Makefile
	$(CXX) $(TEST_CXXFLAGS) $< -o $@

$(TESTER): $(TEST_OBJ)
	$(CP) $(FFPACK_DIR)/zlib/libz-ff.$(SO) $(OUT_DIR)/
	$(CP) $(FFPACK_DIR)/lzma/liblzma-ff.$(SO) $(OUT_DIR)/
	LD_LIBRARY_PATH='$(OUT_DIR)' \
		$(LINKER) $(TEST_LDFLAGS) $+ \
		libz-ff.$(SO) liblzma-ff.$(SO) -o $@
