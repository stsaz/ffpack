/** ffpack: .zip libz decode filter
2020, Simon Zolin */

#include <zlib/zlib-ff.h>

static int _ffzipread_deflated_unpack(ffzipread *z, ffstr input, ffstr *output, ffsize *rd);

static int _ffzipread_deflated_init(ffzipread *z)
{
	if (z->lz == NULL) {
		z_conf zconf = {};
		if (0 != z_inflate_init(&z->lz, &zconf)) {
			z->error = "z_inflate_init()";
			return FFZIPREAD_ERROR;
		}
	} else {
		z_inflate_reset(z->lz);
	}
	z->unpack_func = _ffzipread_deflated_unpack;
	return 0;
}

static void _ffzipr_deflated_close(ffzipread *z)
{
	if (z->lz != NULL) {
		z_inflate_free(z->lz);
		z->lz = NULL;
	}
}

static int _ffzipread_deflated_unpack(ffzipread *z, ffstr input, ffstr *output, ffsize *rd)
{
	*rd = input.len;
	ffssize r = z_inflate(z->lz, input.ptr, rd, (char*)z->buf.ptr, z->buf.cap, 0);

	if (r == 0) {
		return 0xfeed;

	} else if (r == Z_DONE) {
		return 0xa11;

	} else if (r < 0) {
		z->error = "z_inflate()";
		return 0xbad;
	}

	z->buf.len += r;
	ffstr_set2(output, &z->buf);
	z->buf.len = 0;
	return 0;
}
