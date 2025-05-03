/** ffpack: .7z format
2017,2021, Simon Zolin
*/

/*
z7_ghdr_read
z7_varint
z7_find_block
z7_check_req
*/

/* .7z format:
SIG_HDR
STREAM_PACKED(FILE_DATA...)...
META_PACKED(META META_HDR) | META

Meta data structure:
STREAM { offset packed_size } []
FOLDER {
	CODER { id properties stream input_coders[] } []
} []
CODER { unpacked_size } []
FOLDER {
	datafile_unpacked_size[]
	datafile_crc[]
} []
empty_file_index[]
file_name[]
file_mtime[]
file_attr[]
*/

#pragma once

#include <ffpack/path.h>
#include <ffbase/time.h>
#include <ffbase/vector.h>
#include <ffbase/stringz.h>
#include <ffbase/unicode.h>

struct ff7zread;
static struct ff7zread *_ff7z_log_param;
static void _ff7zread_log(struct ff7zread *z, ffuint level, const char *fmt, ...);

enum Z7_E {
	Z7_EOK,
	Z7_ESYS,
	Z7_EHDRSIGN,
	Z7_EHDRVER,
	Z7_EHDRCRC,
	Z7_EBADID,
	Z7_EUKNID,
	Z7_EDUPBLOCK,
	Z7_ENOREQ,
	Z7_EUNSUPP,
	Z7_EUKNCODER,
	Z7_EFOLDER_FLAGS,
	Z7_EMORE,
	Z7_EORDER,
	Z7_EDATA,
	Z7_EDATACRC,
	Z7_ELZMA,
	Z7_EZLIB,
};

enum Z7_METHOD {
	Z7_M_UKN,
	Z7_M_STORE,
	Z7_M_LZMA1,
	Z7_M_X86,
	Z7_M_X86_BCJ2,
	Z7_M_DEFLATE,
	Z7_M_LZMA2,
};

struct z7_fileinfo {
	ffstr name;
	fftime mtime;
	ffuint attr; // enum FFFILE_WIN_FILEATTR
	ffuint crc;
	ffuint64 off;
	ffuint64 size;
};

#define Z7_MAX_BLOCK_DEPTH  (4+1)
#define Z7_MAX_CODERS  4

struct z7_info {
	ffuint64 hdr_off;
	ffuint64 hdr_size;
	ffuint hdr_crc;
};

enum Z7_T {
	Z7_T_End = 0x00,
	Z7_T_Header = 0x01,
	Z7_T_AdditionalStreamsInfo = 0x03,
	Z7_T_MainStreamsInfo = 0x04,
	Z7_T_FilesInfo = 0x05,
	Z7_T_PackInfo = 0x06,
	Z7_T_UnPackInfo = 0x07,
	Z7_T_SubStreamsInfo = 0x08,
	Z7_T_Size = 0x09,
	Z7_T_CRC = 0x0A,
	Z7_T_Folder = 0x0B,
	Z7_T_UnPackSize = 0x0C,
	Z7_T_NumUnPackStream = 0x0D,
	Z7_T_EmptyStream = 0x0E,
	Z7_T_EmptyFile = 0x0F,
	Z7_T_Name = 0x11,
	Z7_T_MTime = 0x14,
	Z7_T_WinAttributes = 0x15,
	Z7_T_EncodedHeader = 0x17,
	Z7_T_Dummy = 0x19,
};

enum Z7_F {
	Z7_F_CHILDREN = 0x0100, // data is 'z7_binblock[]'
	Z7_F_REQ = 0x0200,
	Z7_F_LAST = 0x0400,
	Z7_F_SIZE = 0x0800, // read block size (varint) before block body
	Z7_F_SELF = 0x1000,
	Z7_F_MULTI = 0x2000, // allow multiple occurrences
};

#define Z7_PRIO(n)  (n) << 24
#define Z7_GET_PRIO(f) ((f) >> 24) & 0xff

struct z7_binblock {
	ffuint flags;
	const void *data;
};

struct z7_block {
	ffuint id;
	ffuint used;
	ffuint prio;
	const struct z7_binblock *children;
};

struct z7_stream {
	ffuint64 off;
	ffuint64 pack_size;
};

struct z7_coder {
	ffuint method;
	ffuint nprops;
	ffbyte props[8];
	struct z7_stream stream;
	ffbyte input_coders[Z7_MAX_CODERS];
	ffuint64 unpack_size;
};

struct z7_folder {
	ffuint coders;
	ffuint crc;
	struct z7_coder coder[Z7_MAX_CODERS];
	ffvec files; // z7_fileinfo[]
	ffuint64 ifile;
	ffuint64 unpack_size;

	ffvec empty; // bit-array of total files in archive. Valid for the last stream that contains empty files.
};

struct z7_ghdr {
	char sig[6];
	ffbyte ver_major;
	ffbyte unused;
	ffbyte crc[4]; // CRC of the following bytes
	ffbyte hdr_off[8]; // hdr = struct z7_ghdr + hdr_off
	ffbyte hdr_size[8];
	ffbyte hdr_crc[4];
};

/** Fast CRC32 implementation using 8k table */
FF_EXTERN ffuint crc32(const void *buf, ffsize size, ffuint crc);

/** Read global header */
static inline int z7_ghdr_read(struct z7_info *info, const char *data)
{
	const struct z7_ghdr *h = (struct z7_ghdr*)data;

	static const ffbyte hdr_signature[] = {'7', 'z', 0xbc, 0xaf, 0x27, 0x1c};
	if (ffmem_cmp(h->sig, hdr_signature, 6))
		return Z7_EHDRSIGN;

	if (h->ver_major != 0)
		return Z7_EHDRVER;

	ffuint crc = crc32((void*)h->hdr_off, sizeof(struct z7_ghdr) - FF_OFF(struct z7_ghdr, hdr_off), 0);
	if (ffint_le_cpu32_ptr(h->crc) != crc)
		return Z7_EHDRCRC;

	info->hdr_off = ffint_le_cpu64_ptr(h->hdr_off) + sizeof(struct z7_ghdr);
	info->hdr_size = ffint_le_cpu64_ptr(h->hdr_size);
	info->hdr_crc = ffint_le_cpu32_ptr(h->hdr_crc);
	_ff7zread_log(_ff7z_log_param, 0, "hdr-off:%xU  size:%xU", info->hdr_off, info->hdr_size);
	return 0;
}

/** Read varint */
/*
HI1 [LO8..HI2] -> LO8..HI1 (LE) -> host int
0xxxxxxx +0 bytes
10xxxxxx +1 bytes
...
11111111 +8 bytes
*/
static inline int z7_varint(const char *data, ffsize len, ffuint64 *val)
{
	if (len == 0)
		return 0;

	ffuint size = ffbit_find32(~((ffuint)(ffbyte)data[0] << 24) & 0xff000000);
	if (size == 0)
		size = 8;
	if (size > len)
		return 0;

	/* Get bit mask by number (e.g. 16 -> 0xffff) */
	ffuint bit_mask = ((1U << (8 - size)) - 1);

	ffbyte b[8];
	ffmem_zero(b, sizeof(b));
	b[size - 1] = (ffbyte)data[0] & bit_mask;
	ffmem_copy(b, data + 1, size - 1);

	*val = ffint_le_cpu64_ptr(b);
	return size;
}

/** Read byte and shift input */
static int z7_readbyte(ffstr *d, ffuint *val)
{
	if (d->len == 0)
		return -1;
	*val = (ffbyte)d->ptr[0];
	ffstr_shift(d, 1);
	return 0;
}

/** Read varint and shift input */
static int z7_readint(ffstr *d, ffuint64 *val)
{
	int r = z7_varint(d->ptr, d->len, val);
	ffstr_shift(d, r);
	return r;
}

/** Search block by ID */
static inline int z7_find_block(ffuint blk_id, const struct z7_binblock **pblock, struct z7_block *parent)
{
	ffuint i;
	const struct z7_binblock *blk;
	for (i = 0;  ;  i++) {

		blk = &parent->children[i];
		ffuint id = blk->flags & 0xff;

		if (id == blk_id)
			break;

		if (blk->flags & Z7_F_SELF)
			continue;

		if (blk->flags & Z7_F_LAST)
			return Z7_EUKNID;
	}

	if (ffbit_set32(&parent->used, i) && !(blk->flags & Z7_F_MULTI))
		return Z7_EDUPBLOCK;

	ffuint prio = Z7_GET_PRIO(blk->flags);
	if (prio != 0) {
		if (prio > parent->prio + 1)
			return Z7_EORDER;
		parent->prio = prio;
	}

	*pblock = blk;
	return 0;
}

/** Ensure the required blocks were processed */
static inline int z7_check_req(const struct z7_block *ctx)
{
	for (ffuint i = 0;  ;  i++) {
		if ((ctx->children[i].flags & Z7_F_REQ) && !ffbit_test32(&ctx->used, i))
			return Z7_ENOREQ;
		if (ctx->children[i].flags & Z7_F_LAST)
			break;
	}
	return 0;
}


/*
varint PackPos
varint NumPackStreams
*/
static int z7_packinfo_read(ffvec *stms, ffstr *d)
{
	int r;
	ffuint64 off, n;

	if (0 == (r = z7_readint(d, &off)))
		return Z7_EMORE;
	off += sizeof(struct z7_ghdr);

	if (0 == (r = z7_readint(d, &n)))
		return Z7_EMORE;

	_ff7zread_log(_ff7z_log_param, 0, "streams:%U  offset:%xU", n, off);

	if (n == 0)
		return Z7_EUNSUPP;

	if (NULL == ffvec_zallocT(stms, n, struct z7_stream))
		return Z7_ESYS;
	stms->len = n;
	struct z7_stream *s = (struct z7_stream*)stms->ptr;
	s->off = off;
	return 0;
}

/*
varint PackSize[NumPackStreams]
*/
static int z7_packsizes_read(ffvec *stms, ffstr *d)
{
	int r;
	struct z7_stream *s = (struct z7_stream*)stms->ptr;
	ffuint64 n, off = s->off;

	for (ffsize i = 0;  i != stms->len;  i++) {

		if (0 == (r = z7_readint(d, &n)))
			return Z7_EMORE;

		_ff7zread_log(_ff7z_log_param, 0, " stream#%L size:%xU", i, n);
		s[i].off = off;
		s[i].pack_size = n;
		off += n;
	}
	return 0;
}

enum FOLDER_F {
	/** BCJ2 decoder has 4 input streams, 3 of them must be decompressed separately:
	stream0 -> lzma -\
	stream1 -> lzma -\
	stream2 -> lzma -\
	stream3         -> bcj2
	*/
	FOLDER_F_COMPLEXCODER = 0x10,
	FOLDER_F_ATTRS = 0x20,
};

static const char z7_method[][4] = {
	{0x00}, // Z7_M_STORE
	{0x03,0x01,0x01}, // Z7_M_LZMA1
	{0x03,0x03,0x01,0x03}, // Z7_M_X86
	{0x03,0x03,0x01,0x1b}, // Z7_M_X86_BCJ2
	{0x04,0x01,0x08}, // Z7_M_DEFLATE
	{0x21}, // Z7_M_LZMA2
};

/** Read data for 1 folder */
static int z7_folder_read(struct z7_folder *fo, ffstr *d, ffslice *stms)
{
	int r;
	ffuint64 n, coders_n, in_streams;

	if (0 == z7_readint(d, &coders_n))
		return Z7_EMORE;
	_ff7zread_log(_ff7z_log_param, 0, " coders:%U", coders_n);
	if (coders_n - 1 > Z7_MAX_CODERS)
		return Z7_EDATA;

	in_streams = coders_n;
	fo->coders = coders_n;

	for (ffuint i = 0;  i != coders_n;  i++) {

		struct z7_coder *cod = &fo->coder[i];
		ffuint flags;
		if (0 != z7_readbyte(d, &flags))
			return Z7_EMORE;

		ffuint methlen = flags & 0x0f;
		flags &= 0xf0;
		if (d->len < methlen)
			return Z7_EMORE;
		_ff7zread_log(_ff7z_log_param, 0, "  coder:%*xb  flags:%xu", (ffsize)methlen, d->ptr, flags);
		r = ffcharr_findsorted(z7_method, FF_COUNT(z7_method), sizeof(*z7_method), d->ptr, methlen);
		if (r >= 0)
			cod->method = r + 1;
		ffstr_shift(d, methlen);

		if (flags & FOLDER_F_COMPLEXCODER) {
			ffuint64 in;
			if (0 == z7_readint(d, &in))
				return Z7_EMORE;
			if (0 == z7_readint(d, &n))
				return Z7_EMORE;
			_ff7zread_log(_ff7z_log_param, 0, "   complex-coder: in:%U  out:%U", in, n);
			if (in - 1 > Z7_MAX_CODERS || n != 1)
				return Z7_EDATA;
			flags &= ~FOLDER_F_COMPLEXCODER;
			in_streams += in - 1;
		}

		if (flags & FOLDER_F_ATTRS) {
			if (0 == z7_readint(d, &n))
				return Z7_EMORE;
			if (d->len < n)
				return Z7_EMORE;
			_ff7zread_log(_ff7z_log_param, 0, "   props:%*xb", (ffsize)n, d->ptr);
			if (n >= FF_COUNT(cod->props))
				return Z7_EUNSUPP;
			ffmem_copy(cod->props, d->ptr, n);
			cod->nprops = n;
			ffstr_shift(d, n);
			flags &= ~FOLDER_F_ATTRS;
		}

		if (flags != 0)
			return Z7_EFOLDER_FLAGS;
	}

	ffuint bonds = coders_n - 1;
	for (ffuint i = 0;  i != bonds;  i++) {
		ffuint64 in, out;
		if (0 == z7_readint(d, &in))
			return Z7_EMORE;
		if (0 == z7_readint(d, &out))
			return Z7_EMORE;
		_ff7zread_log(_ff7z_log_param, 0, "  bond: in:%U  out:%U", in, out);
		fo->coder[coders_n - 1].input_coders[i] = i + 1;
	}

	ffuint pack_streams = in_streams - bonds;
	if (pack_streams > stms->len)
		return Z7_EDATA;
	if (pack_streams != 1) {
		for (ffuint i = 0;  i != pack_streams;  i++) {
			if (0 == z7_readint(d, &n))
				return Z7_EMORE;
			_ff7zread_log(_ff7z_log_param, 0, "  pack-stream:%U", n);
		}
	}

	struct z7_stream *stm = (struct z7_stream*)stms->ptr;
	for (ffuint i = 0;  i != pack_streams;  i++) {
		fo->coder[i].stream.off = stm->off;
		fo->coder[i].stream.pack_size = stm->pack_size;
		stm++;
	}
	ffslice_set(stms, stm, stms->len - pack_streams);
	return 0;
}

/*
varint NumFolders
byte External
 0:
  {
   // set NumInStreams = 0
   varint NumCoders
   {
    byte CodecIdSize :4
    byte Flags :4 //enum FOLDER_F
    byte CodecId[CodecIdSize]

    FOLDER_F_COMPLEXCODER:
     varint +=NumInStreams;
     varint NumOutStreams; //=1
    else
     ++NumInStreams

    FOLDER_F_ATTRS:
     varint PropertiesSize
     byte Properties[PropertiesSize]
   } [NumCoders]

   {
    varint InIndex;
    varint OutIndex;
   } [NumCoders - 1]

   varint PackStream[NumInStreams - (NumCoders - 1)]

  } [NumFolders]
*/
static int z7_folders_read(ffvec *stms, ffstr *d)
{
	int r;
	ffuint64 folders;
	ffvec a = {};

	if (0 == z7_readint(d, &folders))
		return Z7_EMORE;
	_ff7zread_log(_ff7z_log_param, 0, " folders:%U", folders);

	ffuint ext;
	if (0 != z7_readbyte(d, &ext))
		return Z7_EMORE;
	if (!!ext)
		return Z7_EUNSUPP;

	if (NULL == ffvec_zallocT(&a, folders + 1 /*folder for empty files*/, struct z7_folder))
		return Z7_ESYS;
	struct z7_folder *fo = (struct z7_folder*)a.ptr;

	ffslice streams;
	ffstr_set2(&streams, stms);

	for (ffsize i = 0;  i != folders;  i++) {
		if (0 != (r = z7_folder_read(&fo[i], d, &streams))) {
			ffvec_free(&a);
			return r;
		}
	}

	// replace stream[] array with folder[]
	a.len = folders;
	ffvec_free(stms);
	*stms = a;
	return 0;
}

/*
varint UnPackSize[folders][folder.coders]
*/
static int z7_datasizes_read(ffvec *folders, ffstr *d)
{
	ffuint64 n = 0;
	int r;
	struct z7_folder *fo = (struct z7_folder*)folders->ptr;

	for (ffsize i = 0;  i != folders->len;  i++) {
		for (ffuint ic = 0;  ic != fo[i].coders;  ic++) {
			if (0 == (r = z7_readint(d, &n)))
				return Z7_EMORE;
			fo[i].coder[ic].unpack_size = n;
			_ff7zread_log(_ff7z_log_param, 0, " folder#%L  coder#%u  unpacked-size:%xU", i, ic, n);
		}
		fo[i].unpack_size = n;
	}
	return 0;
}

/*
byte AllAreDefined
 0:
  bit Defined[NumFolders]
ffuint CRC[NumDefined]
*/
static int z7_datacrcs_read(ffvec *folders, ffstr *d)
{
	struct z7_folder *fo = (struct z7_folder*)folders->ptr;

	ffuint all;
	if (0 != z7_readbyte(d, &all))
		return Z7_EMORE;
	if (!all)
		return Z7_EUNSUPP;

	if (d->len < sizeof(int) * folders->len)
		return Z7_EMORE;

	for (ffsize i = 0;  i != folders->len;  i++) {
		ffuint n = ffint_le_cpu32_ptr(d->ptr);
		ffstr_shift(d, sizeof(int));
		_ff7zread_log(_ff7z_log_param, 0, " folder#%L CRC:%xu", i, n);
		fo[i].crc = n;
	}

	return 0;
}

/*
varint NumUnPackStreamsInFolders[folders]
*/
static int z7_stmfiles_read(ffvec *folders, ffstr *d)
{
	int r;
	ffuint64 n;
	struct z7_folder *fo = (struct z7_folder*)folders->ptr;

	for (ffsize i = 0;  i != folders->len;  i++) {

		if (0 == (r = z7_readint(d, &n)))
			return Z7_EMORE;
		_ff7zread_log(_ff7z_log_param, 0, " folder#%L  files:%U", i, n);
		if (n == 0)
			return Z7_EDATA;

		if (NULL == ffvec_zallocT(&fo[i].files, n, struct z7_fileinfo))
			return Z7_ESYS;
		fo[i].files.len = n;
	}

	return 0;
}

/** List of unpacked file size for non-empty files

varint UnPackSize[folders][folder.files]
*/
static int z7_fsizes_read(ffvec *folders, ffstr *d)
{
	int r;
	ffuint64 n, off;
	struct z7_folder *fo = (struct z7_folder*)folders->ptr;

	for (ffsize ifo = 0;  ifo != folders->len;  ifo++) {
		struct z7_fileinfo *f = (struct z7_fileinfo*)fo[ifo].files.ptr;
		off = 0;

		ffsize i;
		for (i = 0;  i != fo[ifo].files.len - 1;  i++) {

			if (0 == (r = z7_readint(d, &n)))
				return Z7_EMORE;
			f[i].off = off;
			f[i].size = n;
			_ff7zread_log(_ff7z_log_param, 0, "  folder#%u  file[%L]  size:%xU  off:%xU"
				, ifo, i, f[i].size, f[i].off);
			off += n;
			if (off > fo[ifo].unpack_size)
				return Z7_EDATA;
		}

		n = fo[ifo].unpack_size - off;
		f[i].off = off;
		f[i].size = n;
		_ff7zread_log(_ff7z_log_param, 0, "  folder#%u  file[%L]  size:%xU  off:%xU"
			, ifo, i, f[i].size, f[i].off);
	}

	return 0;
}

/** List of unpacked file CRC for non-empty files

byte AllAreDefined
 0:
  bit Defined[NumStreams]
ffuint CRC[NumDefined]
*/
static int z7_fcrcs_read(ffvec *folders, ffstr *d)
{
	ffuint all;
	if (0 != z7_readbyte(d, &all))
		return Z7_EMORE;
	if (!all)
		return Z7_EUNSUPP;

	ffsize dlen = d->len;
	struct z7_folder *fo;
	FFSLICE_WALK(folders, fo) {

		struct z7_fileinfo *f = (struct z7_fileinfo*)fo->files.ptr;
		if (fo->files.len == 0) {
			/* There were no 'NumUnPackStream' and no 'Size', coder is 'store': assume 1 file/folder */
			if (NULL == ffvec_zallocT(&fo->files, 1, struct z7_fileinfo))
				return Z7_ESYS;
			f = (struct z7_fileinfo*)fo->files.ptr;
			fo->files.len = 1;
			f->size = fo->unpack_size;
		}

		if (d->len < sizeof(int) * fo->files.len)
			return Z7_EMORE;

		for (ffsize i = 0;  i != fo->files.len;  i++) {
			ffuint n = ffint_le_cpu32_ptr(d->ptr);
			ffstr_shift(d, sizeof(int));
			f[i].crc = n;
		}
	}

	_ff7zread_log(_ff7z_log_param, 0, "  crc array: %L", dlen - d->len);
	return 0;
}

/** Get the number of all files (both nonempty and empty) and directories

varint NumFiles
*/
static int z7_fileinfo_read(ffvec *folders, ffstr *d)
{
	int r;
	ffuint64 n;
	if (0 == (r = z7_readint(d, &n)))
		return Z7_EMORE;
	_ff7zread_log(_ff7z_log_param, 0, " files:%U", n);
	if (n == 0)
		return Z7_EUNSUPP;

	ffuint64 all = 0;
	struct z7_folder *fo;
	FFSLICE_WALK(folders, fo) {
		all += fo->files.len;
	}

	if (n < all)
		return Z7_EDATA;

	if (n > all) {
		// add one more stream with empty files and directory entries
		fo = ffvec_pushT(folders, struct z7_folder);
		if (NULL == ffvec_zallocT(&fo->files, n - all, struct z7_fileinfo))
			return Z7_ESYS;
		fo->files.len = n - all;

		/* Get the number of bytes needed to hold the specified number of bits */
		ffsize nbytes = ((n + 7) / 8);

		if (NULL == ffvec_zallocT(&fo->empty, nbytes, ffbyte))
			return Z7_ESYS;
	}
	return 0;
}

/** List of empty files

bit IsEmptyStream[NumFiles]
*/
static int z7_emptystms_read(ffvec *folders, ffstr *d)
{
	ffsize i, bit_abs, n0 = 0;
	ffuint bit;

	for (i = 0;  i != d->len;  i++) {
		ffuint n = ffint_be_cpu32_ptr(d->ptr + i) & 0xff000000;

		while (0 <= (int)(bit = ffbit_find32(n) - 1)) {
			bit_abs = i * 8 + bit;
			_ff7zread_log(_ff7z_log_param, 0, " empty:%L", bit_abs);
			(void)bit_abs;
			ffbit_reset32(&n, 31 - bit);
			n0++;
		}
	}

	struct z7_folder *fo_empty = ffslice_lastT(folders, struct z7_folder);
	if ((n0 != 0 && fo_empty->empty.cap == 0)
		|| n0 != fo_empty->files.len)
		return Z7_EDATA; // invalid number of empty files

	i = ffmin(d->len, fo_empty->empty.cap);
	ffmem_copy(fo_empty->empty.ptr, d->ptr, i);

	ffstr_shift(d, d->len);
	return 0;
}

/*
bit IsEmptyFile[NumEmptyStreams]
*/
static int z7_emptyfiles_read(ffvec *folders, ffstr *d)
{
	(void)folders;
	ffstr_shift(d, d->len);
	return 0;
}

static int z7_dummy_read(ffvec *folders, ffstr *d)
{
	(void)folders;
	ffstr_shift(d, d->len);
	return 0;
}

/** Read filenames.  Put names of empty files into the last stream.

byte External
{
 ushort Name[]
 ushort 0
} [Files]
*/
static int z7_names_read(ffvec *folders, ffstr *d)
{
	int r;
	ffuint ext;
	ffssize n, cnt = 0;

	if (0 != z7_readbyte(d, &ext))
		return Z7_EMORE;
	if (!!ext)
		return Z7_EUNSUPP;

	struct z7_folder *fo_empty = ffslice_lastT(folders, struct z7_folder);
	struct z7_fileinfo *fem = (struct z7_fileinfo*)fo_empty->files.ptr;
	ffsize ifem = 0;
	if (fo_empty->empty.cap == 0)
		fo_empty = NULL;

	struct z7_folder *fo = (struct z7_folder*)folders->ptr;
	for (ffsize ifo = 0;  ifo != folders->len;  ifo++) {

		struct z7_fileinfo *f = (struct z7_fileinfo*)fo[ifo].files.ptr;
		ffsize i = 0;
		if (&fo[ifo] == fo_empty) {
			i = ifem;
			fo_empty = NULL;
		}

		while (i != fo[ifo].files.len) {

			if (0 > (n = ffutf16_findchar(d->ptr, d->len, 0x0000)))
				return Z7_EMORE;
			n += 2;

			ffvec a = {};
			if (NULL == ffvec_alloc(&a, n * 3, 1))
				return Z7_ESYS;

			if (0 == (r = ffutf8_from_utf16((char*)a.ptr, a.cap, d->ptr, n, FFUNICODE_UTF16LE))) {
				ffvec_free(&a);
				return Z7_EDATA;
			}
			ffstr_shift(d, n);
			_ff7zread_log(_ff7z_log_param, 0, " folder#%L name[%L](%L):%*s"
				, ifo, i, cnt, (ffsize)r - 1, a.ptr);
			a.len = _ffpack_path_normalize((char*)a.ptr, a.cap, (char*)a.ptr, r - 1, _FFPACK_PATH_FORCE_SLASH | _FFPACK_PATH_SIMPLE);
			((char*)a.ptr)[a.len] = '\0';

			if (fo_empty != NULL && ffbit_array_test(fo_empty->empty.ptr, cnt)) {
				ffstr_set2(&fem[ifem].name, &a);
				ifem++;
			} else {
				ffstr_set2(&f[i].name, &a);
				i++;
			}
			cnt++;
		}
	}

	return 0;
}

typedef struct z7_fftime_winftime {
	ffuint lo, hi;
} z7_fftime_winftime;

/** Windows FILETIME -> fftime */
static inline fftime z7_fftime_from_winftime(const z7_fftime_winftime *ft)
{
	fftime t = {};
	ffuint64 i = ((ffuint64)ft->hi << 32) | ft->lo;
	const ffuint64 tm100ns = 116444736000000000ULL; // 100-ns intervals within 1600..1970
	if (i > tm100ns) {
		i -= tm100ns;
		t.sec = i / (1000000 * 10);
		t.nsec = (i % (1000000 * 10)) * 100;
	}
	return t;
}

/*
byte AllAreDefined
 0:
  bit TimeDefined[NumFiles]
byte External
{
 uint32 TimeLow
 uint32 TimeHi
} [NumDefined]
*/
static int z7_mtimes_read(ffvec *folders, ffstr *d)
{
	ffuint all, ext, cnt = 0;

	if (d->len < 2)
		return Z7_EMORE;

	z7_readbyte(d, &all);
	if (!all)
		return Z7_EUNSUPP;

	z7_readbyte(d, &ext);
	if (!!ext)
		return Z7_EUNSUPP;

	struct z7_folder *fo_empty = ffslice_lastT(folders, struct z7_folder);
	struct z7_fileinfo *fem = (struct z7_fileinfo*)fo_empty->files.ptr;
	ffsize ifem = 0;
	if (fo_empty->empty.cap == 0)
		fo_empty = NULL;

	struct z7_folder *fo;
	FFSLICE_WALK(folders, fo) {

		struct z7_fileinfo *f = (struct z7_fileinfo*)fo->files.ptr;
		ffsize i = 0;
		if (fo == fo_empty) {
			i = ifem;
			fo_empty = NULL;
		}

		while (i != fo->files.len) {

			if (d->len < 8)
				return Z7_EMORE;

			z7_fftime_winftime ft;
			ft.lo = ffint_le_cpu32_ptr(d->ptr);
			ft.hi = ffint_le_cpu32_ptr(d->ptr + 4);
			ffstr_shift(d, 8);

			fftime t = z7_fftime_from_winftime(&ft);
			if (fo_empty != NULL && ffbit_array_test(fo_empty->empty.ptr, cnt))
				fem[ifem++].mtime = t;
			else
				f[i++].mtime = t;
			cnt++;
		}
	}

	return 0;
}

/*
byte AllAreDefined
 0:
  bit AttributesAreDefined[NumFiles]
byte External
uint32 Attributes[Defined]
}
*/
static int z7_winattrs_read(ffvec *folders, ffstr *d)
{
	ffuint n, all, ext, cnt = 0;

	if (d->len < 2)
		return Z7_EMORE;

	z7_readbyte(d, &all);
	if (!all)
		return Z7_EUNSUPP;

	z7_readbyte(d, &ext);
	if (!!ext)
		return Z7_EUNSUPP;

	struct z7_folder *fo_empty = ffslice_lastT(folders, struct z7_folder);
	struct z7_fileinfo *fem = (struct z7_fileinfo*)fo_empty->files.ptr;
	ffsize ifem = 0;
	if (fo_empty->empty.cap == 0)
		fo_empty = NULL;

	struct z7_folder *fo;
	FFSLICE_WALK(folders, fo) {

		struct z7_fileinfo *f = (struct z7_fileinfo*)fo->files.ptr;
		ffsize i = 0;
		if (fo == fo_empty) {
			i = ifem;
			fo_empty = NULL;
		}

		while (i != fo->files.len) {

			if (d->len < 4)
				return Z7_EMORE;

			n = ffint_le_cpu32_ptr(d->ptr);
			ffstr_shift(d, 4);
			_ff7zread_log(_ff7z_log_param, 0, " attr[%L]:%xu", i, n);
			n &= 0xff;

			if (fo_empty != NULL && ffbit_array_test(fo_empty->empty.ptr, cnt))
				fem[ifem++].attr = n;
			else
				f[i++].attr = n;
			cnt++;
		}
	}

	return 0;
}

/* Supported blocks:
Header (0x01)
 MainStreamsInfo (0x04)
  PackInfo (0x06)
   Size (0x09)
  UnPackInfo (0x07)
   Folder (0x0b)
   UnPackSize (0x0c)
   CRC (0x0a)
  SubStreamsInfo (0x08)
   NumUnPackStream (0x0d)
   Size (0x09)
   CRC (0x0a)
 FilesInfo (0x05)
  EmptyStream (0x0e)
  EmptyFile (0x0f)
  Name (0x11)
  MTime (0x14)
  WinAttributes (0x15)
  Dummy (0x19)
EncodedHeader (0x17)
 PackInfo
  ...
 UnPackInfo
  ...
 SubStreamsInfo
  ...
*/

static const struct z7_binblock
	z7_hdr_ctx[],
	z7_stminfo_ctx[],
	z7_stm_packinfo_ctx[],
	z7_stm_unpackinfo_ctx[],
	z7_stm_substminfo_ctx[],
	z7_fileinfo_ctx[];

static const struct z7_binblock z7_ctx_top[] = {
	{ Z7_T_Header | Z7_F_CHILDREN,	z7_hdr_ctx },
	{ Z7_T_EncodedHeader | Z7_F_CHILDREN | Z7_F_LAST,	z7_stminfo_ctx },
};

static const struct z7_binblock z7_hdr_ctx[] = {
	{ Z7_T_AdditionalStreamsInfo | Z7_F_CHILDREN,	z7_stminfo_ctx },
	{ Z7_T_MainStreamsInfo | Z7_F_REQ | Z7_F_CHILDREN | Z7_PRIO(1),	z7_stminfo_ctx },
	{ Z7_T_FilesInfo | Z7_F_CHILDREN | Z7_PRIO(2),	z7_fileinfo_ctx },
	{ Z7_T_End | Z7_F_LAST,	NULL },
};

static const struct z7_binblock z7_stminfo_ctx[] = {
	{ Z7_T_PackInfo | Z7_F_REQ | Z7_F_CHILDREN | Z7_PRIO(1),	z7_stm_packinfo_ctx },
	{ Z7_T_UnPackInfo | Z7_F_REQ | Z7_F_CHILDREN | Z7_PRIO(2),	z7_stm_unpackinfo_ctx },
	{ Z7_T_SubStreamsInfo | Z7_F_CHILDREN | Z7_PRIO(3),	z7_stm_substminfo_ctx },
	{ Z7_T_End | Z7_F_LAST,	NULL },
};

static const struct z7_binblock z7_stm_packinfo_ctx[] = {
	{ 0xff | Z7_F_SELF,	z7_packinfo_read },
	{ Z7_T_Size | Z7_F_REQ,	z7_packsizes_read },
	{ Z7_T_End | Z7_F_LAST,	NULL },
};

static const struct z7_binblock z7_stm_unpackinfo_ctx[] = {
	{ Z7_T_Folder | Z7_PRIO(1),	z7_folders_read },
	{ Z7_T_UnPackSize | Z7_PRIO(2),	z7_datasizes_read },
	{ Z7_T_CRC,	z7_datacrcs_read },
	{ Z7_T_End | Z7_F_LAST,	NULL },
};

static const struct z7_binblock z7_stm_substminfo_ctx[] = {
	{ Z7_T_NumUnPackStream | Z7_PRIO(1),	z7_stmfiles_read },
	{ Z7_T_Size | Z7_PRIO(2),	z7_fsizes_read },
	{ Z7_T_CRC,	z7_fcrcs_read },
	{ Z7_T_End | Z7_F_LAST,	NULL },
};

static const struct z7_binblock z7_fileinfo_ctx[] = {
	{ 0xff | Z7_F_SELF,	z7_fileinfo_read },
	{ Z7_T_EmptyStream | Z7_F_SIZE | Z7_PRIO(1),	z7_emptystms_read },
	{ Z7_T_EmptyFile | Z7_F_SIZE | Z7_PRIO(2),	z7_emptyfiles_read },
	{ Z7_T_Name | Z7_F_REQ | Z7_F_SIZE,	z7_names_read },
	{ Z7_T_MTime | Z7_F_SIZE,	z7_mtimes_read },
	{ Z7_T_WinAttributes | Z7_F_SIZE,	z7_winattrs_read },
	{ Z7_T_Dummy | Z7_F_SIZE | Z7_F_MULTI,	z7_dummy_read },
	{ Z7_T_End | Z7_F_LAST,	NULL },
};
