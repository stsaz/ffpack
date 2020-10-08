/** ffpack: .xz reader
2020, Simon Zolin */

/*
ffxzread_open
ffxzread_close
ffxzread_process
ffxzread_offset
ffxzread_error
ffxzread_getinfo
*/

#pragma once

#include <ffpack/xz-fmt.h>
#include <ffbase/vector.h>
#include <ffbase/string.h>
#include <lzma/lzma-ff.h>

typedef struct ffxzread_info {
	ffuint64 uncompressed_size;

	ffuint64 compressed_size; // how much compressed data we've read so far
} ffxzread_info;

typedef struct ffxzread {
	ffuint state, state_next;
	ffuint gather_size;
	ffvec buf;
	ffuint check_method;
	ffuint idx_size;
	ffuint64 offset;
	ffxzread_info info;
	const char *error;
	lzma_decoder *lzma;
} ffxzread;

/** Prepare for reading
total_size: .xz file size (allows to determine uncompressed data size)
Return 0 on success */
static int ffxzread_open(ffxzread *r, ffint64 total_size);

/** Close reader */
static void ffxzread_close(ffxzread *r);

enum FFXZREAD_R {
	FFXZREAD_MORE, // need more input data
	FFXZREAD_SEEK, // need input data at offset = ffxzread_offset()
	FFXZREAD_INFO, // user may call ffxzread_getinfo() to get info from header/trailer
	FFXZREAD_DATA, // have more decompressed data for user
	FFXZREAD_DONE,
	FFXZREAD_ERROR,
};

/** Read the next chunk
output: pointer to an empty string for the output data
Return enum FFXZREAD_R */
static int ffxzread_process(ffxzread *r, ffstr *input, ffstr *output);

/** Get input offset */
static inline ffuint64 ffxzread_offset(ffxzread *r)
{
	return r->offset;
}

/** Get last error message */
static inline const char* ffxzread_error(ffxzread *r)
{
	return r->error;
}

/** Get info from header/trailer */
static inline ffxzread_info* ffxzread_getinfo(ffxzread *r)
{
	return &r->info;
}

static inline int ffxzread_open(ffxzread *r, ffint64 total_size)
{
	ffmem_zero_obj(r);
	if (total_size >= 0) {
		r->offset = total_size - sizeof(struct xz_stmftr);
		if ((ffint64)r->offset <= 0) {
			r->error = "no xz footer";
			return -1;
		}
	}

	if (NULL == ffvec_allocT(&r->buf, 64*1024, char)) {
		return -1;
	}

	return 0;
}

static inline void ffxzread_close(ffxzread *r)
{
	ffvec_free(&r->buf);
	if (r->lzma != NULL) {
		lzma_decode_free(r->lzma);
		r->lzma = NULL;
	}
}

/* .xz read:
. Seek; read stream footer
. Seek; read index
. Seek; read stream header
. Read data
. Skip index and stream footer
*/
static inline int ffxzread_process(ffxzread *r, ffstr *input, ffstr *output)
{
	ffstr data;
	int rc;
	enum {
		R_BEGIN, R_GATHER, R_FTR, R_IDX, R_HDRSEEK, R_HDR,
		R_BLKHDR_SIZE, R_BLKHDR, R_DATA, R_SKIP_IDX, R_FTR_FIN, R_DONE,
	};

	for (;;) {
		switch (r->state) {

		case R_BEGIN:
			if (r->offset != 0) {
				r->gather_size = sizeof(struct xz_stmftr);
				r->state = R_GATHER;  r->state_next = R_FTR;
				return FFXZREAD_SEEK;
			}
			r->gather_size = sizeof(struct xz_stmhdr);
			r->state = R_GATHER;  r->state_next = R_HDR;
			// fallthrough

		case R_GATHER:
			rc = ffstr_gather((ffstr*)&r->buf, &r->buf.cap, input->ptr, input->len, r->gather_size, &data);
			if (rc < 0) {
				return FFXZREAD_ERROR;
			}
			ffstr_shift(input, rc);
			r->offset += rc;
			if (data.len == 0)
				return FFXZREAD_MORE;
			r->buf.len = 0;
			r->state = r->state_next;
			break;

		case R_FTR:
			if (0 > (rc = xz_stmftr_read(data.ptr, &r->error)))
				return FFXZREAD_ERROR;

			// if (ftr->flags != hdr->flags)
			// 	return ERR;

			r->idx_size = rc;
			r->gather_size = rc;
			r->state = R_GATHER;  r->state_next = R_IDX;
			r->offset = r->offset - data.len - rc;
			return FFXZREAD_SEEK;

		case R_IDX:
			if (0 > (rc = xz_idx_read(data.ptr, data.len, &r->error)))
				return FFXZREAD_ERROR;
			r->info.uncompressed_size = rc;
			r->state = R_HDRSEEK;
			return FFXZREAD_INFO;

		case R_HDRSEEK:
			r->gather_size = sizeof(struct xz_stmhdr);
			r->state = R_GATHER;  r->state_next = R_HDR;
			r->offset = 0;
			return FFXZREAD_SEEK;

		case R_HDR:
			if (0 > (rc = xz_stmhdr_read(data.ptr, &r->error)))
				return FFXZREAD_ERROR;
			r->check_method = rc;
			r->state = R_BLKHDR_SIZE;
			break;

		case R_BLKHDR_SIZE: {
			if (input->len == 0)
				return FFXZREAD_MORE;
			ffbyte blkhdr_size = *(ffbyte*)input->ptr;
			if (blkhdr_size == 0) {
				r->state = R_SKIP_IDX;
				break;
			}
			r->gather_size = (blkhdr_size + 1) * 4;
			r->state = R_GATHER;  r->state_next = R_BLKHDR;
			break;
		}

		case R_BLKHDR: {
			lzma_filter_props filts[4];
			if (0 > (rc = xz_blkhdr_read(data.ptr, data.len, filts, &r->error)))
				return FFXZREAD_ERROR;

			if (0 != (rc = lzma_decode_init(&r->lzma, r->check_method, filts, rc))) {
				r->error = lzma_errstr(rc);
				return FFXZREAD_ERROR;
			}

			r->state = R_DATA;
		}
		// fallthrough

		case R_DATA: {
			ffsize rd = input->len;
			rc = lzma_decode(r->lzma, input->ptr, &rd, (char*)r->buf.ptr, r->buf.cap);

			ffstr_shift(input, rd);
			r->offset += rd;
			r->info.compressed_size += rd;

			if (rc == 0) {
				return FFXZREAD_MORE;

			} else if (rc == LZMA_DONE) {
				r->state = R_BLKHDR_SIZE;
				break;

			} else if (rc < 0) {
				r->error = lzma_errstr(rc);
				return FFXZREAD_ERROR;
			}

			ffstr_set(output, r->buf.ptr, rc);
			return FFXZREAD_DATA;
		}

		case R_SKIP_IDX:
			if (r->idx_size == 0) {
				r->error = "multi-chunk .xz is not supported";
				return FFXZREAD_ERROR;
			}
			rc = ffmin(r->idx_size, input->len);
			ffstr_shift(input, rc);
			r->offset += rc;
			r->idx_size -= rc;
			if (r->idx_size != 0)
				return FFXZREAD_MORE;

			r->gather_size = sizeof(struct xz_stmftr);
			r->state = R_GATHER;  r->state_next = R_FTR_FIN;
			break;

		case R_FTR_FIN:
			if (0 > xz_stmftr_read(data.ptr, &r->error))
				return FFXZREAD_ERROR;
			r->state = R_DONE;
			// fallthrough

		case R_DONE:
			return FFXZREAD_DONE;

		default:
			FF_ASSERT(0);
			return FFXZREAD_ERROR;

		}
	}
}
