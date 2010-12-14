/*
 simulcam.cc -- atmosphere/telescope simulator camera
 Copyright (C) 2010 Tim van Werkhoven <t.i.m.vanwerkhoven@xs4all.nl>
 
 This file is part of FOAM.
 
 FOAM is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 2 of the License, or
 (at your option) any later version.
 
 FOAM is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with FOAM.	If not, see <http://www.gnu.org/licenses/>. 
 */

#include <unistd.h>
#include <gsl/gsl_matrix.h>

#include "io.h"
#include "config.h"
#include "pthread++.h"

#include "simseeing.h"
#include "camera.h"
#include "simulcam.h"

SimulCam::SimulCam(Io &io, foamctrl *ptc, string name, string port, Path &conffile):
Camera(io, ptc, name, SimulCam_type, port, conffile),
seeing(io, ptc, name + "-seeing", port, conffile),
shwfs(NULL)
{
	io.msg(IO_DEB2, "SimulCam::SimulCam()");
	
	// Setup seeing parameters
	Path wffile = ptc->confdir + cfg.getstring("wavefront_file");

	coord_t wind;
	wind.x = cfg.getint("windspeed.x", 16);
	wind.y = cfg.getint("windspeed.y", 16);

	SimSeeing::wind_t windtype;
	string windstr = cfg.getstring("windtype", "linear");
	if (windstr == "linear")
		windtype = SimSeeing::LINEAR;
	else
		windtype = SimSeeing::RANDOM;
	
	seeing.setup(wffile, res, wind, windtype);		
	
	cam_thr.create(sigc::mem_fun(*this, &SimulCam::cam_handler));
}

uint8_t *SimulCam::simul_wfs(gsl_matrix *wave_in) {
	if (!shwfs)
		io.msg(IO_ERR | IO_FATAL, "SimulCam::simul_wfs(): cannot simulate wavefront without Shwfs reference.");
	
	io.msg(IO_DEB2, "SimulCam::simul_wfs()");
	//! @todo Given a wavefront, image it through a system and return the resulting intensity pattern (i.e. an image).
	double min=0, max=0, fac;
	gsl_matrix_minmax(wave_in, &min, &max);
	fac = 255.0/(max-min);
	
	// Apply fourier transform to subimages here
	
	//(coord_t res, coord_t size, coord_t pitch, int xoff, coord_t disp, int &nsubap);
	// Convert frame to uint8_t, scale properly
	size_t cursize = wave_in->size1 * wave_in->size2  * (sizeof(uint8_t));
	if (out_size != cursize) {
		io.msg(IO_DEB2, "SimulCam::simul_wfs() reallocing memory, %zu != %zu", out_size, cursize);
		out_size = cursize;
		frame_out = (uint8_t *) realloc(frame_out, out_size);
	}
	
	for (size_t i=0; i<wave_in->size1; i++)
		for (size_t j=0; j<wave_in->size2; j++)
			frame_out[i*wave_in->size2 + j] = (uint8_t) ((wave_in->data[i*wave_in->tda + j] - min)*fac);
	
	return frame_out;
}


void SimulCam::simul_capture(uint8_t *frame) {
	// Apply offset and exposure here
	for (size_t p=0; p<res.x*res.y; p++)
		frame[p] = (uint8_t) clamp((int) ((frame[p] * exposure) + offset), 0, UINT8_MAX);
}


SimulCam::~SimulCam() {
	//! @todo stop simulator etc.
	io.msg(IO_DEB2, "SimulCam::~SimulCam()");
	cam_thr.cancel();
	cam_thr.join();
	
	mode = Camera::OFF;
}

// From Camera::
void SimulCam::cam_set_exposure(double value) {
	pthread::mutexholder h(&cam_mutex);
	exposure = value;
}

double SimulCam::cam_get_exposure() {
	pthread::mutexholder h(&cam_mutex);
	return exposure;
}

void SimulCam::cam_set_interval(double value) {
	pthread::mutexholder h(&cam_mutex);
	interval = value;
}

double SimulCam::cam_get_interval() {
	return interval;
}

void SimulCam::cam_set_gain(double value) {
	pthread::mutexholder h(&cam_mutex);
	gain = value;
}

double SimulCam::cam_get_gain() {
	return gain;
}

void SimulCam::cam_set_offset(double value) {
	pthread::mutexholder h(&cam_mutex);
	offset = value;
}

double SimulCam::cam_get_offset() {	
	return offset;
}

void SimulCam::cam_handler() { 
	pthread::setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS);
	//! @todo make this Mac compatible
#ifndef __APPLE__
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(1, &cpuset);
	pthread::setaffinity(&cpuset);
#endif
	
	while (true) {
		//! @todo Should mutex lock each time reading mode, or is this ok?
		switch (mode) {
			case Camera::RUNNING:
			{
				gsl_matrix_view wf = seeing.get_wavefront();
				io.msg(IO_DEB1, "SimulCam::cam_handler() RUNNING f@%p, f[100]: %g", 
							 wf.matrix.data, wf.matrix.data[100]);
				uint8_t *frame = simul_wfs(&(wf.matrix));
				
				simul_capture(frame);
				
				cam_queue(frame, frame);

				usleep(interval * 1000000);
				break;
			}
			case Camera::SINGLE:
			{
				io.msg(IO_DEB1, "SimSeeing::cam_handler() SINGLE");

				gsl_matrix_view wf = seeing.get_wavefront();
				uint8_t *frame = simul_wfs(&(wf.matrix));

				simul_capture(frame);
				
				cam_queue(frame, frame);
				
				usleep(interval * 1000000);

				mode = Camera::WAITING;
				break;
			}
			case Camera::OFF:
			case Camera::WAITING:
			case Camera::CONFIG:
			default:
				io.msg(IO_INFO, "SimSeeing::cam_handler() OFF/WAITING/UNKNOWN.");
				// We wait until the mode changed
				mode_mutex.lock();
				mode_cond.wait(mode_mutex);
				mode_mutex.unlock();
				break;
		}
	}
}

void SimulCam::cam_set_mode(mode_t newmode) {
	if (newmode == mode)
		return;
	
	switch (newmode) {
		case Camera::RUNNING:
		case Camera::SINGLE:
			// Start camera
			mode = newmode;
			mode_cond.broadcast();
			break;
		case Camera::WAITING:
		case Camera::OFF:
			// Stop camera
			mode = newmode;
			mode_cond.broadcast();
			break;
		case Camera::CONFIG:
			io.msg(IO_INFO, "SimSeeing::cam_set_mode(%s) mode not supported.", mode2str(newmode).c_str());
		default:
			io.msg(IO_WARN, "SimSeeing::cam_set_mode(%s) mode unknown.", mode2str(newmode).c_str());
			break;
	}
}

void SimulCam::do_restart() {
	io.msg(IO_WARN, "SimSeeing::do_restart() not implemented yet.");
}
