include config.mk
BINDIR := _$(SYS)-$(CPU)

LIBS := \
	lzma \
	zlib \
	zstd

default:
	mkdir -p $(BINDIR)
	$(MAKE) build

build: $(addprefix $(BINDIR)/lib,$(addsuffix -ffpack.$(SO),$(LIBS)))

$(BINDIR)/liblzma-ffpack.$(SO):
	$(MAKE) -C lzma -I..
	mkdir -p $(BINDIR)
	mv lzma/*.$(SO) $(BINDIR)/

$(BINDIR)/libzlib-ffpack.$(SO):
	$(MAKE) -C zlib -I..
	mkdir -p $(BINDIR)
	mv zlib/*.$(SO) $(BINDIR)/

libzstd: $(BINDIR)/libzstd-ffpack.$(SO)
$(BINDIR)/libzstd-ffpack.$(SO):
	$(MAKE) -C zstd -I..
	mkdir -p $(BINDIR)
	mv zstd/*.$(SO) $(BINDIR)/

md5:
	md5sum -b \
		lzma/xz-5.2.4.tar.xz \
		zlib/zlib-1.2.11.tar.xz \
		zstd/zstd-1.5.0.tar.zst \
		>packages.md5

md5check:
	md5sum -c packages.md5 --ignore-missing
