/** ffpack: .xz format
2020, Simon Zolin */

/*
(STM_HDR  [(BLK_HDR  DATA  [BLK_PADDING]  [CHECK])...]  IDX  STM_FTR)...
*/

#pragma once

#include <ffbase/string.h>
#include <lzma/lzma-ff.h>

struct xz_stmhdr {
	ffbyte magic[6]; // "\xFD" "7zXZ" "\0"
	ffbyte flags[2]; // 0x00;  bit 4..7: =0,  bit 0..3: check_method
	ffbyte crc32[4]; // CRC of flags[]
};

struct xz_stmftr {
	ffbyte crc32[4]; // CRC of index_size and flags
	ffbyte index_size[4]; // real_size = (index_size + 1) * 4
	ffbyte flags[2]; // =xz_stmhdr.flags
	ffbyte magic[2]; // "YZ"
};

struct xz_blkhdr {
	ffbyte size; // =1..255. real_size = (size + 1) * 4
	ffbyte flags;
		// have_osize :1
		// have_size :1
		// res :4 // =0
		// nfilt :2
	// varint size
	// varint osize
	// filt_flags filt_flags_list[]
	// ffbyte padding[0...] // =0
	// ffbyte crc32[4]
};

struct xz_idx {
	ffbyte indicator; // =0
	// varint nrec
	// struct {
	// 	varint size; // without padding
	// 	varint osize;
	// } recs[];
	// byte padding[0..3] // =0
	// byte crc32[4]
};

/** Fast CRC32 implementation using 8k table */
FF_EXTERN ffuint crc32(const void *buf, ffsize size, ffuint crc);

/** Parse stream header.
Return check method;  <0 on error. */
static inline int xz_stmhdr_read(const void *buf, const char **error)
{
	const struct xz_stmhdr *h = (struct xz_stmhdr*)buf;

	if (!!ffmem_cmp(h->magic, "\xFD" "7zXZ\0", 6)) {
		*error = "bad stream header";
		return -1;
	}

	ffuint crc = crc32((void*)h->flags, 2, 0);
	if (crc != ffint_le_cpu32(*(ffuint*)h->crc32)) {
		*error = "bad stream header CRC";
		return -1;
	}

	if (h->flags[0] != 0 || (h->flags[1] & 0xf0)) {
		*error = "bad stream header flags";
		return -1;
	}

	return (h->flags[1] & 0x0f);
}

/** Read .xz stream footer
Return index size
  <0 on error */
static inline ffint64 xz_stmftr_read(const void *buf, const char **error)
{
	const struct xz_stmftr *f = (struct xz_stmftr*)buf;

	ffuint crc = crc32((void*)f->index_size, 6, 0);
	if (crc != ffint_le_cpu32(*(ffuint*)f->crc32)) {
		*error = "bad stream footer CRC";
		return -1;
	}

	if (!!ffmem_cmp(f->magic, "YZ", 2)) {
		*error = "bad stream footer";
		return -1;
	}

	ffuint64 idx_size = (ffint_le_cpu32(*(int*)f->index_size) + 1) * 4;
	if (idx_size > (ffuint)-1) {
		*error = "too large index size in stream footer";
		return -1;
	}
	return idx_size;
}

/** Read .xz variable integer
[(1X*)...]  0X*
Return -1 on error and clear 'd' */
static inline ffuint64 xz_varint(ffstr *d)
{
	ffuint i = 0;
	ffuint64 n = 0;
	const ffbyte *p = (ffbyte*)d->ptr;
	ffsize len = ffmin(d->len, 9);

	for (;;) {
		if (i == len) {
			ffstr_shift(d, d->len);
			return (ffuint64)-1;
		}

		n |= (ffuint64)(p[i] & ~0x80) << (i * 7);

		if (!(p[i++] & 0x80))
			break;
	}

	ffstr_shift(d, i);
	return n;
}

/** Read .xz block header
Return number of filters
  <0 on error */
static int xz_blkhdr_read(const void *buf, ffsize len, lzma_filter_props *filts, const char **error)
{
	const struct xz_blkhdr *h = (struct xz_blkhdr*)buf;
	ffstr d;
	ffstr_set(&d, buf, len);

	if (d.len < 2 || (h->flags & 0x3c) != 0) { // res
		*error = "bad block header";
		return -1;
	}
	ffstr_shift(&d, 2); //skip "size", "flags"

	if (h->flags & 0x40) // have_size
		/*size =*/ xz_varint(&d);

	if (h->flags & 0x80) // have_osize
		/*osize =*/ xz_varint(&d);

	ffuint nfilt = (h->flags & 0x03) + 1;
	for (ffuint i = 0;  i != nfilt;  i++) {
		filts[i].id = xz_varint(&d);
		filts[i].prop_len = xz_varint(&d);
		filts[i].props = (char*)d.ptr;
		if (d.len == 0) {
			*error = "bad block header";
			return -1;
		}
		ffstr_shift(&d, filts[i].prop_len);
	}

	ffuint padding = d.len - 4;
	if (padding >= 4 || !!ffmem_cmp(d.ptr, "\x00\x00\x00", padding)) {
		*error = "bad block header";
		return -1;
	}
	ffstr_shift(&d, padding);

	ffuint crc = crc32(buf, len - 4, 0);
	if (d.len != 4 || crc != ffint_le_cpu32(*(ffuint*)d.ptr)) {
		*error = "bad block header CRC";
		return -1;
	}

	return nfilt;
}

/** Read .xz index
Return the original file size
  <0 on error */
static ffint64 xz_idx_read(const void *buf, ffsize len, const char **error)
{
	ffuint64 total_osize = 0;
	ffstr d;
	ffstr_set(&d, buf, len);

	if (d.len == 0 || d.ptr[0] != 0) {
		*error = "bad index";
		return -1;
	}
	ffstr_shift(&d, 1);

	ffuint64 nrec = xz_varint(&d);
	// if (nrec != )
	// 	return ERR;
	for (ffuint64 i = 0;  i != nrec;  i++) {
		ffuint64 size = xz_varint(&d);
		ffuint64 osize = xz_varint(&d);
		total_osize += osize;
		// fflog(10, "index: block #%U: %U -> %U", i, osize, size);
		(void)size;
	}

	ffuint padding = d.len % 4;
	if (padding > d.len || !!ffmem_cmp(d.ptr, "\x00\x00\x00", padding)) {
		*error = "bad index";
		return -1;
	}
	ffstr_shift(&d, padding);

	ffuint crc = crc32(buf, len - 4, 0);
	if (d.len != 4 || crc != ffint_le_cpu32(*(ffuint*)d.ptr)) {
		*error = "bad index CRC";
		return -1;
	}
	return total_osize;
}
