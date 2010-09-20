/*
    fw1394cam.cc -- IEEE1394 Digital Camera handler for the Dutch Open Telescope
    Copyright (C) 2006-2007  Guus Sliepen <G.Sliepen@astro.uu.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <string>
#include <vector>

#include "io.h"
#include "config.h"
#include "dc1394++.h"
#include "pthread++.h"

#include "camera.h"
#include "fw1394cam.h"

using namespace std;

FW1394Camera::FW1394Camera(Io &io, foamctrl *ptc, string name, string port, string conffile):
Camera(io, ptc, name, FW1394cam_type, port, conffile) 
{
	io.msg(IO_DEB2, "Camera::Camera()");
	
	// Init hardware
	std::vector<dc1394::camera *> cameras = _dc1394.find_cameras();
	
	if(!cameras.size())
		throw exception("No IIDC cameras found.");
	if (cameras.size() != 1)
		io.msg(IO_WARN, "Found multiple IIDC cameras, using the first one.");
	
	camera = cameras[0];
	camera->set_transmission(false);
	camera->set_power(true);
	mode = Camera::OFF;
	
	// Set iso-speed: the transmission speed in megabit per second (1600 and 3200 only for future implementations)
	int iso_speed = cfg.getint(name+".iso_speed", 400);
	if (!_dc1394.check_isospeed((int) iso_speed)) {
		io.msg(IO_WARN, "iso_speed should be 2^n*100 for 0<=n<5! Defaulting to 400.");
		iso_speed = 400;
	}
	camera->set_iso_speed((dc1394::iso_speed) _dc1394.iso_speed_p.getenum((int) iso_speed));

	// Set video mode, either fixed format or free FORMAT_7 mode. Default to VGA mono 8 bit.
	std::string vid_mode = cfg.getstring(name+".video_mode", "VIDEO_MODE_640x480_MONO8");
	camera->set_video_mode((dc1394::video_mode) _dc1394.video_mode_p.getenum(vid_mode));

	// Set framerate
	double fps = cfg.getdouble(name+".framerate", 30.);
	if (!_dc1394.check_framerate(fps)) {
		io.msg(IO_WARN, "Framerate should be 2^n*1.875 for 0<=n<7! Defaulting to 15fps.");
		fps = 15.;
	}
	camera->set_framerate((dc1394::framerate) _dc1394.framerate_p.getenum(fps));
	
	// What's this?
	camera->set_control_register(0x80c, 0x82040040);
	// What's this?
	camera->capture_setup(nframes + 10);
	camera->set_transmission();
	
	res.x = cfg.getint(name+".width", 640);
	res.y = cfg.getint(name+".height", 480);
	depth = cfg.getint(name+".depth", 8);
	dtype = DATA_UINT8;
	

	exposure = cam_get_exposure(); 
	interval = cam_get_interval();
	gain = cam_get_gain();
	offset = cam_get_offset();
	
	cam_thr.create(sigc::mem_fun(*this, &FW1394Camera::cam_handler));
}

FW1394Camera::~FW1394Camera() {
	cam_thr.cancel();
	cam_thr.join();

	camera->set_transmission(false);
	camera->capture_stop();
	camera->set_power(false);	
}

// From Camera::
void FW1394Camera::cam_set_exposure(double value) {
	pthread::mutexholder h(&cam_mutex);
	camera->set_feature(dc1394::FEATURE_EXPOSURE, max((uint32_t)(value * 30 * 512), (uint32_t)511));
	exposure = cam_get_exposure();
}

double FW1394Camera::cam_get_exposure() {
	pthread::mutexholder h(&cam_mutex);
	return camera->get_feature(dc1394::FEATURE_EXPOSURE) / 30.0 / 512;
}

void FW1394Camera::cam_set_interval(double value) {
}

double FW1394Camera::cam_get_interval() {
	pthread::mutexholder h(&cam_mutex);
	return 1.0 / 1.875 / (1 << (camera->get_framerate() - 32));
}

void FW1394Camera::cam_set_gain(double value) {
	pthread::mutexholder h(&cam_mutex);
	camera->set_feature(dc1394::FEATURE_GAIN, (uint32_t)value);
	gain = cam_get_gain();
}

double FW1394Camera::cam_get_gain() {
	pthread::mutexholder h(&cam_mutex);
	return camera->get_feature(dc1394::FEATURE_GAIN);
}

void FW1394Camera::cam_set_offset(double value) {
	pthread::mutexholder h(&cam_mutex);
	camera->set_feature(dc1394::FEATURE_BRIGHTNESS, (uint32_t)value + 256);
	offset = cam_get_offset();
}

double FW1394Camera::cam_get_offset() {	
	pthread::mutexholder h(&cam_mutex);
	return camera->get_feature(dc1394::FEATURE_BRIGHTNESS) - 256;
}

void FW1394Camera::cam_handler() {
	pthread::setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS);
	// TODO: make this Mac compatible
#ifndef __APPLE__
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(1, &cpuset);
	pthread::setaffinity(&cpuset);
#endif
	
	while(true) {
		dc1394::frame *frame = camera->capture_dequeue(dc1394::CAPTURE_POLICY_WAIT);
		if(!frame) {
			timeouts++;
			usleep(50000);
			continue;
		}
		
		dc1394::frame *oldframe = (dc1394::frame *)cam_queue(frame, frame->image);
		if (oldframe)
			camera->capture_enqueue(oldframe);
	}
}

void FW1394Camera::cam_set_mode(mode_t newmode) {
	// TODO
	if (newmode == mode)
		return;
	io.msg(IO_WARN, "FW1394::cam_set_mode() not implemented yet.");
}

void FW1394Camera::do_restart() {
	// TODO
	io.msg(IO_WARN, "FW1394::do_restart() not implemented yet.");
}
