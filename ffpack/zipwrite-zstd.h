/** ffpack: .zip zstd encode filter
2020, Simon Zolin */

#include <zstd/zstd-ff.h>

static void* _ffzipw_zstd_open(ffzipwrite *w, ffzipwrite_conf *conf)
{
	zstd_encoder *p = NULL;
	zstd_enc_conf zconf = {};
	zconf.level = conf->zstd_level;
	zconf.workers = conf->zstd_workers;
	if (0 != zstd_encode_init(&p, &zconf)) {
		w->error = "zstd_encode_init()";
		return NULL;
	}
	return p;
}

static void _ffzipw_zstd_close(void *obj, ffzipwrite *w)
{
	(void)w;
	if (obj == NULL) return;

	zstd_encode_free(obj);
}

static int _ffzipw_zstd_pack(void *obj, ffzipwrite *w, ffstr *input, ffstr *output)
{
	ffuint flags = (w->file_fin) ? ZSTD_FFINISH : 0;
	zstd_buf in, out;
	zstd_buf_set(&in, input->ptr, input->len);
	zstd_buf_set(&out, w->buf.ptr, w->buf.cap);
	int r = zstd_encode(obj, &in, &out, flags);
	ffstr_shift(input, in.pos);

	if (r < 0) {
		w->error = "zstd_encode";
		return 0xbad;
	}
	if (out.pos == 0) {
		if (w->file_fin && input->len == 0) {
			return 0xa11;
		}
		return 0xfeed;
	}

	w->buf.len = out.pos;
	ffstr_set2(output, &w->buf);
	w->buf.len = 0;
	return 0;
}

static const ffzipwrite_filter _ffzipw_zstd = {
	_ffzipw_zstd_open, _ffzipw_zstd_close, _ffzipw_zstd_pack
};
