include ../ffbase/conf.mk

FPK_CF := -fpic
CFLAGS += $(FPK_CF)
CXXFLAGS += $(FPK_CF)

FPK_LF := $(LINK_INSTALLNAME_LOADERPATH) $(LINKFLAGS_USER)
FPK_LF += -s
LINKFLAGS += $(FPK_LF)
LINKXXFLAGS += $(FPK_LF) -static-libstdc++

# Set utils
CURL := curl -L
UNTAR_ZST := tar -x --zstd -f
UNTAR_XZ := tar xJf

SYS := $(OS)
ifeq "$(SYS)" "android"
	include ../andk.mk
	CFLAGS := $(FPK_CF) $(A_CFLAGS)
	CXXFLAGS := $(FPK_CF) $(A_CFLAGS)
	LINKFLAGS := $(FPK_LF) $(A_LINKFLAGS)
	LINKXXFLAGS := $(FPK_LF) $(A_LINKFLAGS)
endif
