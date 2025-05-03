/** ffpack: .zip reader
* compression: stored, deflated, zstandard
* CRC check
* doesn't support encryption, multi-disk archives

Building:
Define FFPACK_ZIPREAD_ZLIB, FFPACK_ZIPREAD_ZSTD
 to use zlib/zstd third-party code referenced by ffpack.

2020, Simon Zolin */

/*
ffzipread_open
ffzipread_close
ffzipread_process
ffzipread_offset
ffzipread_error
ffzipread_fileinfo
ffzipread_fileread
*/

#pragma once

#include <ffpack/path.h>
#include <ffpack/base/zip.h>
#include <ffbase/vector.h>
#include <ffbase/string.h>

struct z_ctx;
struct zstd_decoder;
typedef struct ffzipread ffzipread;
typedef struct zip_fileinfo ffzipread_fileinfo_t;
typedef void (*ffzipread_log)(void *udata, ffuint level, ffstr msg);
typedef int (*_ffzipread_unpack)(ffzipread *z, ffstr input, ffstr *output, ffsize *rd);

struct ffzipread {
	ffuint state, state_next;
	ffuint gather_size;
	const char *error;
	ffvec buf;
	ffuint64 offset; // current offset
	ffuint64 cdir_end; // offset where CDIR ends
	ffuint64 file_rd, file_wr;
	ffuint crc; // current CRC

#ifdef FFPACK_ZIPREAD_ZLIB
	struct z_ctx *lz;
#endif

#ifdef FFPACK_ZIPREAD_ZSTD
	struct zstd_decoder *zstd;
#endif

	ffzipread_fileinfo_t fileinfo;
	_ffzipread_unpack unpack_func;
	ffuint64 file_comp_size;
	char *error_buf;
	ffuint have_ftrl :1;
	ffuint zip64_ftrl :1;

	/* Code page for non-Unicode file names. enum FFUNICODE_CP
	default: FFUNICODE_WIN1252 */
	ffuint codepage;

	ffzipread_log log;
	void *udata;

	/* Offset in seconds for the current local time (GMT+XX) */
	int timezone_offset;
};

/** Prepare for reading
total_size: .zip file size
Return 0 on success */
static int ffzipread_open(ffzipread *z, ffint64 total_size);

/** Close reader */
static void ffzipread_close(ffzipread *z);

enum FFZIPREAD_R {
	/* Need more input data
	Expecting ffzipread_process() with more data */
	FFZIPREAD_MORE,

	/* Need input data at absolute file offset = ffzipread_offset()
	Expecting ffzipread_process() with more data at the specified offset */
	FFZIPREAD_SEEK,

	/* File CDIR info is ready - user may call ffzipread_fileinfo()
	Expecting ffzipread_process() or ffzipread_fileread() */
	FFZIPREAD_FILEINFO,

	/* Finished reading file data
	Expecting ffzipread_fileread() */
	FFZIPREAD_FILEDONE,

	/* Short file info from file header is ready - user may call ffzipread_fileinfo()
	File size, CRC may not be available.
	Attributes are not available.
	Expecting ffzipread_process() */
	FFZIPREAD_FILEHEADER,

	/* Have more output data for user
	Expecting ffzipread_process() */
	FFZIPREAD_DATA,

	/* Finished reading meta data
	Expecting ffzipread_fileread() */
	FFZIPREAD_DONE,

	FFZIPREAD_WARNING,

	/* Fatal error */
	FFZIPREAD_ERROR,
};

/** Read the next chunk
output: pointer to an empty string for the output data
Return enum FFZIPREAD_R */
static int ffzipread_process(ffzipread *z, ffstr *input, ffstr *output);

/** Get input offset */
static inline ffuint64 ffzipread_offset(ffzipread *z)
{
	return z->offset;
}

/** Get last error message */
static inline const char* ffzipread_error(ffzipread *z)
{
	return z->error;
}

/** Get info from CDIR entry */
static inline ffzipread_fileinfo_t* ffzipread_fileinfo(ffzipread *z)
{
	return &z->fileinfo;
}

/** Prepare for reading a file
hdr_offset: file header offset from CDIR
comp_size: compressed (on-disk) file size from CDIR */
static inline void ffzipread_fileread(ffzipread *z, ffuint64 hdr_offset, ffuint64 comp_size)
{
	z->state = 20;
	z->offset = hdr_offset;
	z->file_comp_size = comp_size;
}


#ifdef FFPACK_ZIPREAD_ZLIB
	#include <ffpack/zip-read-libz.h>
#endif
#ifdef FFPACK_ZIPREAD_ZSTD
	#include <ffpack/zip-read-zstd.h>
#endif


static inline int ffzipread_open(ffzipread *z, ffint64 total_size)
{
	ffmem_zero_obj(z);
	if (NULL == ffvec_allocT(&z->buf, 64*1024, char)) {
		return -1;
	}
	z->codepage = FFUNICODE_WIN1252;
	z->offset = total_size;
	return 0;
}

static inline void ffzipread_close(ffzipread *z)
{
	ffvec_free(&z->buf);
	ffstr_free(&z->fileinfo.name);

#ifdef FFPACK_ZIPREAD_ZLIB
	_ffzipr_deflated_close(z);
#endif

#ifdef FFPACK_ZIPREAD_ZSTD
	_ffzipr_zstd_close(z);
#endif

	ffmem_free(z->error_buf);  z->error_buf = NULL;
}

static inline void _ffzipread_log(ffzipread *z, ffuint level, const char *fmt, ...)
{
	(void)level;
	if (z->log == NULL)
		return;

	ffstr s = {};
	ffsize cap = 0;
	va_list va;
	va_start(va, fmt);
	ffstr_growfmtv(&s, &cap, fmt, va);
	va_end(va);

	z->log(z->udata, 0, s);
	ffstr_free(&s);
}

/** Copy file name (UTF-8), normalize */
static inline int _ffzipread_fn_copy(ffzipread *z, ffstr fn)
{
	ffstr_free(&z->fileinfo.name);
	if (ffutf8_valid(fn.ptr, fn.len)) {
		if (NULL == ffstr_dup(&z->fileinfo.name, fn.ptr, fn.len))
			return -1;
	} else {
		ffsize cap = 0;
		if (0 == ffstr_growadd_codepage(&z->fileinfo.name, &cap, fn.ptr, fn.len, z->codepage))
			return -1;
	}

	z->fileinfo.name.len = _ffpack_path_normalize(z->fileinfo.name.ptr, z->fileinfo.name.len
		, z->fileinfo.name.ptr, z->fileinfo.name.len
		, _FFPACK_PATH_FORCE_SLASH | _FFPACK_PATH_SIMPLE);
	return 0;
}

/** Process CDIR entry's extra data */
static inline int _ffzipread_extra(ffzipread *z, const void *cdir_data, const void *fhdr_data, ffstr extra)
{
	z->zip64_ftrl = 0;

	while (extra.len != 0) {
		ffstr val;
		int id = zip_extra_next(&extra, &val);
		if (id < 0)
			break;

		_ffzipread_log(z, 0, "CDIR extra: %xu [%L] %*Xb"
			, id, val.len, val.len, val.ptr);

		switch (id) {
		case 0x0001:
			if (cdir_data != NULL)
				(void) zip_extra_cdir64_read(val, cdir_data, &z->fileinfo);
			else {
				zip_extra_fhdr64_read(val, fhdr_data, &z->fileinfo);
				z->zip64_ftrl = 1;
			}
			break;

		case 0x000A:
			(void) zip_extra_ntfs_read(val, &z->fileinfo);
			break;

		case 0x5455: // "UT"
			(void) zip_extra_unixtime_read(val, &z->fileinfo);
			break;

		case 0x7875: // "ux"
			(void) zip_extra_newunix_read(val, &z->fileinfo);
			break;
		}
	}
	return 0;
}

/** Fast CRC32 implementation using 8k table */
FF_EXTERN ffuint crc32(const void *buf, ffsize size, ffuint crc);


static inline int _ffzipread_stored_unpack(ffzipread *z, ffstr input, ffstr *output, ffsize *rd)
{
	(void)z;
	if (input.len == 0) {
		if (*rd == 0)
			return 0xa11;
		*rd = 0;
		return 0xfeed;
	}
	*rd = input.len;
	ffstr_set(output, input.ptr, *rd);
	return 0;
}


/* .zip read:
. find CDIR trailer at file end, get CDIR offset
. if CDIR offset is -1:
  . read zip64 CDIR locator, get zip64 CDIR trailer offset
  . read zip64 CDIR trailer, get CDIR offset
. read entries from CDIR
. return FFZIPREAD_DONE

. after ffzipread_fileread() has been called by user, seek to local file header
. read file header
. decompress data
. [read file trailer]
. perform CRC check
*/
static inline int ffzipread_process(ffzipread *z, ffstr *input, ffstr *output)
{
	ffssize r;
	ffstr data = {};
	enum {
		R_CDIR_TRL_SEEK, R_CDIR_TRL, R_CDIR64_LOC, R_CDIR64, R_CDIR_NEXT, R_CDIR, R_CDIR_DATA,
		R_FHDR_SEEK = 20, R_FHDR, R_FHDR_DATA, R_DATA, R_FTRL, R_FTRL64, R_FILEDONE, R_FILEDONE2, R_DONE,
		R_GATHER, R_GATHER_MORE,
	};

	for (;;) {
		switch (z->state) {

		case R_CDIR_TRL_SEEK: {
			ffuint64 total_size = z->offset;
			z->gather_size = ffmin64(ZIP_CDIR_TRL_MAXSIZE, total_size);
			z->offset = total_size - z->gather_size;
			z->state = R_GATHER;  z->state_next = R_CDIR_TRL;
			return FFZIPREAD_SEEK;
		}

		case R_GATHER_MORE:
			if (z->buf.ptr == data.ptr)
				z->buf.len = data.len;
			else
				ffvec_add2T(&z->buf, &data, char);
			z->state = R_GATHER;
			// fallthrough

		case R_GATHER:
			r = ffstr_gather((ffstr*)&z->buf, &z->buf.cap, input->ptr, input->len, z->gather_size, &data);
			if (r < 0) {
				return FFZIPREAD_ERROR;
			}
			ffstr_shift(input, r);
			z->offset += r;
			if (data.len == 0)
				return FFZIPREAD_MORE;
			z->buf.len = 0;
			z->state = z->state_next;
			break;

		case R_CDIR_TRL: {
			ffint64 cdirtrl_off = zip_cdirtrl_find(data.ptr, data.len);
			if (cdirtrl_off < 0) {
				z->error = "no CDIR trailer";
				return FFZIPREAD_ERROR;
			}
			ffstr_shift(&data, cdirtrl_off);

			ffuint disknum, cdir_disk, cdir_size, cdir_offset;
			r = zip_cdirtrl_read(data.ptr, data.len, &disknum, &cdir_disk, &cdir_size, &cdir_offset);
			if (r < 0) {
				z->error = "zip_cdirtrl_read";
				return FFZIPREAD_ERROR;
			}
			if (disknum != 0 || cdir_disk != 0) {
				z->error = "multi-disk archives are not supported";
				return FFZIPREAD_ERROR;
			}

			if (cdir_offset == (ffuint)-1) {
				z->offset -= data.len;
				z->offset -= sizeof(struct zip64_cdirtrl_loc);
				z->gather_size = sizeof(struct zip64_cdirtrl_loc);
				z->state = R_GATHER;  z->state_next = R_CDIR64_LOC;
				return FFZIPREAD_SEEK;
			}

			z->cdir_end = cdir_offset + cdir_size;
			z->offset = cdir_offset;
			z->state = R_CDIR_NEXT;
			return FFZIPREAD_SEEK;
		}

		case R_CDIR64_LOC: {
			ffuint cdirtrl_disk, disks_number;
			ffuint64 off;
			if (0 != zip_cdirtrl64_loc_read(data.ptr, &cdirtrl_disk, &disks_number, &off)) {
				z->error = "zip_cdirtrl64_loc_read";
				return FFZIPREAD_ERROR;
			}
			if (cdirtrl_disk != 0 || disks_number != 1) {
				z->error = "multi-disk archives are not supported";
				return FFZIPREAD_ERROR;
			}

			z->offset = off;
			z->gather_size = sizeof(struct zip64_cdirtrl);
			z->state = R_GATHER;  z->state_next = R_CDIR64;
			return FFZIPREAD_SEEK;
		}

		case R_CDIR64: {
			ffuint size, disk, cdir_disk;
			ffuint64 cdir_size, cdir_offset;
			r = zip_cdirtrl64_read(data.ptr, &size, &disk, &cdir_disk, &cdir_size, &cdir_offset);
			if (r < 0) {
				z->error = "zip_cdirtrl64_read";
				return FFZIPREAD_ERROR;
			}
			if (disk != 0 || cdir_disk != 0) {
				z->error = "multi-disk archives are not supported";
				return FFZIPREAD_ERROR;
			}

			z->cdir_end = cdir_offset + cdir_size;
			z->offset = cdir_offset;
			z->state = R_CDIR_NEXT;
			return FFZIPREAD_SEEK;
		}

		case R_CDIR_NEXT:
			if (z->offset + sizeof(struct zip_cdir) > z->cdir_end)
				return FFZIPREAD_DONE;

			z->gather_size = sizeof(struct zip_cdir);
			z->state = R_GATHER;  z->state_next = R_CDIR;
			break;

		case R_CDIR:
			r = zip_cdir_read(data.ptr, &z->fileinfo, z->timezone_offset);
			if (r < 0) {
				z->error = "zip_cdir_read";
				return FFZIPREAD_ERROR;
			}
			z->gather_size = r;
			z->state = R_GATHER_MORE;  z->state_next = R_CDIR_DATA;
			break;

		case R_CDIR_DATA: {
			const struct zip_cdir *cdir = (struct zip_cdir*)data.ptr;
			ffuint filenamelen = ffint_le_cpu16_ptr(cdir->filenamelen);
			ffstr fn;
			ffstr_set(&fn, cdir->filename, filenamelen);

			if (0 != _ffzipread_fn_copy(z, fn)) {
				return FFZIPREAD_ERROR;
			}

			ffstr_shift(&data, sizeof(struct zip_cdir) + filenamelen);
			if (0 != _ffzipread_extra(z, cdir, NULL, data)) {
				return FFZIPREAD_ERROR;
			}

			z->state = R_CDIR_NEXT;
			return FFZIPREAD_FILEINFO;
		}

		case R_FHDR_SEEK:
			z->gather_size = sizeof(struct zip_filehdr);
			z->state = R_GATHER;  z->state_next = R_FHDR;
			return FFZIPREAD_SEEK;

		case R_FHDR: {
			ffstr_free(&z->fileinfo.name);
			ffmem_zero_obj(&z->fileinfo);
			r = zip_filehdr_read(data.ptr, &z->fileinfo, z->timezone_offset);
			if (r < 0) {
				z->error = "zip_filehdr_read";
				return FFZIPREAD_ERROR;
			}

			switch (z->fileinfo.compress_method) {
			case ZIP_STORED:
				z->unpack_func = _ffzipread_stored_unpack;
				break;

#ifdef FFPACK_ZIPREAD_ZLIB
			case ZIP_DEFLATED:
				if (0 != _ffzipread_deflated_init(z)) {
					return FFZIPREAD_ERROR;
				}
				break;
#endif

#ifdef FFPACK_ZIPREAD_ZSTD
			case ZIP_ZSTANDARD:
				if (0 != _ffzipr_zstd_init(z)) {
					return FFZIPREAD_ERROR;
				}
				break;
#endif

			default:
				z->error = "unsupported compression method";
				return FFZIPREAD_ERROR;
			}

			const struct zip_filehdr *h = (struct zip_filehdr*)data.ptr;
			z->have_ftrl = !!(h->flags[0] & ZIP_FDATADESC);

			z->crc = 0;
			z->file_rd = 0;
			z->file_wr = 0;
			z->gather_size = r;
			z->state = R_GATHER_MORE;  z->state_next = R_FHDR_DATA;
			break;
		}

		case R_FHDR_DATA: {
			const struct zip_filehdr *h = (struct zip_filehdr*)data.ptr;
			ffuint filenamelen = ffint_le_cpu16_ptr(h->filenamelen);
			ffstr fn;
			ffstr_set(&fn, h->filename, filenamelen);
			if (0 != _ffzipread_fn_copy(z, fn)) {
				return FFZIPREAD_ERROR;
			}

			ffstr_shift(&data, sizeof(struct zip_filehdr) + filenamelen);
			if (0 != _ffzipread_extra(z, NULL, h, data)) {
				return FFZIPREAD_ERROR;
			}

			z->state = R_DATA;
			return FFZIPREAD_FILEHEADER;
		}

		case R_DATA: {
			ffstr in;
			ffstr_set(&in, input->ptr, ffmin(input->len, z->file_comp_size - z->file_rd));
			ffsize rd = z->file_comp_size - z->file_rd;
			r = z->unpack_func(z, in, output, &rd);
			ffstr_shift(input, rd);
			z->offset += rd;
			z->file_rd += rd;

			switch (r) {
			case 0xfeed:
				if (z->file_rd == z->file_comp_size)
					return z->error = "reached the end of file data",  FFZIPREAD_ERROR;
				return FFZIPREAD_MORE;

			case 0xa11:
				if (z->file_rd != z->file_comp_size)
					return z->error = "unprocessed file data",  FFZIPREAD_ERROR;

				z->state = R_FILEDONE;
				if (z->have_ftrl) {
					z->gather_size = 4 + sizeof(struct zip_filetrl);
					z->state = R_GATHER;  z->state_next = R_FTRL;
					if (z->zip64_ftrl) {
						z->gather_size = 4 + sizeof(struct zip64_filetrl);
						z->state = R_GATHER;  z->state_next = R_FTRL64;
					}
				}
				continue;

			case 0:
				break;

			case 0xbad:
				return FFZIPREAD_ERROR;
			}

			z->crc = crc32((void*)output->ptr, output->len, z->crc);
			z->file_wr += output->len;
			return FFZIPREAD_DATA;
		}

		case R_FTRL:
			zip_filetrl_read(data.ptr, &z->fileinfo);
			z->state = R_FILEDONE;
			break;

		case R_FTRL64:
			zip_filetrl64_read(data.ptr, &z->fileinfo);
			z->state = R_FILEDONE;
			break;

		case R_FILEDONE:
			if (z->crc != z->fileinfo.uncompressed_crc) {
				z->error = "computed CRC doesn't match CRC from header";
				z->state = R_FILEDONE2;
				return FFZIPREAD_WARNING;
			}
			// fallthrough
		case R_FILEDONE2:
			z->state = R_DONE;
			return FFZIPREAD_FILEDONE;

		case R_DONE:
			z->error = "nothing to do";
			return FFZIPREAD_ERROR;

		default:
			FF_ASSERT(0);
			return FFZIPREAD_ERROR;
		}
	}
}
