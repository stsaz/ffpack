include ../ffbase/conf.mk

# Set utils
CURL := curl -L
UNTAR_ZST := tar -x --zstd -f
UNTAR_XZ := tar xJf

# Set compiler and append compiler & linker flags for Android
SYS := $(OS)
ifeq "$(SYS)" "android"
	include andk.mk
	CFLAGS += $(A_CFLAGS)
	CXXFLAGS += $(A_CFLAGS)
	LINKFLAGS += $(A_LINKFLAGS)
endif
