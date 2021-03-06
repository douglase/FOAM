/*
 wfs.cc -- a wavefront sensor abstraction class
 Copyright (C) 2009--2011 Tim van Werkhoven <werkhoven@strw.leidenuniv.nl>
 
 This file is part of FOAM.
 
 FOAM is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 2 of the License, or
 (at your option) any later version.
 
 FOAM is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with FOAM.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <string>
#include <gsl/gsl_vector.h>

#include "types.h"
#include "io.h"

//#include "zernike.h"
#include "wfs.h"
#include "camera.h"

using namespace std;

// Constructor / destructors

Wfs::Wfs(Io &io, foamctrl *const ptc, const string name, const string port, Path const &conffile, Camera &wfscam, const bool online):
Device(io, ptc, name, wfs_type, port, conffile, online),
//zernbasis(io, 0, wfscam.get_width()),
cam(wfscam)
{	
	io.msg(IO_DEB2, "Wfs::Wfs()");
	init();
}

Wfs::Wfs(Io &io, foamctrl *const ptc, const string name, const string type, const string port, Path const &conffile, Camera &wfscam, const bool online):
Device(io, ptc, name, wfs_type + "." + type, port, conffile, online),
//zernbasis(io, 0, wfscam.get_width()),
cam(wfscam)
{	
	io.msg(IO_DEB2, "Wfs::Wfs()");
	init();
}

void Wfs::init() {
  add_cmd("calibrate");
  add_cmd("measure");
  
	add_cmd("measuretest");
	add_cmd("get modes");
	add_cmd("get basis");
	add_cmd("get calib");
	add_cmd("get camera");
}

Wfs::~Wfs() {
	io.msg(IO_DEB2, "Wfs::~Wfs()");
	
	// De-allocate memory that might have been allocated in derived classes (Shwfs)
	gsl_vector_float_free(wf.wfamp);
	gsl_vector_float_free(wf.wf_full);
}

void Wfs::on_message(Connection *const conn, string line) {
	string orig = line;
	string command = popword(line);
	bool parsed = true;
	
	if (command == "measuretest") {			// measuretest
		// Specifically call Wfs::measure() for fake 
		Wfs::measure();
		get_var(conn, "measuretest", "ok measuretest");
	} else if (command == "calibrate") {  // calibrate
		calibrate();
		conn->write("ok calibrate");
	} else if (command == "measure") {  // measure
		if (!measure(NULL))
			conn->write("error measure :error in measure()");
		else 
			conn->write("ok measure");
	} else if (command == "get") {			// get ...
		string what = popword(line);
		
		if (what == "modes") {						// get modes
			string moderep = "";
			for (int n=0; n<wf.nmodes; n++)
				moderep += format("%4f ", gsl_vector_float_get(wf.wfamp, n));

			get_var(conn, "modes", format("ok modes %d %s", wf.nmodes, moderep.c_str()));
		} else if (what == "camera") {		// get camera
			get_var(conn, "camera", "ok camera " + cam.name);
		} else if (what == "calib") {			// get calib
			get_var(conn, "calib", format("ok calib %d", get_calib()));
		} else if (what == "basis") {			// get basis
			string tmp;
			if (wf.basis == ZERNIKE) tmp = "zernike";
			else if (wf.basis == KL) tmp = "kl";
			else if (wf.basis == MIRROR) tmp = "mirror";
			else if (wf.basis == SENSOR) tmp = "sensor";
			else tmp = "unknown";
			get_var(conn, "basis", format("ok basis %s", tmp.c_str()));
 		}
		else
			parsed = false;
	}
	else
		parsed = false;
	
	// If not parsed here, call parent
	if (parsed == false)
		Device::on_message(conn, orig);
}

Wfs::wf_info_t* Wfs::measure(Camera::frame_t *) {
	io.msg(IO_DEB2, "Wfs::measure(), filling random");
	if (!wf.wfamp) {
		wf.nmodes = 16;
		wf.wfamp = gsl_vector_float_alloc(wf.nmodes);
		wf.basis = SENSOR;
	}
	
	int randoff = (int) (drand48()*wf.nmodes);
	for (int n=0; n<wf.nmodes; n++)
		gsl_vector_float_set(wf.wfamp, n, (n-randoff)*2.0/(wf.nmodes)-1.0);
	
	return &wf;
}

int Wfs::calibrate() {
	io.msg(IO_DEB2, "Wfs::calibrate()");
	
	set_calib(true);
	return 0;
}

