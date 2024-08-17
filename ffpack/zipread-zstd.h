/** ffpack: .zip zstd decode filter
2020, Simon Zolin */

#include <zstd/zstd-ff.h>

static int _ffzipr_zstd_unpack(ffzipread *z, ffstr input, ffstr *output, ffsize *rd);

static int _ffzipr_zstd_init(ffzipread *z)
{
	zstd_dec_conf zconf = {};
	if (0 != zstd_decode_init(&z->zstd, &zconf)) {
		z->error = "zstd_decode_init";
		return FFZIPREAD_ERROR;
	}
	z->unpack_func = _ffzipr_zstd_unpack;
	return 0;
}

static void _ffzipr_zstd_close(ffzipread *z)
{
	if (z->zstd != NULL) {
		zstd_decode_free(z->zstd);
		z->zstd = NULL;
	}
}

static int _ffzipr_zstd_unpack(ffzipread *z, ffstr input, ffstr *output, ffsize *rd)
{
	int done = (*rd == 0);
	zstd_buf in, out;
	zstd_buf_set(&in, input.ptr, input.len);
	zstd_buf_set(&out, z->buf.ptr, z->buf.cap);
	int r = zstd_decode(z->zstd, &in, &out);
	*rd = in.pos;

	if (r < 0) {
		ffmem_free(z->error_buf);
		z->error_buf = ffsz_allocfmt("zstd_decode: %s", zstd_error(r));
		z->error = z->error_buf;
		return 0xbad;
	}
	if (out.pos == 0) {
		if (done)
			return 0xa11;
		return 0xfeed;
	}

	z->buf.len = out.pos;
	ffstr_set2(output, &z->buf);
	z->buf.len = 0;
	return 0;
}
