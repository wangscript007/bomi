#include "videooutput.hpp"
#include "videoframe.hpp"
#include "videorendereritem.hpp"
#include "playengine.hpp"
#include "hwacc.hpp"

extern "C" {
#include <video/out/vo.h>
#include <video/vfcap.h>
#include <video/decode/dec_video.h>
#include <video/img_fourcc.h>
#include <mpvcore/m_option.h>
#include <video/mp_image.h>
#include <sub/sub.h>
}

struct cmplayer_vo_priv { VideoOutput *vo; char *address; };

static VideoOutput *priv(struct vo *vo) { return static_cast<cmplayer_vo_priv*>(vo->priv)->vo; }

#define OPT_BASE_STRUCT struct cmplayer_vo_priv
vo_driver create_driver() {
	static m_option options[2];
	memset(options, 0, sizeof(options));
	options[0].name = "address";
	options[0].flags = 0;
	options[0].defval = 0;
	options[0].offset = MP_CHECKED_OFFSETOF(OPT_BASE_STRUCT, address, char*);
	options[0].is_new_option = 1;
	options[0].type = &m_option_type_string;

	static vo_info_t info;
	info.name = "CMPlayer video output";
	info.short_name	= "null";
	info.author = "xylosper <darklin20@gmail.com>";
	info.comment = "";

	static vo_driver driver;
	driver.info = &info;
	driver.preinit = VideoOutput::preinit;
	driver.reconfig = VideoOutput::reconfig;
	driver.control = VideoOutput::control;
	driver.draw_osd = VideoOutput::drawOsd;
	driver.flip_page = VideoOutput::flipPage;
	driver.query_format = VideoOutput::queryFormat;
	driver.draw_image = VideoOutput::drawImage;
	driver.uninit = VideoOutput::uninit;
	driver.options = options;
	driver.priv_size = sizeof(cmplayer_vo_priv);
	return driver;
}

vo_driver video_out_null = create_driver();

struct VideoOutput::Data {
	VideoFormat format;
	VideoFrame frame;
	mp_osd_res osd;
	PlayEngine *engine = nullptr;
	bool flip = false, quit = false, upsideDown = false;
	VideoRendererItem *renderer = nullptr;
	bool formatChanged = false;
	int dest_w = 0, dest_h = 0;
	HwAcc *acc = nullptr;
	mp_csp colorspace = MP_CSP_AUTO;
	mp_csp_levels range = MP_CSP_LEVELS_AUTO;
};

VideoOutput::VideoOutput(PlayEngine *engine): d(new Data) {
	memset(&d->osd, 0, sizeof(d->osd));
	d->engine = engine;
}

VideoOutput::~VideoOutput() {}

void VideoOutput::setHwAcc(HwAcc *acc) {
	d->acc = acc;
}

int VideoOutput::preinit(struct vo *vo) {
	auto priv = static_cast<cmplayer_vo_priv*>(vo->priv);
	priv->vo = (VideoOutput*)(void*)(quintptr)QString::fromLatin1(priv->address).toULongLong();
	return 0;
}

void VideoOutput::output(const QImage &image) {
	if (d->renderer)
		d->renderer->present(image);
}

void VideoOutput::setRenderer(VideoRendererItem *renderer) {
	d->renderer = renderer;
}

const VideoFormat &VideoOutput::format() const {
	return d->format;
}

int VideoOutput::reconfig(vo *vo, mp_image_params *params, int flags) {
	auto v = priv(vo); auto d = v->d;
	d->upsideDown = flags & VOFLAG_FLIPPING;
	if (_Change(d->dest_w, params->d_w))
		d->formatChanged = true;
	if (_Change(d->dest_h, params->d_h))
		d->formatChanged = true;
	d->colorspace = params->colorspace;
	d->range = params->colorlevels;
	emit v->reconfigured();
	return 0;
}

HwAcc *VideoOutput::hwAcc() const {return d->acc;}

void VideoOutput::drawImage(struct vo *vo, mp_image *mpi) {
	auto v = priv(vo); auto d = v->d;
	auto img = mpi;
	if (d->acc && d->acc->imgfmt() == mpi->imgfmt)
		img = d->acc->getImage(mpi);
	if (d->formatChanged || (d->formatChanged = !d->format.compare(img)))
		emit v->formatChanged(d->format = VideoFormat(img, d->dest_w, d->dest_h));
	d->frame = VideoFrame(img, d->format);
	if (img != mpi) // new image is allocated, unref it
		mp_image_unrefp(&img);
	d->flip = true;
}

int VideoOutput::control(struct vo *vo, uint32_t req, void *data) {
	auto *v = priv(vo);
	switch (req) {
	case VOCTRL_REDRAW_FRAME:
		if (v->d->renderer)
			v->d->renderer->present(v->d->frame, v->d->upsideDown);
		return VO_TRUE;
	case VOCTRL_GET_HWDEC_INFO: {
		auto info = static_cast<mp_hwdec_info*>(data);
		info->vdpau_ctx = (mp_vdpau_ctx*)(void*)(v);
		return VO_TRUE;
	} default:
		return VO_NOTIMPL;
	}
}

void VideoOutput::drawOsd(struct vo *vo, struct osd_state *osd) {
	Data *d = priv(vo)->d;
	if (auto r = d->engine->videoRenderer()) {
		d->osd.w = d->format.width();
		d->osd.h = d->format.height();
		d->osd.display_par = 1.0;
		d->osd.video_par = vo->aspdat.par;
		static bool format[SUBBITMAP_COUNT] = {0, 0, 1, 0};
		osd_draw(osd, d->osd, osd->vo_pts, 0, format,  VideoRendererItem::drawMpOsd, r);
	}
}

void VideoOutput::flipPage(struct vo *vo) {
	Data *d = priv(vo)->d;
	if (!d->flip || d->quit)
		return;
	if (d->renderer) {
		d->renderer->present(d->frame, d->upsideDown, d->formatChanged);
		auto w = d->renderer->window();
		while (w && w->isVisible() && d->renderer->isFramePended() && !d->quit)
			PlayEngine::usleep(50);
		d->formatChanged = false;
	}
	d->flip = false;
}

void VideoOutput::quit() {
	d->quit = true;
}

int VideoOutput::queryFormat(struct vo */*vo*/, uint32_t format) {
	switch (format) {
	case IMGFMT_VDPAU:	case IMGFMT_VDA:	case IMGFMT_VAAPI:
	case IMGFMT_420P:
	case IMGFMT_NV12:
	case IMGFMT_NV21:
	case IMGFMT_YUYV:
	case IMGFMT_UYVY:
	case IMGFMT_BGRA:
	case IMGFMT_RGBA:
		return VFCAP_CSP_SUPPORTED | VFCAP_CSP_SUPPORTED_BY_HW | VFCAP_FLIP;
	default:
		return 0;
	}
}

#ifdef Q_OS_LINUX
vo_driver video_out_vaapi;
vo_driver video_out_vdpau;
#endif
