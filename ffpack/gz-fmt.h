/** ffpack: .gz format
2020, Simon Zolin */

/*
gz_header_read
gz_header_write
gz_trailer_read
gz_trailer_write
*/

/* Format:
(HEADER DATA TRAILER)...
*/

#pragma once

#include <ffbase/string.h>

struct gz_header_info {
	ffstr extra;
	ffstr name;
	ffstr comment; // must not contain '\0'
	ffuint mtime_sec; // since 1970
};

struct gz_header {
	ffbyte id[2]; // 0x1f, 0x8b
	ffbyte comp_method; // 8=deflate
	ffbyte flags; // enum GZ_FLAGS
	ffbyte mtime[4];
	ffbyte exflags;
	ffbyte fstype; // 0=fat, 3=unix, 11=ntfs, 255=unknown
	// GZ_FEXTRA: len_lo[1], len_hi[1], data[]
	// GZ_FNAME: string \0
	// GZ_FCOMMENT: string \0
	// GZ_FHDRCRC: crc16
};

struct gz_trailer {
	ffbyte crc[4]; // CRC of uncompressed data
	ffbyte orig_size[4]; // size of uncompressed data
};

enum GZ_FLAGS {
	GZ_FHDRCRC = 0x02,
	GZ_FEXTRA = 0x04,
	GZ_FNAME = 0x08,
	GZ_FCOMMENT = 0x10,
	GZ_F_ALL = 0x1e,
};

/** Read .gz header
Return enum GZ_FLAGS;
 -1 on error */
static inline int gz_header_read(const void *buf, ffuint *comp_method, ffuint *mtime_sec)
{
	const struct gz_header *h = (struct gz_header*)buf;
	if (!(h->id[0] == 0x1f && h->id[1] == 0x8b)) {
		return -1;
	}
	*comp_method = h->comp_method;
	*mtime_sec = ffint_le_cpu16_ptr(h->mtime);
	return h->flags;
}

/** Write .gz header
Return N of bytes written */
static inline ffsize gz_header_write(void *buf, const struct gz_header_info *info)
{
	if (buf == NULL) {
		return sizeof(struct gz_header) + info->name.len+1 + info->comment.len+1;
	}

	ffbyte *d = (ffbyte*)buf;
	static const ffbyte defhdr[] = { 0x1f, 0x8b, 8, 0 };
	ffmem_copy(d, defhdr, sizeof(defhdr));
	struct gz_header *h = (struct gz_header*)buf;
	*(int*)h->mtime = ffint_le_cpu32(info->mtime_sec);
	h->exflags = 0;
	h->fstype = 255;
	d += sizeof(struct gz_header);

	if (info->name.len != 0) {
		h->flags |= GZ_FNAME;
		ffmem_copy(d, info->name.ptr, info->name.len);
		d[info->name.len] = '\0';
		d += info->name.len + 1;
	}

	if (info->comment.len != 0) {
		h->flags |= GZ_FCOMMENT;
		ffmem_copy(d, info->comment.ptr, info->comment.len);
		d[info->comment.len] = '\0';
		d += info->comment.len + 1;
	}

	return d - (ffbyte*)buf;
}

/** Read .gz trailer */
static inline void gz_trailer_read(const void *buf, ffuint *crc, ffuint *orig_size)
{
	const struct gz_trailer *t = (struct gz_trailer*)buf;
	*crc = ffint_le_cpu32_ptr(t->crc);
	*orig_size = ffint_le_cpu32_ptr(t->orig_size);
}

/** Write .gz trailer
Return N of bytes written */
static inline int gz_trailer_write(void *buf, ffuint crc, ffuint orig_size)
{
	struct gz_trailer *t = (struct gz_trailer*)buf;
	*(int*)t->crc = ffint_le_cpu32(crc);
	*(int*)t->orig_size = ffint_le_cpu32(orig_size);
	return sizeof(struct gz_trailer);
}
