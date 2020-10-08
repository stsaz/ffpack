/** ffpack: .gz reader
2020, Simon Zolin */

/*
ffgzread_open
ffgzread_close
ffgzread_process
ffgzread_offset
ffgzread_error
ffgzread_getinfo
*/

#pragma once

#include <ffpack/gz-fmt.h>
#include <ffbase/vector.h>
#include <ffbase/string.h>
#include <zlib/zlib-ff.h>

typedef struct ffgzread_info {
	ffstr extra;
	ffstr name;
	ffuint mtime; // seconds since 1970
	ffstr comment; // must not contain '\0'

	ffuint uncompressed_crc;
	ffuint64 uncompressed_size; // not accurate if >4gb

	ffuint64 compressed_size; // how much compressed data we've read so far
} ffgzread_info;

typedef struct ffgzread {
	ffuint state, state_next;
	ffuint gather_size;
	const char *error;
	ffvec buf;
	ffuint64 offset;
	ffuint crc;
	ffuint hdr_flags;
	z_ctx *lz;
	ffgzread_info info;
} ffgzread;

/** Prepare for reading
.gz file may consist of multiple chunks:
 user must repeat the calls to open-process-close functions until all data in file is read,
 and set total_size=-1 to eliminate the attempt to seek to offset 0.
total_size: .gz file size (allows to determine uncompressed data size)
  -1: file size is unknown
Return 0 on success */
static int ffgzread_open(ffgzread *r, ffint64 total_size);

/** Close reader */
static void ffgzread_close(ffgzread *r);

enum FFGZREAD_R {
	FFGZREAD_MORE, // need more input data
	FFGZREAD_SEEK, // need input data at offset = ffgzread_offset()
	FFGZREAD_INFO, // user may call ffgzread_getinfo() to get info from header/trailer
	FFGZREAD_DATA, // have more decompressed data for user
	FFGZREAD_DONE,
	FFGZREAD_WARNING,
	FFGZREAD_ERROR,
};

/** Read the next chunk
output: pointer to an empty string for the output data
Return enum FFGZREAD_R */
static int ffgzread_process(ffgzread *r, ffstr *input, ffstr *output);

/** Get input offset */
static inline ffuint64 ffgzread_offset(ffgzread *r)
{
	return r->offset;
}

/** Get last error message */
static inline const char* ffgzread_error(ffgzread *r)
{
	return r->error;
}

/** Get info from header/trailer */
static inline ffgzread_info* ffgzread_getinfo(ffgzread *r)
{
	return &r->info;
}

static inline int ffgzread_open(ffgzread *r, ffint64 total_size)
{
	ffmem_zero_obj(r);
	if (total_size >= 0) {
		r->offset = total_size - sizeof(struct gz_trailer);
		if ((ffint64)r->offset <= 0) {
			r->error = "no gz trailer";
			return -1;
		}
	}

	if (NULL == ffvec_allocT(&r->buf, 64*1024, char)) {
		return -1;
	}

	return 0;
}

static inline void ffgzread_close(ffgzread *r)
{
	ffstr_free(&r->info.extra);
	ffstr_free(&r->info.name);
	ffstr_free(&r->info.comment);
	ffvec_free(&r->buf);
	if (r->lz != NULL) {
		z_inflate_free(r->lz);
		r->lz = NULL;
	}
}

/** Fast CRC32 implementation using 8k table */
FF_EXTERN ffuint crc32(const void *buf, ffsize size, ffuint crc);

/* .gz read:
. [seek to gz trailer; read it]
. [seek to gz header;] read it
. read extra data length
. read extra data
. read name
. read comment
. read header CRC
. decompress data
. read gz trailer
. check CRC, offset
*/
static inline int ffgzread_process(ffgzread *r, ffstr *input, ffstr *output)
{
	ffssize rc;
	ffstr data;
	enum {
		R_BEGIN, R_GATHER, R_GATHER_STRZ, R_TRL,
		R_HDR, R_HDR_FIELD, R_EXTRA_SIZE, R_EXTRA, R_NAME, R_COMMENT, R_HDRCRC,
		R_LZ_INIT, R_DATA, R_TRL_FIN,
	};
	for (;;) {
		switch (r->state) {

		case R_BEGIN:
			if (r->offset != 0) {
				r->gather_size = sizeof(struct gz_trailer);
				r->state = R_GATHER;  r->state_next = R_TRL;
				return FFGZREAD_SEEK;
			}
			r->gather_size = sizeof(struct gz_header);
			r->state = R_GATHER;  r->state_next = R_HDR;
			// fallthrough

		case R_GATHER:
			rc = ffstr_gather((ffstr*)&r->buf, &r->buf.cap, input->ptr, input->len, r->gather_size, &data);
			if (rc < 0) {
				return FFGZREAD_ERROR;
			}
			ffstr_shift(input, rc);
			r->offset += rc;
			if (data.len == 0)
				return FFGZREAD_MORE;
			r->buf.len = 0;
			r->state = r->state_next;
			break;

		case R_GATHER_STRZ: {
			rc = ffstr_findchar(input, '\0');
			int found = 1;
			if (rc < 0) {
				found = 0;
				rc = input->len;
			}
			ffvec_addT(&r->buf, input->ptr, rc, char);
			if (found)
				rc++;
			ffstr_shift(input, rc);
			r->offset += rc;
			if (!found)
				return FFGZREAD_MORE;

			ffstr_set2(&data, &r->buf);
			r->buf.len = 0;
			r->state = r->state_next;
			break;
		}

		case R_TRL: {
			ffuint uncompressed_size;
			gz_trailer_read(data.ptr, &r->info.uncompressed_crc, &uncompressed_size);
			r->info.uncompressed_size = (r->offset & 0xffffffff00000000ULL) | uncompressed_size;

			r->gather_size = sizeof(struct gz_header);
			r->state = R_GATHER;  r->state_next = R_HDR;
			r->offset = 0;
			return FFGZREAD_SEEK;
		}

		case R_HDR: {
			ffuint mtime;
			rc = gz_header_read((void*)data.ptr, &mtime);
			if (rc < 0) {
				r->error = "bad gz header";
				return FFGZREAD_ERROR;
			}
			r->hdr_flags = rc;
			r->info.mtime = mtime;

			r->state = R_HDR_FIELD;
			if (0 != (r->hdr_flags & ~GZ_F_ALL)) {
				r->error = "bad flags in gz header";
				return FFGZREAD_WARNING;
			}
		}
		// fallthrough

		case R_HDR_FIELD:
			if (r->hdr_flags & GZ_FEXTRA) {
				r->gather_size = 2;
				r->state = R_GATHER;  r->state_next = R_EXTRA_SIZE;
			} else if (r->hdr_flags & GZ_FNAME) {
				r->state = R_GATHER_STRZ;  r->state_next = R_NAME;
			} else if (r->hdr_flags & GZ_FCOMMENT) {
				r->state = R_GATHER_STRZ;  r->state_next = R_COMMENT;
			} else if (r->hdr_flags & GZ_FHDRCRC) {
				r->gather_size = 2;
				r->state = R_GATHER;  r->state_next = R_HDRCRC;
			} else {
				r->state = R_LZ_INIT;
				return FFGZREAD_INFO;
			}
			break;

		case R_EXTRA_SIZE: {
			ffuint extra_len = ffint_le_cpu16(*(short*)data.ptr);
			r->gather_size = extra_len;
			r->state = R_GATHER;  r->state_next = R_EXTRA;
			break;
		}

		case R_EXTRA:
			if (NULL == ffstr_dup2(&r->info.extra, &data)) {
				return FFGZREAD_ERROR;
			}
			r->hdr_flags &= ~GZ_FEXTRA;
			r->state = R_HDR_FIELD;
			break;

		case R_NAME:
			if (NULL == ffstr_dup2(&r->info.name, &data)) {
				return FFGZREAD_ERROR;
			}
			r->hdr_flags &= ~GZ_FNAME;
			r->state = R_HDR_FIELD;
			break;

		case R_COMMENT:
			if (NULL == ffstr_dup2(&r->info.comment, &data)) {
				return FFGZREAD_ERROR;
			}
			r->hdr_flags &= ~GZ_FCOMMENT;
			r->state = R_HDR_FIELD;
			break;

		case R_HDRCRC:
			r->hdr_flags &= ~GZ_FHDRCRC;
			r->state = R_HDR_FIELD;
			break;

		case R_LZ_INIT: {
			z_conf zconf = {};
			if (0 != z_inflate_init(&r->lz, &zconf)) {
				r->error = "z_inflate_init()";
				return FFGZREAD_ERROR;
			}
			r->state = R_DATA;
		}
		// fallthrough

		case R_DATA: {
			ffsize rd = input->len;
			rc = z_inflate(r->lz, input->ptr, &rd, (char*)r->buf.ptr, r->buf.cap, 0);

			ffstr_shift(input, rd);
			r->offset += rd;
			r->info.compressed_size += rd;

			if (rc == 0) {
				return FFGZREAD_MORE;

			} else if (rc == Z_DONE) {
				r->gather_size = sizeof(struct gz_trailer);
				r->state = R_GATHER;  r->state_next = R_TRL_FIN;
				break;

			} else if (rc < 0) {
				r->error = "z_inflate()";
				return FFGZREAD_ERROR;
			}

			r->crc = crc32((void*)r->buf.ptr, rc, r->crc);

			r->buf.len += rc;
			ffstr_set2(output, &r->buf);
			r->buf.len = 0;
			return FFGZREAD_DATA;
		}

		case R_TRL_FIN: {
			ffuint uncompressed_size;
			gz_trailer_read(data.ptr, &r->info.uncompressed_crc, &uncompressed_size);

			if (r->crc != r->info.uncompressed_crc) {
				r->error = "computed CRC doesn't match CRC from trailer";
				return FFGZREAD_WARNING;
			}
			return FFGZREAD_DONE;
		}

		default:
			FF_ASSERT(0);
			return FFGZREAD_ERROR;
		}
	}
}
