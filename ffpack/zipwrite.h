/** ffpack: .zip writer
* compression: stored, deflated, zstandard
* supports unseekable output device (via zip local file trailer)

Building:
Define FFPACK_ZIPWRITE_ZLIB, FFPACK_ZIPWRITE_ZSTD, FFPACK_ZIPWRITE_CRC32
 to use zlib/zstd/crc32 third-party code referenced by ffpack.
Otherwise, you must provide your own implementation for CRC32 filter
 and for deflate/zstandard filters (for deflate/zstd compression methods).

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
#include <ffbase/string.h>
#include <ffbase/vector.h>

typedef struct ffzipwrite_filter ffzipwrite_filter;
typedef struct ffzipwrite ffzipwrite;
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
	ffuint64 offset;
	ffuint64 fhdr_offset;
	int file_fin, arc_fin;

	struct {
		const ffzipwrite_filter *iface;
		void *obj;
	} filters[2];
	ffuint filter_cur;

	/* If TRUE, the writer won't ask user to seek on output file */
	ffuint non_seekable;

	/* Offset in seconds for the current local time (GMT+XX) */
	int timezone_offset;

	void *udata;
};

typedef struct ffzipwrite_conf {
	int deflate_level; // 0:default
	int deflate_mem; // 0:default

	int zstd_level; // 0:default
	ffuint zstd_workers;

	ffstr name;
	fftime mtime; // seconds since 1970
	ffuint attr_win, attr_unix; // Windows/UNIX file attributes
	ffuint uid, gid; // UNIX user/group ID
	enum ZIP_COMP compress_method;
	const ffzipwrite_filter *compress_filter;
	const ffzipwrite_filter *crc32_filter;
} ffzipwrite_conf;

struct ffzipwrite_filter {
	void* (*open)(ffzipwrite *w, ffzipwrite_conf *conf);
	void (*close)(void *obj, ffzipwrite *w);
	int (*process)(void *obj, ffzipwrite *w, ffstr *input, ffstr *output);
};

/** Prepare for writing the next file
Auto naming:
  * dir -> dir/
  * { /a, ./a, ../a } -> a
  * c:\a\b -> a/b (Windows)
Return 0 on success
  <0 on error
  -2 if normalized file name is empty (.e.g. for "/" or "." or "..") */
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

#ifdef FFPACK_ZIPWRITE_ZLIB
	#include <ffpack/zipwrite-libz.h>
#endif
#ifdef FFPACK_ZIPWRITE_ZSTD
	#include <ffpack/zipwrite-zstd.h>
#endif

/** Write the next chunk
output: pointer to an empty string for the output data
Return enum FFZIPWRITE_R */
static int ffzipwrite_process(ffzipwrite *w, ffstr *input, ffstr *output);

/** Get output offset */
static inline ffuint64 ffzipwrite_offset(ffzipwrite *w)
{
	return w->offset;
}

/** Input data for the current file is finished */
static inline void ffzipwrite_filefinish(ffzipwrite *w)
{
	w->file_fin = 1;
}

/** All input data is finished */
static inline void ffzipwrite_finish(ffzipwrite *w)
{
	w->arc_fin = 1;
}

/** Get last error message */
static inline const char* ffzipwrite_error(ffzipwrite *w)
{
	return w->error;
}


static void* _ffzipw_stored_open(ffzipwrite *w, ffzipwrite_conf *conf)
{
	(void)conf;
	return w;
}

static void _ffzipw_stored_close(void *obj, ffzipwrite *w)
{
	(void)obj; (void)w;
}

static int _ffzipw_stored_process(void *obj, ffzipwrite *w, ffstr *input, ffstr *output)
{
	(void)obj;
	if (input->len == 0) {
		if (w->file_fin)
			return 0xa11;
		return 0xfeed;
	}

	*output = *input;
	ffstr_shift(input, input->len);
	return 0;
}

static const ffzipwrite_filter _ffzipw_stored = {
	_ffzipw_stored_open, _ffzipw_stored_close, _ffzipw_stored_process
};


#ifdef FFPACK_ZIPWRITE_CRC32
/** Fast CRC32 implementation using 8k table */
extern ffuint crc32(const void *buf, ffsize size, ffuint crc);

static int _ffzipw_crc(void *obj, ffzipwrite *w, ffstr *input, ffstr *output)
{
	(void)obj;
	*output = *input;
	w->crc = crc32((void*)input->ptr, input->len, w->crc);
	ffstr_shift(input, input->len);
	return 0;
}

static const ffzipwrite_filter _ffzipw_crc32 = {
	NULL, NULL, _ffzipw_crc
};
#endif


#define _FFZIPWRITE_BUFCAP  (64*1024)

static inline int ffzipwrite_fileadd(ffzipwrite *w, ffzipwrite_conf *conf)
{
	struct zip_fileinfo info = {};
	int comp_method;
	int rc = -1;
	if (w->state != 0) // W_FHDR
		return -1;

	int dir = (conf->attr_win & 0x10) || (conf->attr_unix & 0040000);

	ffstr name = {};
	if (NULL == ffstr_alloc(&name, conf->name.len + 1))
		return -1;
	name.len = _ffpack_path_normalize(name.ptr, conf->name.len, conf->name.ptr, conf->name.len, _FFPACK_PATH_FORCE_SLASH | _FFPACK_PATH_SIMPLE);
	if (name.len == 0) {
		rc = -2;
		goto end;
	}
	if (dir && name.ptr[name.len - 1] != '/')
		name.ptr[name.len++] = '/';

	info.name = name;
	info.mtime = conf->mtime;
	info.attr_win = conf->attr_win;
	info.attr_unix = conf->attr_unix;
	info.uid = conf->uid;
	info.gid = conf->gid;
	if (w->non_seekable)
		info.compressed_size = (ffuint64)-1;

	comp_method = (dir) ? ZIP_STORED : conf->compress_method;
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

#ifdef FFPACK_ZIPWRITE_CRC32
	w->filters[0].iface = &_ffzipw_crc32;
#else
	FF_ASSERT(conf->crc32_filter != NULL);
#endif

	if (conf->crc32_filter != NULL)
		w->filters[0].iface = conf->crc32_filter;

	if (w->filters[1].iface != NULL) {
		w->filters[1].iface->close(w->filters[1].obj, w);
		w->filters[1].obj = NULL;
		w->filters[1].iface = NULL;
	}

	switch (comp_method) {

	case ZIP_STORED:
		w->filters[1].iface = &_ffzipw_stored;
		break;

#ifdef FFPACK_ZIPWRITE_ZLIB
	case ZIP_DEFLATED:
		w->filters[1].iface = &_ffzipw_deflated;
		break;
#endif

#ifdef FFPACK_ZIPWRITE_ZSTD
	case ZIP_ZSTANDARD:
		w->filters[1].iface = &_ffzipw_zstd;
		break;
#endif

	default:
		FF_ASSERT(conf->compress_filter != NULL);
		w->filters[1].iface = conf->compress_filter;
	}

	if (NULL == (w->filters[1].obj = w->filters[1].iface->open(w, conf)))
		return -1;

	w->filter_cur = 0;
	rc = 0;

end:
	ffstr_free(&name);
	return rc;
}

static inline void ffzipwrite_destroy(ffzipwrite *w)
{
	if (w->filters[1].iface != NULL) {
		w->filters[1].iface->close(w->filters[1].obj, w);
		w->filters[1].obj = NULL;
	}
	ffvec_free(&w->cdir);
	ffvec_free(&w->buf);
	ffvec_free(&w->fhdr_buf);
}

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
		W_FHDR = 0, W_DATA, W_FHDR_UPDATE, W_END_SEEK, W_FTRL, W_FDONE, W_CDIR, W_DONE,
	};

	for (;;) {
		switch (w->state) {

		case W_FHDR:
			if (w->arc_fin) {
				w->state = W_CDIR;
				continue;
			}
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
			if (w->filter_cur == 0) {
				w->filters[0].iface->process(w->filters[0].obj, w, input, output);
				*input = *output;
				w->filter_cur = 1;
				continue;
			}

			ffsize rd = input->len;
			r = w->filters[1].iface->process(w->filters[1].obj, w, input, output);
			rd -= input->len;
			w->file_rd += rd;
			w->total_rd += rd;

			switch (r) {
			case 0xfeed:
				w->filter_cur = 0;
				return FFZIPWRITE_MORE;

			case 0xa11:
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

			case 0:
				break;

			case 0xbad:
				return FFZIPWRITE_ERROR;
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
			w->file_fin = 0;
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
