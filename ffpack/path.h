/** ffos: file-system path
2020, Simon Zolin
*/

#pragma once

#include <ffbase/string.h>

enum FFPATH_NORM {
	FFPATH_SLASH_ONLY = 1, // split path by slash (default on UNIX)
	FFPATH_SLASH_BACKSLASH = 2, // split path by both slash and backslash (default on Windows)
	FFPATH_FORCE_SLASH = 4, // convert '\\' to '/'
	FFPATH_FORCE_BACKSLASH = 8, // convert '/' to '\\'

	FFPATH_SIMPLE = 0x10, // convert to a simple path: {/abc, ./abc, ../abc} -> abc

	/* Handle disk drive letter, e.g. 'C:\' (default on Windows)
	C:/../a -> C:/a
	C:/a -> a (FFPATH_SIMPLE) */
	FFPATH_DISK_LETTER = 0x20,
	FFPATH_NO_DISK_LETTER = 0x40, // disable auto FFPATH_DISK_LETTER on Windows
};

/** Normalize file path
flags: enum FFPATH_NORM
Default behaviour:
  * split path by slash (on UNIX), by both slash and backslash (on Windows)
    override by FFPATH_SLASH_BACKSLASH or FFPATH_SLASH_ONLY
  * handle disk drive letter on Windows, e.g. 'C:\'
    override by FFPATH_DISK_LETTER or FFPATH_NO_DISK_LETTER
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
	int simplify = !!(flags & FFPATH_SIMPLE);
	int skip_disk = (flags & (FFPATH_DISK_LETTER | FFPATH_SIMPLE)) == (FFPATH_DISK_LETTER | FFPATH_SIMPLE);

	const char *slashes = (flags & FFPATH_SLASH_BACKSLASH) ? "/\\" : "/";
#ifdef FF_WIN
	if ((flags & (FFPATH_SLASH_BACKSLASH | FFPATH_SLASH_ONLY)) == 0)
		slashes = "/\\";

	if ((flags & (FFPATH_DISK_LETTER | FFPATH_NO_DISK_LETTER)) == 0)
		flags |= FFPATH_DISK_LETTER;
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
					} else if ((flags & FFPATH_DISK_LETTER)
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

			} else if (flags & FFPATH_SIMPLE) {
				continue;
			}
		}

		if (k + part.len + ((pos >= 0) ? 1 : 0) > cap)
			return -1;
		ffmem_copy(&dst[k], part.ptr, part.len);
		k += part.len;

		if (pos >= 0) {
			if (flags & FFPATH_FORCE_SLASH)
				dst[k] = '/';
			else if (flags & FFPATH_FORCE_BACKSLASH)
				dst[k] = '\\';
			else
				dst[k] = s2[pos];
			k++;
		}
	}

	return k;
}
