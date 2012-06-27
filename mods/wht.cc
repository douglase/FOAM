/*
 wht.h -- William Herschel Telescope control
 Copyright (C) 2012 Tim van Werkhoven <werkhoven@strw.leidenuniv.nl>
 
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

#include <math.h>
#include <string>
#include <gsl/gsl_blas.h>

#ifdef HAVE_CONFIG_H
#include "autoconfig.h"
#endif

#include "utils.h"
#include "io.h"
#include "config.h"
#include "csv.h"
#include "pthread++.h"
#include "serial.h"

#include "devices.h"
#include "telescope.h"
#include "wht.h"

using namespace std;

WHT::WHT(Io &io, foamctrl *const ptc, const string name, const string port, Path const &conffile, const bool online):
Telescope(io, ptc, name, wht_type, port, conffile, online),
wht_ctrl(NULL), sport(""),
altfac(-1.0), delay(1.0)
{
	io.msg(IO_DEB2, "WHT::WHT()");
	
	// Configure initial settings
	{
		// port
		sport = cfg.getstring("port", "/dev/ttyao00");
		
		// delimiter
		// coords url = http://whtics.roque.ing.iac.es:8081/TCSStatus/TCSStatusExPo
		track_prot = "http://";
		track_host = cfg.getstring("track_host", "whtics.roque.ing.iac.es");
		track_port = cfg.getstring("track_port", "8081");
		track_file = cfg.getstring("track_file", "/TCSStatus/TCSStatusExPo");
		
		// Altitude factor, can be 1 or -1 or any other conversion factor
		altfac = cfg.getdouble("altfac", -1.0);

	}

	// WHT operates in alt/az mode
	telunits[0] = "alt";
	telunits[1] = "az";

	// Start WHT config update thread
	wht_cfg_thr.create(sigc::mem_fun(*this, &WHT::wht_updater));

	add_cmd("get trackurl");
	add_cmd("get altfac");
	add_cmd("set altfac");

	// Open serial port connection
	wht_ctrl = new serial::port(sport, B9600, 0, '\r');
}

WHT::~WHT() {
	io.msg(IO_DEB2, "WHT::~WHT()");
	
	// Stop serial port
	delete wht_ctrl;

	//!< @todo Save all device settings back to cfg file
	// Join with WHT updater thread
	wht_cfg_thr.cancel();
	wht_cfg_thr.join();
}

void WHT::wht_updater() {
	struct timeval last, now, diff;
	float this_sleep;
	
	while (ptc->mode != AO_MODE_SHUTDOWN) {
		gettimeofday(&last, 0);
		
		// Update WHT config info
		update_wht_coords(&telpos[0], &telpos[1], &delay);

		// Make sure each iteration takes at minimum delay seconds:
		// update duration = now - last
		// loop period should be delay
		// sleep time = delay - (now - last)
		gettimeofday(&now, 0);
		timerclear(&diff);
		timersub(&now, &last, &diff);
		
		this_sleep = delay * 1E6 - (diff.tv_sec * 1E6 + diff.tv_usec);
		if(this_sleep > 0)
			usleep(this_sleep);
	}
}


int WHT::update_wht_coords(double *const alt, double *const az, double *const delay) {
	// Connect if necessary
	if (!sock_track.is_connected()) {
//		io.msg(IO_DEB1, "WHT::update_wht_coords(): connecting to %s:%s...", track_host.c_str(), track_port.c_str());
		sock_track.connect(track_host, track_port);
		sock_track.setblocking(false);
	}

	// Open URL, request disconnect 
	sock_track.printf("GET %s HTTP/1.1\r\nHOST: %s\r\nUser-Agent: FOAM dev.telescope.wht\r\nConnection: close\r\n\r\n\n", 
										track_file.c_str(), track_host.c_str());

	// Read data
	string rawdata;
	char buf[2048];
	while (sock_track.read((void *)buf, 2048))
		rawdata += string(buf);

	// Parse data, find first line after \r\n\r\n
	size_t dbeg = rawdata.find("\r\n\r\n") +4;
	if (dbeg == string::npos) {
		io.msg(IO_WARN, "WHT::update_wht_coords(): could not find data.");
		return -1;
	}
	string track_data = rawdata.substr(dbeg);
	
	// Split key=val pairs, find coordinates, store and return
	string key, val;
	while (track_data.length()) {
		key = popword(track_data, "=");
		val = popword(track_data, "\n");
		wht_info[key] = val;
	}
	
	// Check if we got the ALT and AZ parameters
	if (wht_info.find("AZ") == wht_info.end() or wht_info.find("ALT") == wht_info.end()) {		
		io.msg(IO_WARN, "WHT::update_wht_coords(): did not get alt/az information!");
	} else {
		// Set elevation, declination
		double newalt = strtod(wht_info["ALT"].c_str(), NULL);
		double newaz = strtod(wht_info["AZ"].c_str(), NULL);
		if (newalt != *alt || newaz != *az) {
			*alt = newalt;
			*az = newaz;
			io.msg(IO_XNFO, "WHT::update_wht_coords(): new alt=%g, az=%g", *alt, *az);
		}
	}
	
	// Check if we have period, and update delay if found
	if (wht_info.find("DELAY") == wht_info.end()) {
		double newdelay = strtod(wht_info["DELAY"].c_str(), NULL);
		if (newdelay > 0 && *delay != newdelay) {
			*delay = newdelay;
			io.msg(IO_XNFO, "WHT::update_wht_coords(): new delay=%g", *delay);
		}
	}
	
	return 0;
}

int WHT::update_telescope_track(const float sht0, const float sht1) {
//	io.msg(IO_DEB1, "WHT::update_telescope_track(sht0=%g, sht1=%g)", sht0, sht1);
	
	// We have shift in the focal plane, convert to WHT axis by rotation:
	// General:
	// x' = [ x cos(th) - y sin(th) ]
	// y; = [ x sin(th) + y cos(th) ]
	// For ExPo:
	// az = 50 - 0.01 * [ x * sin( (45-ele) * pi/180 ) + y * cos( (45-e) * pi/180 ) ]
	// ele = 50 + 0.01 * [ y * sin( (45-ele) * pi/180 ) - x * cos( (45-e) * pi/180 ) ]
	ctrl0 = 50 + (ttgain.p * (sht0 * cos(altfac * (telpos[0]*M_PI/180.0)) - sht1 * sin(altfac * (telpos[0]*M_PI/180.0))));
	ctrl1 = 50 + (ttgain.p * (sht0 * sin(altfac * (telpos[0]*M_PI/180.0)) + sht1 * cos(altfac * (telpos[0]*M_PI/180.0))));
	
	
	// This is the command string sent over the serial port. Syntax is specified 
	// in wht.h, but should be like '00050.00 00050.00 00000.10\r'.
	string cmdstr = format("%7.2f %7.2f %7.2f", ctrl0, ctrl1);

	// Send control command to telescope
	io.msg(IO_XNFO, "WHT::update_telescope_track(): sending '%s'", cmdstr.c_str());
	wht_ctrl->write(cmdstr);
	
	return 0;
}

void WHT::on_message(Connection *const conn, string line) {
	string orig = line;
	string command = popword(line);
	bool parsed = true;
	
	if (command == "get") {
		string what = popword(line);
		
		if (what == "trackurl") {				// get trackurl
			conn->addtag("trackurl");
			conn->write("ok trackurl " +track_prot+"://"+track_host+":"+track_port+"/"+track_file);
		} else if (what == "altfac") {	// get altfac - altitude correction factor
			conn->addtag("altfac");
			conn->write(format("ok altfac %lf", altfac));
		} else 
			parsed = false;
	} else if (command == "set") {
		string what = popword(line);
		
		if (what == "altfac") {					// set altfac <1,-1
			conn->addtag("altfac");
			altfac = popdouble(line);
		} else
			parsed = false;
	} else {
		parsed = false;
	}
	
	// If not parsed here, call parent
	if (parsed == false)
		Telescope::on_message(conn, orig);
}
