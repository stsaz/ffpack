/** ffos: file-system path
2020, Simon Zolin
*/

#pragma once

#include <ffbase/string.h>

/** Split into path and name so that "foo" is a name without path */
static inline ffssize _ffpack_path_splitpath_unix(const char *fn, ffsize len, ffstr *dir, ffstr *name)
{
	ffssize slash = ffs_rfindchar(fn, len, '/');
	if (slash < 0) {
		if (dir != NULL)
			dir->len = 0;
		if (name != NULL)
			ffstr_set(name, fn, len);
		return -1;
	}
	return ffs_split(fn, len, slash, dir, name);
}

/** Split into name and extension so that ".foo" is a name without extension */
static inline ffssize _ffpack_path_splitname(const char *fn, ffsize len, ffstr *name, ffstr *ext)
{
	ffssize dot = ffs_rfindchar(fn, len, '.');
	if (dot <= 0) {
		if (name != NULL)
			ffstr_set(name, fn, len);
		if (ext != NULL)
			ext->len = 0;
		return 0;
	}
	return ffs_split(fn, len, dot, name, ext);
}

enum _FFPACK_PATH_NORM {
	_FFPACK_PATH_SLASH_ONLY = 1, // split path by slash (default on UNIX)
	_FFPACK_PATH_SLASH_BACKSLASH = 2, // split path by both slash and backslash (default on Windows)
	_FFPACK_PATH_FORCE_SLASH = 4, // convert '\\' to '/'
	_FFPACK_PATH_FORCE_BACKSLASH = 8, // convert '/' to '\\'

	_FFPACK_PATH_SIMPLE = 0x10, // convert to a simple path: {/abc, ./abc, ../abc} -> abc

	/* Handle disk drive letter, e.g. 'C:\' (default on Windows)
	C:/../a -> C:/a
	C:/a -> a (_FFPACK_PATH_SIMPLE) */
	_FFPACK_PATH_DISK_LETTER = 0x20,
	_FFPACK_PATH_NO_DISK_LETTER = 0x40, // disable auto _FFPACK_PATH_DISK_LETTER on Windows
};

/** Normalize file path
flags: enum _FFPACK_PATH_NORM
Default behaviour:
  * split path by slash (on UNIX), by both slash and backslash (on Windows)
    override by _FFPACK_PATH_SLASH_BACKSLASH or _FFPACK_PATH_SLASH_ONLY
  * handle disk drive letter on Windows, e.g. 'C:\'
    override by _FFPACK_PATH_DISK_LETTER or _FFPACK_PATH_NO_DISK_LETTER
  * skip "." components, unless leading
    ./a/b -> ./a/b
    a/./b -> a/b
  * handle ".." components
    a/../b -> b
    ../a/b -> ../a/b
    ./../a -> ../a
    /../a -> /a
Return N of bytes written
  <0 on error */
static inline ffssize _ffpack_path_normalize(char *dst, ffsize cap, const char *src, ffsize len, ffuint flags)
{
	ffsize k = 0;
	ffstr s, part;
	ffstr_set(&s, src, len);
	int simplify = !!(flags & _FFPACK_PATH_SIMPLE);
	int skip_disk = (flags & (_FFPACK_PATH_DISK_LETTER | _FFPACK_PATH_SIMPLE)) == (_FFPACK_PATH_DISK_LETTER | _FFPACK_PATH_SIMPLE);

	const char *slashes = (flags & _FFPACK_PATH_SLASH_BACKSLASH) ? "/\\" : "/";
#ifdef FF_WIN
	if ((flags & (_FFPACK_PATH_SLASH_BACKSLASH | _FFPACK_PATH_SLASH_ONLY)) == 0)
		slashes = "/\\";

	if ((flags & (_FFPACK_PATH_DISK_LETTER | _FFPACK_PATH_NO_DISK_LETTER)) == 0)
		flags |= _FFPACK_PATH_DISK_LETTER;
#endif

	while (s.len != 0) {
		const char *s2 = s.ptr;
		ffssize pos = ffstr_splitbyany(&s, slashes, &part, &s);

		if (simplify) {
			if (part.len == 0
				|| ffstr_eqcz(&part, "."))
				continue; // merge slashes, skip dots

			if (skip_disk) {
				skip_disk = 0;
				if (*ffstr_last(&part) == ':')
					continue; // skip 'c:/'
			}

		} else {
			// allow leading slash or dot
			simplify = 1;
		}

		if (ffstr_eqcz(&part, "..")) {
			if (k != 0) {
				ffssize slash = ffs_rfindany(dst, k - 1, slashes, ffsz_len(slashes));
				ffstr prev;
				if (slash < 0) {
					ffstr_set(&prev, dst, k - 1);
					if (ffstr_eqcz(&prev, ".")) {
						k = 0; // "./" -> "../"
					} else if (ffstr_eqcz(&prev, "..")) {
						// "../" -> "../../"
					} else if (k == 1) {
						continue; // "/" -> "/"
					} else if ((flags & _FFPACK_PATH_DISK_LETTER)
						&& *ffstr_last(&prev) == ':') {
						continue; // "c:/" -> "c:/"
					} else {
						k = 0; // "a/" -> ""
						continue;
					}

				} else {
					slash++;
					ffstr_set(&prev, &dst[slash], k - 1 - slash);
					if (ffstr_eqcz(&prev, "..")) {
						// ".../../" -> ".../../../"
					} else {
						k = slash; // ".../a/" -> ".../"
						continue;
					}
				}

			} else if (flags & _FFPACK_PATH_SIMPLE) {
				continue;
			}
		}

		if (k + part.len + ((pos >= 0) ? 1 : 0) > cap)
			return -1;
		ffmem_copy(&dst[k], part.ptr, part.len);
		k += part.len;

		if (pos >= 0) {
			if (flags & _FFPACK_PATH_FORCE_SLASH)
				dst[k] = '/';
			else if (flags & _FFPACK_PATH_FORCE_BACKSLASH)
				dst[k] = '\\';
			else
				dst[k] = s2[pos];
			k++;
		}
	}

	return k;
}
