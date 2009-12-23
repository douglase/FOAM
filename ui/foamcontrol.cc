/* foamcontrol.cc - FOAM control connection
 Copyright (C) 2009 Tim van Werkhoven (t.i.m.vanwerkhoven@xs4all.nl)
 
 This file is part of FOAM.
 
 FOAM is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 FOAM is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with FOAM.  If not, see <http://www.gnu.org/licenses/>.
 */
/*!
 @file foamcontrol.cc
 @author Tim van Werkhoven (t.i.m.vanwerkhoven@xs4all.nl)
 
 @brief This is the FOAM control connection class
 */

#include "foamcontrol.h"

FoamControl::FoamControl(ControlPage &parent): parent(parent) {
	printf("%x:FoamControl::FoamControl()\n", pthread_self());

	init();
}

FoamControl::~FoamControl() {
	if (protocol) 
		delete protocol;
}

void FoamControl::init() {
	printf("%xFoamControl::init()\n", pthread_self());
	protocol = new Protocol::Client();
	
	ok = false;
	errormsg = "Not connected";
	
	// Init variables
	state.mode = AO_MODE_UNDEF;
	state.numwfc = 0;
	state.numwfs = 0;
	
	// register callbacks
	signal_conn_update.connect(sigc::mem_fun(*this, &FoamControl::on_connect_update));
	signal_msg_update.connect(sigc::mem_fun(*this, &FoamControl::on_message_update));
}

int FoamControl::connect(const string &host, const string &port) {
	printf("%x:FoamControl::connect(%s, %s)\n", pthread_self(), host.c_str(), port.c_str());
	
	if (protocol->is_connected()) 
		return -1;
	
	// connect a control connection
	protocol->slot_message = sigc::mem_fun(this, &FoamControl::on_message);
	protocol->slot_connected = sigc::mem_fun(this, &FoamControl::on_connected);
	protocol->connect(host, port, "");
	
	return 0;
}

void FoamControl::set_mode(aomode_t mode) {
	if (!protocol->is_connected()) return;
	
	switch (mode) {
		case AO_MODE_LISTEN:
			printf("FoamControl::set_mode(AO_MODE_LISTEN)\n");
			protocol->write("MODE LISTEN");
			break;
		case AO_MODE_OPEN:
			printf("FoamControl::set_mode(AO_MODE_OPEN)\n");
			protocol->write("MODE OPEN");
			break;
		case AO_MODE_CLOSED:
			printf("FoamControl::set_mode(AO_MODE_CLOSED)\n");
			protocol->write("MODE CLOSED");
			break;
		default:
			printf("FoamControl::set_mode(UNKNOWN)\n");
			break;
	}
}

int FoamControl::disconnect() {
	printf("%xFoamControl::disconnect()\n", pthread_self());
	if (protocol->is_connected())
		protocol->disconnect();
	
	// Init variables
	state.mode = AO_MODE_UNDEF;
	state.numwfc = 0;
	state.numwfs = 0;
	
	signal_conn_update();
	return 0;
}

void FoamControl::on_connect_update() {
	printf("%xFoamControl::on_connect_update()\n", pthread_self());
	
	// GUI updating is done in parent (a ControlPage)
	parent.on_connect_update();
}

void FoamControl::on_message_update() {
	printf("%xFoamControl::on_message_update()\n", pthread_self());
	// GUI updating is done in parent (a ControlPage)
	parent.on_message_update();
}

void FoamControl::on_connected(bool conn) {
	printf("%xFoamControl::on_connected(bool=%d)\n", pthread_self(), conn);

	if (!conn) {
		ok = false;
		errormsg = "Not connected";
		protocol->disconnect();
		signal_conn_update();
		return;
	}
	
	ok = true;
	
	protocol->write("GET NUMWFS");
	protocol->write("GET NUMWFC");
	protocol->write("GET MODE");
	signal_conn_update();
	return;
}

void FoamControl::on_message(string line) {
	printf("%xFoamControl::on_message(string=%s)\n", pthread_self(), line.c_str());

	if (popword(line) != "OK") {
		ok = false;
		errormsg = line;
		signal_msg_update();
		return;
	}
	
	ok = true;
	string what = popword(line);
	
	if (what == "VAR") {
		string var = popword(line);
		if (var == "NUMWFC")
			state.numwfc = popint32(line);
		else if (var == "NUMWFS")
			state.numwfs = popint32(line);
		else if (var == "MODE")
			state.mode = str2mode(popword(line));
	}
	else if (what == "CMD") {
		// command confirmation hook
		state.currcmd = popword(line);
	}
	else if (what == "CALIB") {
		// post-calibration hook
	}
	else if (what == "MODE") {
		state.mode = str2mode(popword(line));
	} else {
		ok = false;
		errormsg = "Unexpected response '" + what + "'";
	}
	
	signal_msg_update();
}

