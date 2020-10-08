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

# set utils
RM := rm -f
CP := cp -u

ROOT := ..
FFPACK_DIR := $(ROOT)/ffpack
FFBASE_DIR := $(ROOT)/ffbase
FF3PT_DIR := $(ROOT)/ff-3pt
FF3PT_BINDIR := $(ROOT)/ff-3pt-bin/$(OS)-amd64
HDR := $(wildcard $(FFPACK_DIR)/ffpack/*.h)

OUT_DIR := $(FFPACK_DIR)/test

TESTER := $(OUT_DIR)/ffpack-test
ifeq ($(OS),win)
	TESTER := $(OUT_DIR)/ffpack-test.exe
endif

TEST_OBJ := \
	$(OUT_DIR)/main.o \
	$(OUT_DIR)/gz.o \
	$(OUT_DIR)/xz.o \
	$(OUT_DIR)/compat.o \
	\
	$(OUT_DIR)/crc.o

all: $(TESTER)

clean:
	$(RM) $(TESTER) $(TEST_OBJ)

TEST_CFLAGS := -I$(FFPACK_DIR) -I$(FFBASE_DIR) -I$(FF3PT_DIR) -Wall -Wextra -Werror -fvisibility=hidden
TEST_CFLAGS += -DFF_DEBUG -O0 -g
TEST_CXXFLAGS := $(TEST_CFLAGS)
TEST_CFLAGS += -std=gnu99
# TEST_CFLAGS += -fsanitize=address
# TEST_LDFLAGS += -fsanitize=address

$(OUT_DIR)/%.o: $(FF3PT_DIR)/crc/%.c $(FFPACK_DIR)/test/Makefile
	$(C) $(TEST_CFLAGS) $< -o $@

$(OUT_DIR)/%.o: $(FFPACK_DIR)/test/%.c $(HDR) $(FFPACK_DIR)/test/Makefile
	$(C) $(TEST_CFLAGS) $< -o $@

$(OUT_DIR)/%.o: $(FFPACK_DIR)/test/%.cpp $(HDR) $(FFPACK_DIR)/test/Makefile
	$(CXX) $(TEST_CXXFLAGS) $< -o $@

$(TESTER): $(TEST_OBJ)
	$(LINKER) $(TEST_LDFLAGS) $+ $(FF3PT_BINDIR)/libz-ff.so $(FF3PT_BINDIR)/liblzma-ff.so -o $@
	$(CP) $(FF3PT_BINDIR)/libz-ff.so $(OUT_DIR)/
	$(CP) $(FF3PT_BINDIR)/liblzma-ff.so $(OUT_DIR)/
