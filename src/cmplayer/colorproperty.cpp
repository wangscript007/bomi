#include "colorproperty.hpp"
#include <QVector3D>
#include <QtMath>

static QMatrix4x4 matYCbCrToRgb(double kb, double kr, double y1, double y2, double c1, double c2) {
	const double dy = 1.0/(y2-y1);
	const double dc = 2.0/(c2-c1);
	const double kg = 1.0 - kb - kr;
	QMatrix4x4 mat;
	mat(0, 0) = dy; mat(0, 1) = 0.0;                mat(0, 2) = (1.0 - kr)*dc;
	mat(1, 0) = dy; mat(1, 1) = -dc*(1.0-kb)*kb/kg; mat(1, 2) = -dc*(1.0-kr)*kr/kg;
	mat(2, 0) = dy; mat(2, 1) = dc*(1-kb);          mat(2, 2) = 0.0;
	return mat;
}

static QMatrix4x4 matRgbToYCbCr(double kb, double kr, double y1, double y2, double c1, double c2) {
	const double dy = (y2-y1);
	const double dc = (c2-c1)/2.0;
	const double kg = 1.0 - kb - kr;
	QMatrix4x4 mat;
	mat(0, 0) = dy*kr;              mat(0, 1) = dy*kg;              mat(0, 2) = dy*kb;
	mat(1, 0) = -dc*kr/(1.0 - kb);  mat(1, 1) = -dc*kg/(1.0-kb);    mat(1, 2) = dc;
	mat(2, 0) = dc;                 mat(2, 1) = -dc*kg/(1.0-kr);    mat(2, 2) = -dc*kb/(1.0 - kr);
	return mat;
}

static QMatrix4x4 matSHC(double s, double h, double c) {
	s = qBound(0.0, s + 1.0, 2.0);
	c = qBound(0.0, c + 1.0, 2.0);
	h = qBound(-M_PI, h*M_PI, M_PI);
	QMatrix4x4 mat;
	mat(0, 0) = 1.0;    mat(0, 1) = 0.0;        mat(0, 2) = 0.0;
	mat(1, 0) = 0.0;    mat(1, 1) = s*qCos(h);  mat(1, 2) = s*qSin(h);
	mat(2, 0) = 0.0;    mat(2, 1) = -s*qSin(h); mat(2, 2) = s*qCos(h);
	mat *= c;
	return mat;
}

						//  y1,y2,c1,c2
const float ranges[MP_CSP_LEVELS_COUNT][4] = {
	{	  0.0,	      1.0,      0.0,         1.0}, //MP_CSP_LEVELS_AUTO
	{16./255.,	235./255.,	16./255,	240./255}, // MP_CSP_LEVELS_TV
	{	  0.0,	      1.0,      0.0,         1.0}  // MP_CSP_LEVELS_PC
};

const float specs[MP_CSP_COUNT][2] = {
	{0.0, 0.0}, // MP_CSP_AUTO,
	{0.114,0.299}, // MP_CSP_BT_601,
	{0.0722,0.2126}, // MP_CSP_BT_709,
	{0.087, 0.212}//		MP_CSP_SMPTE_240M,
};

//		MP_CSP_RGB,
//		MP_CSP_XYZ,
//		MP_CSP_YCGCO,

QMatrix4x4 ColorProperty::matrix(mp_csp colorspace, mp_csp_levels levels) const {
	const float *spec = specs[colorspace];
	const float *range = ranges[levels];
	switch (colorspace) {
	case MP_CSP_RGB:
		spec = specs[MP_CSP_BT_601];
		range = ranges[MP_CSP_LEVELS_TV];
	case MP_CSP_BT_601:
	case MP_CSP_BT_709:
	case MP_CSP_SMPTE_240M:
		break;
	default:
		return QMatrix4x4();
	}
	switch (levels) {
	case MP_CSP_LEVELS_TV:
	case MP_CSP_LEVELS_PC:
		break;
	default:
		return QMatrix4x4();
	}
	const float kb = spec[0], kr = spec[1];
	const float y1 = range[0], y2 = range[1], c1 = range[2], c2 = range[3];
	QMatrix4x4 mat = matYCbCrToRgb(kb, kr, y1, y2, c1, c2)*matSHC(saturation(), hue(), contrast());
	if (colorspace == MP_CSP_RGB)
		mat *= matRgbToYCbCr(kb, kr, y1, y2, c1, c2);
	mat(0, 3) = mat(1, 3) = mat(2, 3) = qBound(-1.0, brightness(), 1.0)/(y2 - y1);
	if (colorspace != MP_CSP_RGB) {
		mat(3, 0) = y1;	mat(3, 1) = mat(3, 2) = (c1 + c2)/2.0f;
	}
	return mat;
}

