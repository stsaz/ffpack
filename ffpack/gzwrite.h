/** ffpack: .gz writer
2020, Simon Zolin */

/*
ffgzwrite_init
ffgzwrite_destroy
ffgzwrite_process
ffgzwrite_finish
ffgzwrite_error
*/

#pragma once

#include <ffpack/gz-fmt.h>
#include <ffbase/string.h>
#include <zlib/zlib-ff.h>

typedef struct ffgzwrite {
	ffuint state;
	const char *error;
	ffstr buf;
	ffuint64 total_rd;
	ffuint crc;
	z_ctx *lz;
	ffuint lz_flush;
} ffgzwrite;

typedef struct ffgzwrite_conf {
	int deflate_level; // 0:default
	int deflate_mem; // 0:default

	ffstr name; // must not contain '\0'
	ffuint mtime; // seconds since 1970
	ffstr comment; // must not contain '\0'
} ffgzwrite_conf;

/** Prepare for writing
Return 0 on success */
static int ffgzwrite_init(ffgzwrite *w, ffgzwrite_conf *conf);

/** Close writer */
static void ffgzwrite_destroy(ffgzwrite *w);

enum FFGZWRITE_R {
	FFGZWRITE_MORE, // need more input data
	FFGZWRITE_DATA, // have more compressed data for user
	FFGZWRITE_DONE,
	FFGZWRITE_ERROR,
};

/** Write the next chunk
output: pointer to an empty string for the output data
Return enum FFGZWRITE_R */
static int ffgzwrite_process(ffgzwrite *w, ffstr *input, ffstr *output);

/** Input data is finished */
static inline void ffgzwrite_finish(ffgzwrite *w)
{
	w->lz_flush = Z_FINISH;
}

/** Get last error message */
static inline const char* ffgzwrite_error(ffgzwrite *w)
{
	return w->error;
}

#define FFGZWRITE_BUFCAP  (64*1024)

static inline int ffgzwrite_init(ffgzwrite *w, ffgzwrite_conf *conf)
{
	ffmem_zero_obj(w);

	struct gz_header_info info = {};
	info.comment = conf->comment;
	info.name = conf->name;
	info.mtime_sec = conf->mtime;
	ffsize cap = gz_header_write(NULL, &info);
	cap = ffmax(cap, FFGZWRITE_BUFCAP);
	if (NULL == ffstr_alloc(&w->buf, cap)) {
		return -1;
	}
	w->buf.len = gz_header_write(w->buf.ptr, &info);

	z_conf zconf = {};
	zconf.level = conf->deflate_level;
	zconf.mem = conf->deflate_mem;
	if (0 != z_deflate_init(&w->lz, &zconf)) {
		w->error = "z_deflate_init()";
		return -1;
	}
	return 0;
}

static inline void ffgzwrite_destroy(ffgzwrite *w)
{
	if (w->lz != NULL) {
		z_deflate_free(w->lz);
		w->lz = NULL;
	}
	ffstr_free(&w->buf);
}

/** Fast CRC32 implementation using 8k table */
extern ffuint crc32(const void *buf, ffsize size, ffuint crc);

/* .gz write:
. write header
. compress data
. write trailer
*/
static inline int ffgzwrite_process(ffgzwrite *w, ffstr *input, ffstr *output)
{
	enum { W_HDR, W_DATA, W_TRL, W_DONE, };

	for (;;) {
		switch (w->state) {

		case W_HDR:
			ffstr_set2(output, &w->buf);
			w->buf.len = 0;
			w->state = W_DATA;
			return FFGZWRITE_DATA;

		case W_DATA: {
			ffsize rd = input->len;
			int r = z_deflate(w->lz, input->ptr, &rd, w->buf.ptr, FFGZWRITE_BUFCAP, w->lz_flush);

			w->crc = crc32((void*)input->ptr, rd, w->crc);
			ffstr_shift(input, rd);
			w->total_rd += rd;

			if (r == Z_DONE) {
				w->state = W_TRL;
				continue;
			} else if (r < 0) {
				w->error = "z_deflate";
				return FFGZWRITE_ERROR;
			}
			if (r == 0)
				return FFGZWRITE_MORE;
			w->buf.len += r;

			ffstr_set2(output, &w->buf);
			w->buf.len = 0;
			return FFGZWRITE_DATA;
		}

		case W_TRL:
			w->buf.len = gz_trailer_write(w->buf.ptr, w->crc, w->total_rd);
			ffstr_set2(output, &w->buf);
			w->buf.len = 0;
			w->state = W_DONE;
			return FFGZWRITE_DATA;

		case W_DONE:
			return FFGZWRITE_DONE;

		default:
			FF_ASSERT(0);
			return FFGZWRITE_ERROR;
		}
	}
}
