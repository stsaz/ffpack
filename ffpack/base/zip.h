/** ffpack: .zip format
2020, Simon Zolin */

/*
zip_extra_next
zip_extra_cdir64_read	zip_extra_cdir64_write
zip_extra_fhdr64_read	zip_extra_fhdr64_write
zip_extra_newunix_read	zip_extra_newunix_write
zip_extra_unixtime_read	zip_extra_unixtime_write
zip_extra_ntfs_read
zip_filehdr_read	zip_filehdr_write
zip_filetrl_read
zip_filetrl64_read	zip_filetrl64_write
zip_cdir_read	zip_cdir_write	zip_cdir_finishwrite
zip_cdirtrl_read	zip_cdirtrl_find	zip_cdirtrl_write
zip_cdirtrl64_read	zip_cdirtrl64_write
zip_cdirtrl64_loc_read	zip_cdirtrl64_loc_write
*/

/* .zip format:
(FILE_HDR [EXTRA...] DATA [FILE_TRL || FILE_TRL64])...
(CDIR_ENTRY [EXTRA...])...
[CDIR_TRL64 CDIR_TRL64_LOCATOR] CDIR_TRL
*/

#pragma once

#include <ffbase/string.h>
#include <ffbase/time.h>

enum ZIP_COMP {
	ZIP_STORED = 0,
	ZIP_DEFLATED = 8,
	ZIP_ZSTANDARD = 93,
};

struct zip_fileinfo {
	ffstr name;
	fftime mtime; // seconds since 1970
	ffuint attr_win, attr_unix;
	ffuint uid, gid;
	enum ZIP_COMP compress_method;
	ffuint uncompressed_crc;
	ffuint64 uncompressed_size, compressed_size;
	ffuint64 hdr_offset;
};

static inline int zip_fileinfo_isdir(const struct zip_fileinfo *zf)
{
	return ((zf->attr_unix & 0170000) == 0040000)
		|| (zf->attr_win & 0x10);
}

#define ZIP_MINVER  20

enum ZIP_FLAGS {
	ZIP_FENCRYPTED = 1,
	ZIP_FDATADESC = 8, // zip_filetrl
};

/** File header */
struct zip_filehdr {
	char sig[4]; // "PK\3\4"
	ffbyte minver[2];
	ffbyte flags[2]; // enum ZIP_FLAGS
	ffbyte comp[2]; // enum ZIP_COMP
	ffbyte modtime[2];
	ffbyte moddate[2];
	ffbyte crc32[4];
	ffbyte size[4];
	ffbyte size_orig[4];
	ffbyte filenamelen[2];
	ffbyte extralen[2];
	char filename[0];
	// char extra[0];
};

/** File trailer */
struct zip_filetrl {
	// char sig[4]; // "PK\7\8" - optional signature
	ffbyte crc32[4];
	ffbyte size[4];
	ffbyte size_orig[4];
};

/** zip64 file trailer */
struct zip64_filetrl {
	// char sig[4]; // "PK\7\8" - optional signature
	ffbyte crc32[4];
	ffbyte size[8];
	ffbyte size_orig[8];
};

enum ZIP_SYS {
	ZIP_FAT = 0,
	ZIP_UNIX = 3,
};

/** Central directory header */
struct zip_cdir {
	char sig[4]; // "PK\1\2"
	ffbyte ver;
	ffbyte sysver; // enum ZIP_SYS
	ffbyte minver[2];
	ffbyte flags[2];
	ffbyte comp[2];
	ffbyte modtime[2];
	ffbyte moddate[2];
	ffbyte crc32[4];
	ffbyte size[4];
	ffbyte size_orig[4];
	ffbyte filenamelen[2];
	ffbyte extralen[2];
	ffbyte commentlen[2];
	ffbyte disknum[2];
	ffbyte attrs_int[2]; // bit 0: is text file
	ffbyte attrs[4]; // [0]: enum FFWIN_FILE_ATTR; [2..3]: enum FFUNIX_FILEATTR
	ffbyte offset[4];
	char filename[0]; // relative path with '/' (not '\\')
	// char extra[0];
	// char comment[0];
};

struct zip_extrahdr {
	char id[2];
	ffbyte size[2]; // size of the following data
};

// struct zip_extra_zip64 {
	// char id[2]; // "\1\0"
	// ffbyte size[2];
	// ffbyte size_orig[8]; // appears if zip_cdir.size_orig == -1
	// ffbyte size_comp[8]; // appears if zip_cdir.size == -1
	// ffbyte offset[8]; // appears if zip_cdir.offset == -1
	// ffbyte disk[4]; // appears if zip_cdir.disknum == -1
// };

struct zip_extra_ntfs {
	// char id[2]; // "\x0A\0"
	// ffbyte size[2];
	ffbyte reserved[4];

	ffbyte tag[2]; // "\1\0"
	ffbyte tag_size[2]; // size of the following data
	ffbyte mod_time[8];
	ffbyte access_time[8];
	ffbyte create_time[8];
};

struct zip_extra_newunix {
	// char id[2]; // "ux"
	// ffbyte size[2];
	ffbyte ver; //=1
	ffbyte uid_size;
	ffbyte uid[0];
	// ffbyte gid_size;
	// ffbyte gid[0];
};

struct zip_extra_unix {
	// char id[2]; // "Ux"
	// ffbyte size[2];
	ffbyte uid[2];
	ffbyte gid[2];
};

struct zip_extra_unixtime {
	// char id[2]; // "UT"
	// ffbyte size[2];
	ffbyte flags; // 0x01:mtime, 0x02:atime, 0x04:ctime
	// ffbyte mtime[4]; // UTC
	// ffbyte atime[4];
	// ffbyte ctime[4];
};

struct zip_cdirtrl {
	char sig[4]; // "PK\5\6"
	ffbyte disknum[2];
	ffbyte cdir_disk[2];
	ffbyte disk_ents[2];
	ffbyte total_ents[2];
	ffbyte cdir_size[4]; // size of zip_cdir[].  -1: value is in zip64_cdirtrl
	ffbyte cdir_off[4]; // -1: value is in zip64_cdirtrl
	ffbyte commentlen[2];
	ffbyte comment[0];
};

#define ZIP_CDIR_TRL_MAXSIZE  (sizeof(struct zip_cdirtrl) + 0xffff)

struct zip64_cdirtrl {
	char sig[4]; // "PK\6\6"
	ffbyte size[8]; // size of the following data
	ffbyte ver[2];
	ffbyte ver2[2];
	ffbyte disk[4];
	ffbyte cdir_disk[4];
	ffbyte disk_ents[8];
	ffbyte total_ents[8];
	ffbyte cdir_size[8];
	ffbyte cdir_offset[8];
	// char data[]
};

struct zip64_cdirtrl_loc {
	char sig[4]; //"PK\6\7"
	ffbyte cdir_trl_disk[4];
	ffbyte offset[8];
	ffbyte disks_number[4];
};

/** Get next CDIR extra record
Return record ID */
static inline int zip_extra_next(ffstr *data, ffstr *chunk)
{
	if (4 > data->len)
		return -1;
	ffuint id = ffint_le_cpu16_ptr(data->ptr);
	ffuint size = ffint_le_cpu16_ptr(&data->ptr[2]);
	if (4 + size > data->len)
		return -1;
	ffstr_set(chunk, &data->ptr[4], size);
	ffstr_shift(data, 4 + size);
	return id;
}

/** Read zip64-extra from CDIR entry */
static inline int zip_extra_cdir64_read(ffstr data, const void *cdir_data, struct zip_fileinfo *info)
{
	ffuint i = 0;
	const struct zip_cdir *cdir = (struct zip_cdir*)cdir_data;

	if (ffint_le_cpu32_ptr(cdir->size_orig) == 0xffffffff) {
		if (i + 8 > data.len)
			return -1;
		info->uncompressed_size = ffint_le_cpu64_ptr(&data.ptr[i]);
		i += 8;
	}

	if (ffint_le_cpu32_ptr(cdir->size) == 0xffffffff) {
		if (i + 8 > data.len)
			return -1;
		info->compressed_size = ffint_le_cpu64_ptr(&data.ptr[i]);
		i += 8;
	}

	if (ffint_le_cpu32_ptr(cdir->offset) == 0xffffffff) {
		if (i + 8 > data.len)
			return -1;
		info->hdr_offset = ffint_le_cpu64_ptr(&data.ptr[i]);
		i += 8;
	}

	// disk

	return 0;
}

/** Read zip64-extra from file header
Return number of fields read */
static inline int zip_extra_fhdr64_read(ffstr data, const void *fhdr_data, struct zip_fileinfo *info)
{
	ffuint i = 0;
	const struct zip_filehdr *h = (struct zip_filehdr*)fhdr_data;

	if (ffint_le_cpu32_ptr(h->size_orig) == 0xffffffff) {
		if (i + 8 > data.len)
			return -1;
		info->uncompressed_size = ffint_le_cpu64_ptr(&data.ptr[i]);
		i += 8;
	}

	if (ffint_le_cpu32_ptr(h->size) == 0xffffffff) {
		if (i + 8 > data.len)
			return -1;
		info->compressed_size = ffint_le_cpu64_ptr(&data.ptr[i]);
		i += 8;
	}

	// disk

	return 0;
}

/** Write zip64-extra in CDIR entry */
static inline int zip_extra_cdir64_write(void *buf, const struct zip_fileinfo *info)
{
	ffuint sz = 4+8+8+8;
	if (buf == NULL)
		return sz;

	ffbyte *d = (ffbyte*)buf;
	ffmem_copy(d, "\x01\x00", 2);
	*(ffushort*)&d[2] = ffint_le_cpu16(sz - 4);

	*(ffuint64*)&d[4] = ffint_le_cpu64(info->uncompressed_size);
	*(ffuint64*)&d[12] = ffint_le_cpu64(info->compressed_size);
	*(ffuint64*)&d[20] = ffint_le_cpu64(info->hdr_offset);
	return sz;
}

/** Write zip64-extra in file header entry */
static inline int zip_extra_fhdr64_write(void *buf, ffuint64 uncompressed_size, ffuint64 compressed_size)
{
	ffuint sz = 4+8+8;
	if (buf == NULL)
		return sz;

	ffbyte *d = (ffbyte*)buf;
	ffmem_copy(d, "\x01\x00", 2);
	*(ffushort*)&d[2] = ffint_le_cpu16(sz - 4);

	*(ffuint64*)&d[4] = ffint_le_cpu64(uncompressed_size);
	*(ffuint64*)&d[12] = ffint_le_cpu64(compressed_size);
	return sz;
}

static inline int zip_extra_newunix_read(ffstr data, struct zip_fileinfo *info)
{
	if (data.len < 2)
		return -1;
	if (data.ptr[0] != 1) // ver
		return -1;
	ffstr_shift(&data, 1);

	if (data.ptr[0] != 0) { // uid_size
		if (data.ptr[0] == 4)
			info->uid = ffint_le_cpu32_ptr(&data.ptr[1]);
		ffstr_shift(&data, 1 + data.ptr[0]);
	}

	if (data.len < 1)
		return -1;
	if (data.ptr[0] != 0) { // gid_size
		if (data.ptr[0] == 4)
			info->gid = ffint_le_cpu32_ptr(&data.ptr[1]);
		ffstr_shift(&data, 1 + data.ptr[0]);
	}
	return 0;
}

static inline int zip_extra_newunix_write(void *buf, const struct zip_fileinfo *info)
{
	ffuint sz = 4+1+1+4+1+4;
	if (buf == NULL)
		return sz;

	ffbyte *d = (ffbyte*)buf;
	ffmem_copy(d, "ux", 2);
	*(ffushort*)&d[2] = ffint_le_cpu16(sz - 4);

	d[4] = 1; // ver
	d[5] = 4; // uid_size
	*(ffuint*)&d[6] = ffint_le_cpu32(info->uid);
	d[10] = 4; // gid_size
	*(ffuint*)&d[11] = ffint_le_cpu32(info->gid);
	return sz;
}

static inline int zip_extra_unixtime_read(ffstr data, struct zip_fileinfo *info)
{
	if (data.len < 1)
		return -1;
	ffuint f = data.ptr[0];
	ffstr_shift(&data, 1);

	if (f & 0x01) {
		if (data.len < 4)
			return -1;
		*(ffuint*)&info->mtime.sec = ffint_le_cpu32_ptr(data.ptr);
		info->mtime.nsec = 0;
		ffstr_shift(&data, 4);
	}

	if (f & 0x02) {
		if (data.len < 4)
			return -1;
		// atime
		ffstr_shift(&data, 4);
	}

	if (f & 0x04) {
		if (data.len < 4)
			return -1;
		// ctime
		ffstr_shift(&data, 4);
	}
	return 0;
}

static inline int zip_extra_unixtime_write(void *buf, const struct zip_fileinfo *info)
{
	ffuint sz = 4+1+4;
	if (buf == NULL)
		return sz;

	ffbyte *d = (ffbyte*)buf;
	ffmem_copy(d, "UT", 2);
	*(ffushort*)&d[2] = ffint_le_cpu16(sz - 4);

	d[4] = 0x01;
	*(ffuint*)&d[5] = ffint_le_cpu32(info->mtime.sec);
	return sz;
}

static inline int zip_extra_ntfs_read(ffstr data, struct zip_fileinfo *info)
{
	if (sizeof(struct zip_extra_ntfs) > data.len)
		return -1;
	const struct zip_extra_ntfs *ntfs = (struct zip_extra_ntfs*)data.ptr;

	ffuint tag = ffint_le_cpu16_ptr(ntfs->tag);
	if (tag != 1)
		return -1;

	ffuint tag_size = ffint_le_cpu16_ptr(ntfs->tag_size);
	if (tag_size < 8 * 3)
		return -1;

	ffuint64 t = ffint_le_cpu64_ptr(ntfs->mod_time);
	const ffuint64 TIME_1600_1970_100NS = 116444736000000000ULL; // 100-ns intervals within 1600..1970
	ffmem_zero_obj(&info->mtime);
	if (t > TIME_1600_1970_100NS) {
		t -= TIME_1600_1970_100NS;
		info->mtime.sec = t / (1000000 * 10);
		info->mtime.nsec = (t % (1000000 * 10)) * 100;
	}

	return 0;
}

static inline void _zip_time_todos(fftime t, ffuint *date, ffuint *time)
{
	t.sec += FFTIME_1970_SECONDS;
	ffdatetime dt = {};
	fftime_split1(&dt, &t);

	*date = ((dt.year - 1980) << 9) | (dt.month << 5) | (dt.day);
	*time = (dt.hour << 11) | (dt.minute << 5) | (dt.second / 2);
}

static inline void _zip_time_fromdos(fftime *t, ffuint date, ffuint time)
{
	ffdatetime dt = {};
	dt.year = (date >> 9) + 1980;
	dt.month = (date >> 5) & 0x0f;
	dt.day = (date & 0x1f);
	dt.hour = (time >> 11);
	dt.minute = (time >> 5) & 0x3f;
	dt.second = (time & 0x1f) * 2;

	fftime_join1(t, &dt);
	t->sec -= FFTIME_1970_SECONDS;
}

/** Write file header
info->compressed_size==-1: file trailer will be written after data
Return N of bytes written;
  <0 on error */
static inline int zip_filehdr_write(void *buf, const struct zip_fileinfo *info, int timezone_offset)
{
	ffuint extralen = zip_extra_fhdr64_write(NULL, 0, 0)
		+ zip_extra_unixtime_write(NULL, NULL)
		+ zip_extra_newunix_write(NULL, NULL);
	if (buf == NULL)
		return sizeof(struct zip_filehdr) + info->name.len + extralen;

	if (info->name.len > 0xffff)
		return -1;

	struct zip_filehdr *h = (struct zip_filehdr*)buf;
	static const ffbyte zip_defhdr[] = {
		'P','K','\x03','\x04',  ZIP_MINVER,0,  0,0,
	};
	ffmem_copy(h, zip_defhdr, sizeof(zip_defhdr));
	if (info->compressed_size == (ffuint64)-1)
		h->flags[0] = ZIP_FDATADESC;
	*(ffushort*)h->comp = ffint_le_cpu16(info->compress_method);
	*(ffuint*)h->crc32 = ffint_le_cpu32(info->uncompressed_crc);
	*(ffuint*)h->size = 0xffffffff;
	*(ffuint*)h->size_orig = 0xffffffff;

	ffuint modtime, moddate;
	fftime mtime_local = info->mtime;
	mtime_local.sec += timezone_offset;
	_zip_time_todos(mtime_local, &moddate, &modtime);
	*(ffushort*)h->modtime = ffint_le_cpu16(modtime);
	*(ffushort*)h->moddate = ffint_le_cpu16(moddate);

	ffmem_copy(h->filename, info->name.ptr, info->name.len);
	*(ffushort*)h->filenamelen = ffint_le_cpu16(info->name.len);

	*(ffushort*)h->extralen = ffint_le_cpu16(extralen);
	ffbyte *extra = (ffbyte*)h->filename + info->name.len;
	extra += zip_extra_fhdr64_write(extra, info->uncompressed_size, info->compressed_size);
	extra += zip_extra_unixtime_write(extra, info);
	extra += zip_extra_newunix_write(extra, info);

	return sizeof(struct zip_filehdr) + info->name.len + extralen;
}

/** Update filehdr.crc, extra.zip64.uncompressed_size, extra.zip64.compressed_size fields */
static inline void zip_filehdr_update(void *buf, ffuint64 uncompressed_size, ffuint64 compressed_size, ffuint crc)
{
	struct zip_filehdr *h = (struct zip_filehdr*)buf;
	*(ffuint*)h->crc32 = ffint_le_cpu32(crc);
	ffuint filenamelen = ffint_le_cpu16_ptr(h->filenamelen);
	ffbyte *extra = (ffbyte*)h->filename + filenamelen;
	zip_extra_fhdr64_write(extra, uncompressed_size, compressed_size);
}

/** Parse file header
Return file header size
 <0 on error */
static inline int zip_filehdr_read(const void *buf, struct zip_fileinfo *info, int timezone_offset)
{
	const struct zip_filehdr *h = (struct zip_filehdr*)buf;
	if (!!ffmem_cmp(h->sig, "PK\x03\x04", 4))
		return -1;

	ffuint flags = ffint_le_cpu16_ptr(h->flags);
	if (flags & ZIP_FENCRYPTED)
		return -1;

	_zip_time_fromdos(&info->mtime, ffint_le_cpu16_ptr(h->moddate), ffint_le_cpu16_ptr(h->modtime));
	info->mtime.sec -= timezone_offset;

	info->compress_method = (enum ZIP_COMP)ffint_le_cpu16_ptr(h->comp);
	info->uncompressed_crc = ffint_le_cpu32_ptr(h->crc32);
	info->compressed_size = ffint_le_cpu32_ptr(h->size);
	info->uncompressed_size = ffint_le_cpu32_ptr(h->size_orig);
	return sizeof(struct zip_filehdr) + ffint_le_cpu16_ptr(h->filenamelen) + ffint_le_cpu16_ptr(h->extralen);
}

static inline int zip_filetrl64_write(void *buf, ffuint64 file_insize, ffuint64 file_outsize, ffuint crc)
{
	if (buf == NULL)
		return 4 + sizeof(struct zip64_filetrl);

	ffmem_copy(buf, "PK\x07\x08", 4);
	struct zip64_filetrl *trl = (struct zip64_filetrl*)((char*)buf + 4);
	*(ffuint*)trl->crc32 = ffint_le_cpu32(crc);
	*(ffuint64*)trl->size = ffint_le_cpu64(file_outsize);
	*(ffuint64*)trl->size_orig = ffint_le_cpu64(file_insize);
	return 4 + sizeof(struct zip64_filetrl);
}

static inline void zip_filetrl_read(const void *buf, struct zip_fileinfo *info)
{
	if (!ffmem_cmp(buf, "PK\x07\x08", 4))
		buf = (ffbyte*)buf + 4;
	const struct zip_filetrl *trl = (struct zip_filetrl*)buf;
	info->uncompressed_crc = ffint_le_cpu32_ptr(trl->crc32);
	info->compressed_size = ffint_le_cpu32_ptr(trl->size);
	info->uncompressed_size = ffint_le_cpu32_ptr(trl->size_orig);
}

static inline void zip_filetrl64_read(const void *buf, struct zip_fileinfo *info)
{
	if (!ffmem_cmp(buf, "PK\x07\x08", 4))
		buf = (ffbyte*)buf + 4;
	const struct zip64_filetrl *trl = (struct zip64_filetrl*)buf;
	info->uncompressed_crc = ffint_le_cpu32_ptr(trl->crc32);
	info->compressed_size = ffint_le_cpu64_ptr(trl->size);
	info->uncompressed_size = ffint_le_cpu64_ptr(trl->size_orig);
}

/** Write CDIR header and extra data: unixtime, newmode
Return N of bytes written;
  <0 on error */
static inline int zip_cdir_write(void *buf, const struct zip_fileinfo *info, int timezone_offset)
{
	ffuint extralen = zip_extra_cdir64_write(NULL, NULL)
		+ zip_extra_unixtime_write(NULL, NULL)
		+ zip_extra_newunix_write(NULL, NULL);
	if (buf == NULL)
		return sizeof(struct zip_cdir) + info->name.len + extralen;

	if (info->name.len > 0xffff)
		return -1;

	struct zip_cdir *cdir = (struct zip_cdir*)buf;
	ffmem_zero_obj(cdir);
	static const ffbyte zip_defcdir[] = {
		'P','K','\x01','\x02',  ZIP_MINVER,0,  ZIP_MINVER,0,  ZIP_FDATADESC,0,  0,0,
	};
	ffmem_copy(cdir, zip_defcdir, sizeof(zip_defcdir));

	*(ffushort*)cdir->comp = ffint_le_cpu16(info->compress_method);

#ifdef FF_WIN
	cdir->sysver = ZIP_FAT;
#else
	cdir->sysver = ZIP_UNIX;
#endif
	cdir->attrs[0] = (ffbyte)info->attr_win;
	*(ffushort*)&cdir->attrs[2] = ffint_le_cpu16(info->attr_unix);

	ffuint modtime, moddate;
	fftime mtime_local = info->mtime;
	mtime_local.sec += timezone_offset;
	_zip_time_todos(mtime_local, &moddate, &modtime);
	*(ffushort*)cdir->modtime = ffint_le_cpu16(modtime);
	*(ffushort*)cdir->moddate = ffint_le_cpu16(moddate);

	*(ffuint*)cdir->crc32 = ffint_le_cpu32(info->uncompressed_crc);
	*(ffuint*)cdir->size = 0xffffffff;
	*(ffuint*)cdir->size_orig = 0xffffffff;
	*(ffuint*)cdir->offset = 0xffffffff;

	*(ffushort*)cdir->filenamelen = ffint_le_cpu16(info->name.len);
	ffmem_copy(cdir->filename, info->name.ptr, info->name.len);

	*(ffushort*)cdir->extralen = ffint_le_cpu16(extralen);
	ffbyte *extra = (ffbyte*)cdir->filename + info->name.len;
	extra += zip_extra_cdir64_write(extra, info);
	extra += zip_extra_unixtime_write(extra, info);
	extra += zip_extra_newunix_write(extra, info);

	return sizeof(struct zip_cdir) + info->name.len + extralen;
}

/** Update CRC in cdir; original size, compressed size fields in cdir.extra.zip64 */
static inline void zip_cdir_finishwrite(void *buf, ffuint64 uncompressed_size, ffuint64 compressed_size, ffuint crc)
{
	struct zip_cdir *cdir = (struct zip_cdir*)buf;
	*(ffuint*)cdir->crc32 = ffint_le_cpu32(crc);

	ffstr extra, data;
	ffstr_set(&extra, (ffbyte*)buf + sizeof(struct zip_cdir) + ffint_le_cpu16_ptr(cdir->filenamelen), ffint_le_cpu16_ptr(cdir->extralen));
	while (extra.len != 0) {
		int id = zip_extra_next(&extra, &data);
		if (id == 0x0001) {
			FF_ASSERT(data.len >= 8*2);

			*(ffuint64*)data.ptr = ffint_le_cpu64(uncompressed_size);
			ffstr_shift(&data, 8);

			*(ffuint64*)data.ptr = ffint_le_cpu64(compressed_size);
			ffstr_shift(&data, 8);
			break;
		}
	}
}

/** Parse CDIR entry (without file name)
Return CDIR entry size
  <0 on error */
static inline int zip_cdir_read(const void *buf, struct zip_fileinfo *info, int timezone_offset)
{
	const struct zip_cdir *cdir = (struct zip_cdir*)buf;
	if (!!ffmem_cmp(cdir->sig, "PK\1\2", 4))
		return -1;

	_zip_time_fromdos(&info->mtime, ffint_le_cpu16_ptr(cdir->moddate), ffint_le_cpu16_ptr(cdir->modtime));
	info->mtime.sec -= timezone_offset;

	info->compress_method = (enum ZIP_COMP)ffint_le_cpu16_ptr(&cdir->comp);
	info->compressed_size = ffint_le_cpu32_ptr(cdir->size);
	info->uncompressed_size = ffint_le_cpu32_ptr(cdir->size_orig);
	info->attr_win = cdir->attrs[0];
	info->attr_unix = ffint_le_cpu16_ptr(&cdir->attrs[2]);
	info->uncompressed_crc = ffint_le_cpu32_ptr(cdir->crc32);
	info->hdr_offset = ffint_le_cpu32_ptr(cdir->offset);
	return sizeof(struct zip_cdir) + ffint_le_cpu16_ptr(cdir->filenamelen) + ffint_le_cpu16_ptr(cdir->extralen) + ffint_le_cpu16_ptr(cdir->commentlen);
}

/** Write CDIR trailer
Return N of bytes written;
  <0 on error */
static inline int zip_cdirtrl_write(void *buf, ffsize cdir_len, ffuint64 cdir_off, ffuint items_num)
{
	if (buf == NULL)
		return sizeof(struct zip_cdirtrl);

	if (cdir_len > 0xffffffff || cdir_off > 0xffffffff || items_num > 0xffff)
		return -1;

	struct zip_cdirtrl *trl = (struct zip_cdirtrl*)buf;
	ffmem_zero_obj(trl);
	ffmem_copy(trl, "PK\x05\x06", 4);
	*(ffushort*)trl->disk_ents = ffint_le_cpu16(items_num);
	*(ffushort*)trl->total_ents = ffint_le_cpu16(items_num);
	*(ffuint*)trl->cdir_size = ffint_le_cpu32(cdir_len);
	*(ffuint*)trl->cdir_off = ffint_le_cpu32(cdir_off);
	return sizeof(struct zip_cdirtrl);
}

/** Search for CDIR trailer */
static inline int zip_cdirtrl_find(const void *buf, ffsize len)
{
	return ffs_rfindstr((char*)buf, len, "PK\x05\x06", 4);
}

/** Read CDIR trailer */
static inline int zip_cdirtrl_read(const void *buf, ffsize len, ffuint *disk, ffuint *cdir_disk, ffuint *cdir_size, ffuint *cdir_offset)
{
	if (len < sizeof(struct zip_cdirtrl))
		return -1;

	const struct zip_cdirtrl *trl = (struct zip_cdirtrl*)buf;
	if (!!ffmem_cmp(trl->sig, "PK\x05\x06", 4))
		return -1;

	*disk = ffint_le_cpu16_ptr(trl->disknum);
	*cdir_disk = ffint_le_cpu16_ptr(trl->cdir_disk);
	*cdir_size = ffint_le_cpu32_ptr(trl->cdir_size);
	*cdir_offset = ffint_le_cpu32_ptr(trl->cdir_off);

	ffuint commentlen = ffint_le_cpu16_ptr(trl->commentlen);
	if (len < sizeof(struct zip_cdirtrl) + commentlen)
		return -1;
	return 0;
}

/** Read CDIR zip64 trailer */
static inline int zip_cdirtrl64_read(const void *buf, ffuint *size, ffuint *disk, ffuint *cdir_disk, ffuint64 *cdir_size, ffuint64 *cdir_offset)
{
	const struct zip64_cdirtrl *trl = (struct zip64_cdirtrl*)buf;
	if (!!ffmem_cmp(trl->sig, "PK\x06\x06", 4))
		return -1;

	*size = ffint_le_cpu32_ptr(trl->size);
	if (*size < sizeof(struct zip64_cdirtrl) - 12)
		return -1;

	*disk = ffint_le_cpu32_ptr(trl->disk);
	*cdir_disk = ffint_le_cpu32_ptr(trl->cdir_disk);

	*cdir_size = ffint_le_cpu64_ptr(trl->cdir_size);
	*cdir_offset = ffint_le_cpu64_ptr(trl->cdir_offset);
	if (*cdir_offset + *cdir_size < *cdir_offset)
		return -1;
	return 0;
}

/** Write CDIR zip64 trailer
Return N of bytes written */
static inline int zip_cdirtrl64_write(void *buf, ffsize cdir_len, ffuint64 cdir_offset, ffuint items_num)
{
	if (buf == NULL)
		return sizeof(struct zip64_cdirtrl);

	struct zip64_cdirtrl *trl = (struct zip64_cdirtrl*)buf;
	ffmem_zero_obj(trl);
	ffmem_copy(trl, "PK\x06\x06", 4);
	*(ffuint64*)trl->size = ffint_le_cpu64(sizeof(struct zip64_cdirtrl) - 12);
	*(ffuint64*)trl->disk_ents = ffint_le_cpu64(items_num);
	*(ffuint64*)trl->total_ents = ffint_le_cpu64(items_num);
	*(ffuint64*)trl->cdir_size = ffint_le_cpu64(cdir_len);
	*(ffuint64*)trl->cdir_offset = ffint_le_cpu64(cdir_offset);
	return sizeof(struct zip64_cdirtrl);
}

/** Read CDIR zip64 trailer locator */
static inline int zip_cdirtrl64_loc_read(const void *buf, ffuint *cdir_trl_disk, ffuint *disks_number, ffuint64 *cdirtrl_off)
{
	const struct zip64_cdirtrl_loc *loc = (struct zip64_cdirtrl_loc*)buf;
	if (!!ffmem_cmp(loc->sig, "PK\x06\x07", 4))
		return -1;

	*cdir_trl_disk = ffint_le_cpu32_ptr(loc->cdir_trl_disk);
	*disks_number = ffint_le_cpu32_ptr(loc->disks_number);
	*cdirtrl_off = ffint_le_cpu64_ptr(loc->offset);
	return 0;
}

/** Write CDIR zip64 trailer locator */
static inline int zip_cdirtrl64_loc_write(void *buf, ffuint disks_number, ffuint cdir_trl_disk, ffuint64 cdir_zip64_off)
{
	if (buf == NULL)
		return sizeof(struct zip64_cdirtrl_loc);

	struct zip64_cdirtrl_loc *loc = (struct zip64_cdirtrl_loc*)buf;
	ffmem_copy(loc, "PK\x06\x07", 4);
	*(ffuint64*)loc->cdir_trl_disk = ffint_le_cpu32(cdir_trl_disk);
	*(ffuint64*)loc->offset = ffint_le_cpu64(cdir_zip64_off);
	*(ffuint64*)loc->disks_number = ffint_le_cpu64(disks_number);
	return sizeof(struct zip64_cdirtrl_loc);
}
