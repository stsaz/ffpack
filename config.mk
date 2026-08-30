include $(FFPACK)/../ffbase/conf.mk

FPK_CF := -fpic -fvisibility=hidden
CFLAGS += $(FPK_CF)

FPK_LF =
ifneq "$(OS)" "apple"
	FPK_LF += -fuse-ld=lld -static-libgcc
endif
FPK_LF += $(LINK_INSTALLNAME_LOADERPATH) $(LINKFLAGS_USER)
FPK_LF += -s
FPK_LF_TMP := $(LINKFLAGS)
LINKFLAGS = $(FPK_LF_TMP) $(FPK_LF)

# Set utils
CURL := curl -L
UNTAR := tar -x --no-same-owner -f

SYS := $(OS)
ifeq "$(SYS)" "android"
	include ../andk.mk
	CFLAGS := $(FPK_CF) $(A_CFLAGS)
	LINKFLAGS := $(FPK_LF) $(A_LINKFLAGS)
endif
