/** ffpack: .zip writer
* compression: stored, deflated
* supports unseekable output device (via zip local file trailer)

2020, Simon Zolin */

/*
ffzipwrite_destroy
ffzipwrite_fileadd
ffzipwrite_filefinish
ffzipwrite_process
ffzipwrite_offset
ffzipwrite_finish
ffzipwrite_error
*/

#pragma once

#include <ffpack/zip-fmt.h>
#include <ffpack/path.h>
#include <zlib/zlib-ff.h>
#include <ffbase/string.h>
#include <ffbase/vector.h>

typedef struct ffzipwrite ffzipwrite;
typedef int (*_ffzipwrite_pack)(ffzipwrite *z, ffstr input, ffstr *output, ffsize *rd);

struct ffzipwrite {
	ffuint state;
	const char *error;
	ffvec buf;
	ffvec fhdr_buf;
	ffvec cdir; // growing CDIR data
	ffuint64 file_rd, file_wr;
	ffuint64 total_rd, total_wr;
	ffuint crc; // current CRC of uncompressed data
	ffuint cdir_items;
	ffuint cdir_hdrlen;
	z_ctx *lz;
	ffuint lz_flush;
	ffuint64 offset;
	ffuint64 fhdr_offset;
	_ffzipwrite_pack pack_func;

	/* If TRUE, the writer won't ask user to seek on output file */
	ffuint non_seekable :1;

	/* Offset in seconds for the current local time (GMT+XX) */
	int timezone_offset;
};

typedef struct ffzipwrite_conf {
	int deflate_level; // 0:default
	int deflate_mem; // 0:default

	ffstr name;
	fftime mtime; // seconds since 1970
	ffuint attr_win, attr_unix; // Windows/UNIX file attributes
	ffuint uid, gid; // UNIX user/group ID
	enum ZIP_COMP compress_method;
} ffzipwrite_conf;

/** Prepare for writing the next file
Auto naming:
  * dir -> dir/
  * { /a, ./a, ../a } -> a
  * c:\a\b -> a/b (Windows)
Return 0 on success */
static int ffzipwrite_fileadd(ffzipwrite *w, ffzipwrite_conf *conf);

/** Close writer */
static void ffzipwrite_destroy(ffzipwrite *w);

enum FFZIPWRITE_R {
	/* Need more input data
	Expecting ffzipwrite_process() with more data or ffzipwrite_filefinish() */
	FFZIPWRITE_MORE,

	/* Have more output data for user
	Expecting ffzipwrite_process() */
	FFZIPWRITE_DATA,

	/* The next output data must be written at offset ffzipwrite_offset()
	Expecting ffzipwrite_process() */
	FFZIPWRITE_SEEK,

	/* Finished processing the current file
	Expecting ffzipwrite_fileadd() or ffzipwrite_finish() */
	FFZIPWRITE_FILEDONE,

	/* Finished writing .zip file */
	FFZIPWRITE_DONE,

	/* Fatal error */
	FFZIPWRITE_ERROR,
};

/** Write the next chunk
output: pointer to an empty string for the output data
Return enum FFZIPWRITE_R */
static int ffzipwrite_process(ffzipwrite *w, ffstr *input, ffstr *output);

/** Get outputput offset */
static inline ffuint64 ffzipwrite_offset(ffzipwrite *w)
{
	return w->offset;
}

/** Input data for the current file is finished */
static inline void ffzipwrite_filefinish(ffzipwrite *w)
{
	FF_ASSERT(w->state == 1);
	w->lz_flush = Z_FINISH;
}

/** All input data is finished */
static inline void ffzipwrite_finish(ffzipwrite *w)
{
	FF_ASSERT(w->state == 0);
	w->state = 6;
}

/** Get last error message */
static inline const char* ffzipwrite_error(ffzipwrite *w)
{
	return w->error;
}


static inline int _ffzipwrite_stored_pack(ffzipwrite *w, ffstr input, ffstr *output, ffsize *rd)
{
	*rd = input.len;
	if (input.len == 0) {
		if (w->lz_flush == 0)
			return FFZIPWRITE_MORE;
		return FFZIPWRITE_DONE;
	}

	*output = input;
	return FFZIPWRITE_DATA;
}

static int _ffzipwrite_deflated_pack(ffzipwrite *w, ffstr input, ffstr *output, ffsize *rd);

static inline int _ffzipwrite_deflated_init(ffzipwrite *w, ffzipwrite_conf *conf)
{
	z_conf zconf = {};
	zconf.level = conf->deflate_level;
	zconf.mem = conf->deflate_mem;
	if (0 != z_deflate_init(&w->lz, &zconf)) {
		w->error = "z_deflate_init()";
		return -1;
	}
	w->pack_func = _ffzipwrite_deflated_pack;
	return 0;
}

static inline int _ffzipwrite_deflated_pack(ffzipwrite *w, ffstr input, ffstr *output, ffsize *rd)
{
	*rd = input.len;
	int r = z_deflate(w->lz, input.ptr, rd, (char*)w->buf.ptr, w->buf.cap, w->lz_flush);

	if (r == Z_DONE) {
		if (w->lz != NULL) {
			z_deflate_free(w->lz);
			w->lz = NULL;
		}
		return FFZIPWRITE_DONE;
	} else if (r < 0) {
		w->error = "z_deflate";
		return FFZIPWRITE_ERROR;
	} else if (r == 0) {
		return FFZIPWRITE_MORE;
	}

	w->buf.len += r;
	ffstr_set2(output, &w->buf);
	w->buf.len = 0;
	return FFZIPWRITE_DATA;
}

#define _FFZIPWRITE_BUFCAP  (64*1024)

static inline int ffzipwrite_fileadd(ffzipwrite *w, ffzipwrite_conf *conf)
{
	int rc = -1;
	if (w->state != 0)
		return -1;

	int dir = (conf->attr_win & 0x10) || (conf->attr_unix & 0040000);

	ffstr name = {};
	if (NULL == ffstr_alloc(&name, conf->name.len + 1))
		return -1;
	name.len = _ffpack_path_normalize(name.ptr, conf->name.len, conf->name.ptr, conf->name.len, FFPATH_FORCE_SLASH | FFPATH_SIMPLE);
	if (name.len != 0 && dir) {
		if (name.ptr[name.len - 1] != '/')
			name.ptr[name.len++] = '/';
	}

	struct zip_fileinfo info = {};
	info.name = name;
	info.mtime = conf->mtime;
	info.attr_win = conf->attr_win;
	info.attr_unix = conf->attr_unix;
	info.uid = conf->uid;
	info.gid = conf->gid;
	if (w->non_seekable)
		info.compressed_size = (ffuint64)-1;

	int comp_method = (dir) ? ZIP_STORED : conf->compress_method;
	info.compress_method = (enum ZIP_COMP)comp_method;
	info.hdr_offset = w->total_wr;

	int r;
	r = zip_filehdr_write(NULL, &info, 0);
	r = ffmax(r, _FFZIPWRITE_BUFCAP);
	if (NULL == ffvec_reallocT(&w->buf, r, char))
		goto end;
	r = zip_filehdr_write(w->buf.ptr, &info, w->timezone_offset);
	if (r < 0) {
		w->error = "zip_filehdr_write";
		goto end;
	}
	w->buf.len = r;

	// we need to update CRC and file sizes after file data is written
	ffvec_add2T(&w->fhdr_buf, &w->buf, char);

	// prepare CDIR entry now and not later, or otherwise we'd have to store values from 'conf'
	r = zip_cdir_write(NULL, &info, 0);
	if (NULL == ffvec_growtwiceT(&w->cdir, (ffsize)r, char))
		goto end;
	r = zip_cdir_write(ffslice_endT(&w->cdir, char), &info, w->timezone_offset);
	if (r < 0) {
		w->error = "zip_cdir_write";
		goto end;
	}
	w->cdir_hdrlen = r;

	switch (comp_method) {

	case ZIP_STORED:
		w->pack_func = _ffzipwrite_stored_pack;
		break;

	case ZIP_DEFLATED:
		if (0 != _ffzipwrite_deflated_init(w, conf))
			return -1;
		break;

	default:
		w->pack_func = NULL;
	}

	rc = 0;

end:
	ffstr_free(&name);
	return rc;
}

static inline void ffzipwrite_destroy(ffzipwrite *w)
{
	if (w->lz != NULL) {
		z_deflate_free(w->lz);
		w->lz = NULL;
	}
	ffvec_free(&w->cdir);
	ffvec_free(&w->buf);
	ffvec_free(&w->fhdr_buf);
}

/** Fast CRC32 implementation using 8k table */
extern ffuint crc32(const void *buf, ffsize size, ffuint crc);

/* .zip write:
for each new file:
 . write local file header
 . add CDIR header to a buffer
 . write file data
 . update CDIR header
 . seek to file header and update it or write file trailer
. write CDIR data, CDIR zip64 trailer, CDIR zip64 trailer locator and CDIR trailer
*/
static inline int ffzipwrite_process(ffzipwrite *w, ffstr *input, ffstr *output)
{
	int r;
	enum {
		W_FHDR, W_DATA = 1, W_FHDR_UPDATE, W_END_SEEK, W_FTRL, W_FDONE, W_CDIR = 6, W_DONE,
	};

	for (;;) {
		switch (w->state) {

		case W_FHDR:
			if (w->buf.len == 0) {
				w->error = "file info isn't ready";
				return FFZIPWRITE_ERROR;
			}
			ffstr_set2(output, &w->buf);
			w->buf.len = 0;
			w->fhdr_offset = w->total_wr;
			w->total_wr += output->len;
			w->crc = 0;
			w->file_rd = 0;
			w->file_wr = 0;
			w->state = W_DATA;
			return FFZIPWRITE_DATA; // file header data

		case W_DATA: {
			ffsize rd;
			r = w->pack_func(w, *input, output, &rd);

			w->crc = crc32((void*)input->ptr, rd, w->crc);
			ffstr_shift(input, rd);
			w->file_rd += rd;
			w->total_rd += rd;

			if (r == FFZIPWRITE_DONE) {
				zip_cdir_finishwrite(ffslice_endT(&w->cdir, char), w->file_rd, w->file_wr, w->crc);
				w->cdir.len += w->cdir_hdrlen;
				w->cdir_items++;

				if (w->non_seekable) {
					w->state = W_FTRL;
					continue;
				}
				w->state = W_FHDR_UPDATE;
				w->offset = w->fhdr_offset;
				return FFZIPWRITE_SEEK; // seek back to file header

			} else if (r != FFZIPWRITE_DATA) {
				return r;
			}

			w->file_wr += output->len;
			w->total_wr += output->len;
			return FFZIPWRITE_DATA; // compressed file data
		}

		case W_FHDR_UPDATE:
			zip_filehdr_update(w->fhdr_buf.ptr, w->file_rd, w->file_wr, w->crc);
			ffstr_set2(output, &w->fhdr_buf);
			w->fhdr_buf.len = 0;
			w->state = W_END_SEEK;
			return FFZIPWRITE_DATA; // updated file header data

		case W_END_SEEK:
			w->state = W_FDONE;
			w->offset = w->total_wr;
			return FFZIPWRITE_SEEK; // seek to the end of file

		case W_FTRL:
			w->buf.len = zip_filetrl64_write(w->buf.ptr, w->file_rd, w->file_wr, w->crc);

			ffstr_set2(output, &w->buf);
			w->buf.len = 0;
			w->total_wr += output->len;
			w->state = W_FDONE;
			return FFZIPWRITE_DATA; // file trailer data

		case W_FDONE:
			w->lz_flush = 0;
			w->state = W_FHDR;
			return FFZIPWRITE_FILEDONE;

		case W_CDIR: {
			r = zip_cdirtrl64_write(NULL, 0, 0, 0);
			r += zip_cdirtrl64_loc_write(NULL, 0, 0, 0);
			r += zip_cdirtrl_write(NULL, 0, 0, 0);
			if (NULL == ffvec_growT(&w->cdir, r, char))
				return FFZIPWRITE_ERROR;

			ffuint64 zip64_off = w->total_wr + w->cdir.len;
			w->cdir.len += zip_cdirtrl64_write(ffslice_endT(&w->cdir, char), w->cdir.len, w->total_wr, w->cdir_items);
			w->cdir.len += zip_cdirtrl64_loc_write(ffslice_endT(&w->cdir, char), 1, 0, zip64_off);
			w->cdir.len += zip_cdirtrl_write(ffslice_endT(&w->cdir, char), 0xffffffff, 0xffffffff, 0xffff);

			ffstr_set2(output, &w->cdir);
			w->state = W_DONE;
			return FFZIPWRITE_DATA; // the whole CDIR data
		}

		case W_DONE:
			return FFZIPWRITE_DONE;

		default:
			FF_ASSERT(0);
			return FFZIPWRITE_ERROR;
		}
	}
}
