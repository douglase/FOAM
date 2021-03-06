/*
 wfc.cc -- a wavefront corrector base class
 Copyright (C) 2011 Tim van Werkhoven <werkhoven@strw.leidenuniv.nl>
 
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
#include <stdint.h>
#include <sys/types.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_vector.h>

#include "utils.h"
#include "types.h"
#include "config.h"
#include "path++.h"
#include "imgdata.h"

#include "foamctrl.h"
#include "devices.h"
#include "wfc.h"

Wfc::Wfc(Io &io, foamctrl *const ptc, const string name, const string type, const string port, Path const & conffile, const bool online):
Device(io, ptc, name, wfc_type + "." + type, port, conffile, online),
real_nact(0), virt_nact(0), actmap_mat(NULL),
have_waffle(false),
offset_str("0"), maxact(1.0) {
	io.msg(IO_DEB2, "Wfc::Wfc()");

	try {
		// Get waffle pattern actuators
		str_waffle_odd = cfg.getstring("waffle_odd", "");
		str_waffle_even = cfg.getstring("waffle_even", "");
		
		// Get actuation mapping, relative to data dir
		actmap_f = cfg.getstring("actmapfile", "");
		io.msg(IO_DEB1, "Wfc::Wfc(): Got actmap file: %s", actmap_f.c_str());
		
	} catch (std::runtime_error &e) {
		io.msg(IO_ERR | IO_FATAL, "Wfc: problem with configuration file: %s", e.what());
	} catch (...) { 
		io.msg(IO_ERR | IO_FATAL, "Wfc: unknown error at initialisation.");
		throw;
	}
	
	// Load actuation mapping matrix
	if (actmap_f != "") {
		actmap_f = ptc->datadir + actmap_f;
		actmap_mat = load_actmap_matrix(actmap_f);
		virt_nact = actmap_mat->size2;
	}
	
	add_cmd("set gain");
	add_cmd("get gain");
	add_cmd("get nact");
	//! @todo	add_cmd("get real_nact");
	add_cmd("get ctrl");
	add_cmd("get offset");
	add_cmd("set offset");

	add_cmd("get maxact");
	add_cmd("set maxact");

	add_cmd("act waffle");
	add_cmd("act random");
	add_cmd("act all");
	add_cmd("act one");
	add_cmd("act vec");
}

Wfc::~Wfc() {
	io.msg(IO_DEB2, "Wfc::~Wfc()");
	
	// WFC control vectors
	gsl_vector_float_free(ctrlparams.ctrl_vec);
	gsl_vector_float_free(ctrlparams.offset);

	gsl_vector_float_free(ctrlparams.target);
	gsl_vector_float_free(ctrlparams.err);
	gsl_vector_float_free(ctrlparams.prev);
	gsl_vector_float_free(ctrlparams.pid_int);
	
	// Work vector (same size as target, virt_nact)
	gsl_vector_float_free(workvec);

	// Actuation mapping matrix
	gsl_matrix_float_free(actmap_mat);
}

gsl_matrix_float *Wfc::load_actmap_matrix(Path filepath) {
	
	io.msg(IO_DEB2, "Wfc::load_actmap_matrix(), file=%s", filepath.c_str());
	
	if (!filepath.r())
		return NULL;
	
	ImgData actmap_tmp(io, filepath, ImgData::AUTO);
	if (actmap_tmp.geterr() != ImgData::ERR_NO_ERROR)
		throw exception(format("Wfc::load_actmap_matrix() ImgData returned an error: %d", actmap_tmp.geterr()));
	
	io.msg(IO_XNFO, "Wfc::load_actmap_matrix() got data: %zux%zux%d",
				 actmap_tmp.getwidth(), actmap_tmp.getheight(), actmap_tmp.getbpp());
	
	// We own the data now, we need to free it as well
	gsl_matrix *actmap_dbl = actmap_tmp.as_GSL(true);

	if (!actmap_dbl)
		throw std::runtime_error("Wfc::load_actmap_matrix() Could not load actuation matrix.");

	// Convert from double to float
	gsl_matrix_float *actmap_flt = gsl_matrix_float_calloc(actmap_dbl->size1, actmap_dbl->size2);
	
	// Copy data
	for (size_t i=0; i<actmap_flt->size1; i++)
		for (size_t j=0; j<actmap_flt->size2; j++)
			gsl_matrix_float_set(actmap_flt, i, j, gsl_matrix_get(actmap_dbl, i, j));
		
	// Free actmap as double
	gsl_matrix_free(actmap_dbl);
	
	return actmap_flt;
}

string Wfc::ctrl_as_str(const char *fmt) const {
	if (!ctrlparams.target)
		return "0";
	
	// Init string with number of values
	string ctrl_str;
	ctrl_str = format("%zu", ctrlparams.target->size);
	
	// Add all values seperated by commas
	for (size_t i=0; i < ctrlparams.target->size; i++)
		ctrl_str += ", " + format(fmt, gsl_vector_float_get(ctrlparams.target, i));
	
	return ctrl_str;
}

int Wfc::ctrl_apply_actmap() {
	// This should do: ctrl_vec = actmat . (target + offset)
	// If actmat = 0: ctrl_vec = target + offset

	// Copy ctrlparams.target to workvec
	gsl_blas_scopy(ctrlparams.target, workvec);

	// Compute workvec += offset, which gives workvec = ctrlparams.target + offset
	gsl_blas_saxpy(1.0, ctrlparams.offset, workvec);
	
	// Compute ctrl_vec = actmat . workvec
	if (actmap_mat)
		gsl_blas_sgemv(CblasNoTrans, 1.0, actmap_mat, workvec, 0.0, ctrlparams.ctrl_vec);
	else
		gsl_blas_scopy(workvec, ctrlparams.ctrl_vec);
	
	return 0;
}

int Wfc::update_control(const gsl_vector_float *const error, const gain_t g, const float retain) {
	// gsl_blas_saxpy(alpha, x, y): compute the sum y = \alpha x + y for the vectors x and y.
	// gsl_blas_sscal(alpha, x): rescale the vector x by the multiplicative factor alpha. 
	if (!get_calib())
		calibrate();
	
	// Copy error to our memory (ctrlparams.err), unless it is the same memory
	if (error != ctrlparams.err)
		gsl_blas_scopy(error, ctrlparams.err);
	
	// Copy current target to ctrlparams.prev
	gsl_blas_scopy(ctrlparams.target, ctrlparams.prev);
	
	// If retain is unequal to 1, apply
	if (retain != 1.0) {
		if (retain == 0)
			gsl_vector_float_set_zero(ctrlparams.target);
		else
			gsl_blas_sscal(retain, ctrlparams.target);
	}
	
	// If proportional gain is unequal zero, use it
	if (g.p != 0) {
		// ctrlparams.target += pid->p * ctrlparams.err
		gsl_blas_saxpy(g.p, ctrlparams.err, ctrlparams.target);
	}
	
	//! @todo Move this somewhere deeper, clamping should always happen?
	// Clamp WFC control values if requested
	for (size_t actid=0; actid<ctrlparams.target->size; actid++) {
		float thisact = gsl_vector_float_get(ctrlparams.target, actid);
		gsl_vector_float_set(ctrlparams.target, actid, clamp(thisact, -maxact, maxact));
	}
	
	//! @todo Extend update_control() with (P)ID control
#if 0
	// If integral gain is unequal zero, use it
	if (g.i != 0) {
		// ctrlparams.pid_int += error
		gsl_blas_saxpy(1.0, error, ctrlparams.pid_int);
		// check if ctrlparams.pid_int is still within range, clip if necessary
		
		// ctrlparams.target += pid->i * ctrlparams.pid_int
		gsl_blas_saxpy(g.i, ctrlparams.pid_int, ctrlparams.target);
	}
	
	// If differential gain is unequal zero, use it
	// This is the same as proportional control?
	if (g.d != 0) {
		// ctrlparams.target += pid->d + ctrlparams.err
		gsl_blas_saxpy(g.d, ctrlparams.err, ctrlparams.target);
	}
#endif
	
	return ctrl_apply_actmap();
}

int Wfc::set_control(const gsl_vector_float *const newctrl) {
	if (!get_calib())
		calibrate();
	
	// Copy new target to ctrlparams.target
	gsl_blas_scopy(newctrl, ctrlparams.target);
	return ctrl_apply_actmap();
}

int Wfc::set_control(const float val) {
	if (!get_calib())
		calibrate();
	
	// Set all actuators to 'val'
	gsl_vector_float_set_all(ctrlparams.target, val);
	return ctrl_apply_actmap();
}

int Wfc::set_control_act(const float val, const size_t act_id) {
	if (!get_calib())
		calibrate();
	
	// Set actuator 'act_id' to 'val'
	gsl_vector_float_set(ctrlparams.target, act_id, val);
	return ctrl_apply_actmap();
}

float Wfc::get_control_act(const size_t act_id) {
	if (!get_calib())
		calibrate();
	
	// return actuator 'act_id' value
	return gsl_vector_float_get(ctrlparams.target, act_id);
}

int Wfc::set_wafflepattern(const float val) {
	if (!have_waffle) {
		io.msg(IO_WARN, "Wfc::set_wafflepattern() no waffle!");
		return 1;
	}
	
	if (!get_calib())
		calibrate();
	
	// Set all to zero first
	gsl_vector_float_set_zero(ctrlparams.ctrl_vec);
	
	// Set 'even' actuators to +val, set 'odd' actuators to -val:
    //! @todo Check bounds here, waffle_even.at(idx) can be higher than ctrl_vec length 
	for (size_t idx=0; idx < waffle_even.size(); idx++)
		gsl_vector_float_set(ctrlparams.ctrl_vec, waffle_even.at(idx), val);

	for (size_t idx=0; idx < waffle_odd.size(); idx++)
		gsl_vector_float_set(ctrlparams.ctrl_vec, waffle_odd.at(idx), -val);
	
	return 0;
}

int Wfc::set_randompattern(const float maxval) {
	if (!get_calib())
		calibrate();

	// Set all to zero first
	gsl_vector_float_set_zero(ctrlparams.target);
	
	for (size_t idx=0; idx < ctrlparams.target->size; idx++)
		gsl_vector_float_set(ctrlparams.target, idx, (drand48()*2.0-1.0)*maxval);
	
	return ctrl_apply_actmap();
}

int Wfc::calibrate() {
	// Check if we have an actuation map
	if (actmap_mat == NULL)
		virt_nact = real_nact;
	
	// Parse waffle pattern strings (only here because otherwise real_nact is 0)
	parse_waffle(str_waffle_odd, str_waffle_even);
	
	// Allocate memory for control command
	gsl_vector_float_free(ctrlparams.target);
	ctrlparams.target = gsl_vector_float_calloc(virt_nact);
	gsl_vector_float_free(ctrlparams.err);
	ctrlparams.err = gsl_vector_float_calloc(virt_nact);
	gsl_vector_float_free(ctrlparams.prev);
	ctrlparams.prev = gsl_vector_float_calloc(virt_nact);
	gsl_vector_float_free(ctrlparams.pid_int);
	ctrlparams.pid_int = gsl_vector_float_calloc(virt_nact);

	// Memory for offset control
	gsl_vector_float_free(ctrlparams.offset);
	ctrlparams.offset = gsl_vector_float_calloc(virt_nact);

	// Memory for output control
	gsl_vector_float_free(ctrlparams.ctrl_vec);
	ctrlparams.ctrl_vec = gsl_vector_float_calloc(real_nact);
	
	// Work vector (same size as target, virt_nact)
	gsl_vector_float_free(workvec);
	workvec = gsl_vector_float_calloc(virt_nact);
	
	set_calib(true);
	return 0;
}

int Wfc::reset() {
	if (!get_calib())
		calibrate();

	gsl_vector_float_set_zero(ctrlparams.ctrl_vec);
	
	actuate();
	return 0;
}

void Wfc::loosen(const double amp, const int niter, const double delay) {
	for (int iter=0; iter<niter; iter++) {
		set_control(-1.0*amp);
		actuate();
		set_control(1.0*amp);
		actuate();
		usleep(delay * 1E6);
	}
}

void Wfc::parse_waffle(string &odd, string &even) {
	io.msg(IO_DEB2, "Wfc::parse_waffle(odd=%s, even=%s)", odd.c_str(), even.c_str());
	if (odd.size() <= 0 || even.size() <= 0)
		return;

	string thisact;
	int thisact_i=0;
	
	string odd_act_l = "";
	string even_act_l = "";
	
	while (odd.size() > 0) {
		thisact = popword(odd, " \t\n,");
		thisact_i = strtol(thisact.c_str(), (char **) NULL, 10);
		if (thisact_i >= 0 && thisact_i <= real_nact) {
			waffle_odd.push_back(thisact_i);
			odd_act_l += format(" %d", thisact_i);
		}
		else {
			io.msg(IO_WARN, "Wfc::parse_waffle() could not parse odd waffle actuator %d!", thisact_i);
			break;
		}
	}
	
	while (even.size() > 0) {
		thisact = popword(even, " \t\n,");
		thisact_i = strtol(thisact.c_str(), (char **) NULL, 10);
		if (thisact_i >= 0 && thisact_i <= real_nact) {
			waffle_even.push_back(thisact_i);
			even_act_l += format(" %d", thisact_i);
		}
		else {
			io.msg(IO_WARN, "Wfc::parse_waffle() could not parse even waffle actuator %d!", thisact_i);
			break;
		}
	}
	
	io.msg(IO_DEB2, "Wfc::parse_waffle() odd = %s", odd_act_l.c_str());
	io.msg(IO_DEB2, "Wfc::parse_waffle() even = %s", even_act_l.c_str());

	
	have_waffle = true;
}

void Wfc::on_message(Connection *const conn, string line) { 
	string orig = line;
	string command = popword(line);
	bool parsed = true;
	
	if (command == "get") {							// get ...
		string what = popword(line);
		
		if (what == "gain") {							// get gain
			conn->addtag("gain");
			conn->write(format("ok gain %g %g %g", ctrlparams.gain.p, ctrlparams.gain.i, ctrlparams.gain.d));
		} else if (what == "nact") {			// get nact
			conn->write(format("ok nact %d", get_nact()));
		} else if (what == "ctrl") {			// get ctrl
			conn->write(format("ok ctrl %s", ctrl_as_str().c_str()));
		} else if (what == "maxact") {		// get maxact
			conn->addtag("maxact");
			conn->write(format("ok maxact %g", maxact));
		} else if (what == "offset") {		// get offset
			conn->write(format("ok offset %s", offset_str.c_str()));
			
		} else
			parsed = false;

	} else if (command == "set") {			// set ...
		string what = popword(line);
		
		if (what == "gain") {							// set gain <p> <i> <d>
			conn->addtag("gain");
			ctrlparams.gain.p = popdouble(line);
			ctrlparams.gain.i = popdouble(line);
			ctrlparams.gain.d = popdouble(line);
			net_broadcast(format("ok gain %g %g %g", ctrlparams.gain.p, ctrlparams.gain.i, ctrlparams.gain.d));
		} else if (what == "maxact") {		// set maxact <float>
			conn->addtag("maxact");
			maxact = popdouble(line);
			net_broadcast(format("ok maxact %g", maxact));
		} else if (what == "offset") {		// set offset <off0> <off1> ... <offN>
			conn->addtag("offset");
			offset_str = format("%zu", ctrlparams.offset->size);
			double thisoff = 0;
			for (size_t actid=0; actid < ctrlparams.offset->size; actid++) {
				thisoff = popdouble(line);
				gsl_vector_float_set(ctrlparams.offset, actid, thisoff);
				offset_str += format(" %.3g", thisoff);
			}
			net_broadcast(format("ok offset %s", offset_str.c_str()));
		} else
			parsed = false;
	} else if (command == "act") { 
		string actwhat = popword(line);
		
		if (actwhat == "waffle") {				// act waffle
			double w_amp = popdouble(line);
			if (w_amp < 0 || w_amp > 1)
				w_amp = 0.5;
			
			set_wafflepattern(w_amp);
			actuate();
			conn->write(format("ok act waffle %g", w_amp));
		} else if (actwhat == "random") { // act random
			double w_amp = popdouble(line);
			if (w_amp < 0 || w_amp > 1)
				w_amp = 0.5;
			
			set_randompattern(w_amp);
			actuate();
			conn->write(format("ok act random %g", w_amp));
		} else if (actwhat == "one") { 		// act one <id> <val>
			int actid = popint(line);
			double actval = popdouble(line);
			set_control_act(actval, actid);
			actuate();
			conn->write(format("ok act one"));
		} else if (actwhat == "all") { 		// act all <val>
			double actval = popdouble(line);
			set_control(actval);
			actuate();
			conn->write(format("ok act all"));
            
		} else if (actwhat == "vec") { 		// act vec <val0> <val1> ... <valN>
			double actval = 0;
			for (size_t acti=0; acti < ctrlparams.target->size; acti++) {
				actval = popdouble(line);
				gsl_vector_float_set(ctrlparams.target, acti, actval);
			}
			actuate();
			conn->write(format("ok act vec"));
		} else
			parsed = false;
	} else {
		parsed = false;
	}
		
	// If not parsed here, call parent
	if (parsed == false)
		Device::on_message(conn, orig);
}
