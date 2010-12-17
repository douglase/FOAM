/*
 camview.cc -- camera control class
 Copyright (C) 2010 Tim van Werkhoven <t.i.m.vanwerkhoven@xs4all.nl>
 Copyright (C) 2010 Guus Sliepen
 
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

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#define GL_GLEXT_PROTOTYPES
#define GL_ARB_IMAGING

#include <stdexcept>
#include <cstring>
#include <gtkmm/accelmap.h>
#include <stdlib.h>

#include "types.h"
#include "camctrl.h"
#include "glviewer.h"
#include "camview.h"

using namespace std;
using namespace Gtk;

CamView::CamView(Log &log, FoamControl &foamctrl, string n, bool is_parent): 
DevicePage(log, foamctrl, n),
infoframe("Info"),
dispframe("Display settings"),
ctrlframe("Camera controls"),
camframe("Camera"),
histoframe("Histogram"),
e_exposure("Exp."), e_offset("Offset"), e_interval("Intv."), e_gain("Gain"), e_res("Res."), e_mode("Mode"), e_stat("Status"),
flipv("Flip vert."), fliph("Flip hor."), crosshair("Crosshair"), grid("Grid"), zoomin(Stock::ZOOM_IN), zoomout(Stock::ZOOM_OUT), zoom100(Stock::ZOOM_100), zoomfit(Stock::ZOOM_FIT), capture("Capture"), display("Display"), store("Store"),
histoalign(0.5, 0.5, 0, 0), minval("Display min"), maxval("Display max"), e_avg("Avg."), e_rms("RMS"), e_datamin("Min"), e_datamax("Max")
{
	fprintf(stderr, "%x:CamView::CamView()\n", (int) pthread_self());
	
	lastupdate = 0;
	waitforupdate = false;
	s = -1;
	
	// Setup histogram
	histo = 0;
	histopixbuf = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, 256, 100);
	histopixbuf->fill(0xFFFFFF00);
	histoimage.set(histopixbuf);
	histoimage.set_double_buffered(false);
	
	e_exposure.set_width_chars(8);
	e_offset.set_width_chars(4);
	e_interval.set_width_chars(8);
	e_gain.set_width_chars(4);
	
	e_res.set_width_chars(12);
	e_res.set_editable(false);
	e_mode.set_width_chars(8);
	e_mode.set_editable(false);
	e_stat.set_width_chars(20);
	e_stat.set_editable(false);
	
	fliph.set_active(false);
	flipv.set_active(false);
	crosshair.set_active(false);
	grid.set_active(false);
	
	store_n.set_width_chars(4);
	
	minval.set_range(0, 1 << camctrl->get_depth());
	minval.set_digits(0);
	minval.set_increments(1, 16);
	maxval.set_range(0, 1 << camctrl->get_depth());
	maxval.set_digits(0);
	maxval.set_increments(1, 16);
	
	e_avg.set_width_chars(6);
	e_avg.set_alignment(1);
	e_avg.set_editable(false);
	e_rms.set_width_chars(6);
	e_rms.set_alignment(1);
	e_rms.set_editable(false);
	e_datamin.set_width_chars(5);
	e_datamin.set_alignment(1);
	e_datamin.set_editable(false);
	e_datamax.set_width_chars(5);
	e_datamax.set_alignment(1);
	e_datamax.set_editable(false);
	
	clear_gui();
	disable_gui();
	
	// glarea
	//glarea.linkData((void *) NULL, 8, 0, 0);
	glarea.set_size_request(256, 256);	
	
	// signals
	//Glib::signal_timeout().connect(sigc::mem_fun(*this, &CamView::on_timeout), 1000.0/30.0);
	e_exposure.entry.signal_activate().connect(sigc::mem_fun(*this, &CamView::on_info_change));
	e_offset.entry.signal_activate().connect(sigc::mem_fun(*this, &CamView::on_info_change));
	e_interval.entry.signal_activate().connect(sigc::mem_fun(*this, &CamView::on_info_change));
	e_gain.entry.signal_activate().connect(sigc::mem_fun(*this, &CamView::on_info_change));
	
	fliph.signal_toggled().connect(sigc::mem_fun(*this, &CamView::do_update));
	flipv.signal_toggled().connect(sigc::mem_fun(*this, &CamView::do_update));
	crosshair.signal_toggled().connect(sigc::mem_fun(*this, &CamView::do_update));
	grid.signal_toggled().connect(sigc::mem_fun(*this, &CamView::do_update));
	
	zoomfit.signal_toggled().connect(sigc::mem_fun(*this, &CamView::do_update));
	zoom100.signal_clicked().connect(sigc::mem_fun(*this, &CamView::on_zoom100_activate));
	zoomin.signal_clicked().connect(sigc::mem_fun(*this, &CamView::on_zoomin_activate));
	zoomout.signal_clicked().connect(sigc::mem_fun(*this, &CamView::on_zoomout_activate));

	capture.signal_clicked().connect(sigc::mem_fun(*this, &CamView::on_capture_clicked));
	display.signal_clicked().connect(sigc::mem_fun(*this, &CamView::on_display_clicked));
	store.signal_clicked().connect(sigc::mem_fun(*this, &CamView::on_store_clicked));
	
	histoevents.signal_button_press_event().connect(sigc::mem_fun(*this, &CamView::on_histo_clicked));
	
	// Handle some glarea events as well
	glarea.view_update.connect(sigc::mem_fun(*this, &CamView::on_glarea_view_update));
		
	// layout
	infohbox.set_spacing(4);
	infohbox.pack_start(e_exposure, PACK_SHRINK);
	infohbox.pack_start(e_offset, PACK_SHRINK);
	infohbox.pack_start(e_interval, PACK_SHRINK);
	infohbox.pack_start(e_gain, PACK_SHRINK);
	infohbox.pack_start(e_res, PACK_SHRINK);
	infohbox.pack_start(e_mode, PACK_SHRINK);
	infohbox.pack_start(e_stat, PACK_SHRINK);
	infoframe.add(infohbox);
	
	disphbox.set_spacing(4);
	disphbox.pack_start(flipv, PACK_SHRINK);
	disphbox.pack_start(fliph, PACK_SHRINK);
	disphbox.pack_start(crosshair, PACK_SHRINK);
	disphbox.pack_start(grid, PACK_SHRINK);
	disphbox.pack_start(vsep1, PACK_SHRINK);
	disphbox.pack_start(zoomfit, PACK_SHRINK);
	disphbox.pack_start(zoom100, PACK_SHRINK);
	disphbox.pack_start(zoomin, PACK_SHRINK);
	disphbox.pack_start(zoomout, PACK_SHRINK);
	dispframe.add(disphbox);
	
	//ctrlhbox.pack_start(refresh, PACK_SHRINK);
	ctrlhbox.set_spacing(4);
	ctrlhbox.pack_start(capture, PACK_SHRINK);
	ctrlhbox.pack_start(display, PACK_SHRINK);
	ctrlhbox.pack_start(store, PACK_SHRINK);
	ctrlhbox.pack_start(store_n, PACK_SHRINK);
	ctrlframe.add(ctrlhbox);
	
	camhbox.pack_start(glarea);
	camframe.add(camhbox);
	
	histoevents.add(histoimage);
	histoalign.add(histoevents);
	
//	histohbox2.set_spacing(4);
	histohbox2.pack_start(e_avg, PACK_SHRINK);
	histohbox2.pack_start(e_rms, PACK_SHRINK);

//	histohbox3.set_spacing(4);
	histohbox3.pack_start(e_datamin, PACK_SHRINK);
	histohbox3.pack_start(e_datamax, PACK_SHRINK);
	
//	histovbox.set_spacing(4);
	histovbox.pack_start(histohbox2);
	histovbox.pack_start(histohbox3);
	histovbox.pack_start(minval);
	histovbox.pack_start(maxval);

//	histohbox.set_spacing(4);
	histohbox.pack_start(histoalign);
	histohbox.pack_start(histovbox, PACK_SHRINK);

	histoframe.add(histohbox);
	
	pack_start(infoframe, PACK_SHRINK);
	pack_start(dispframe, PACK_SHRINK);
	pack_start(ctrlframe, PACK_SHRINK);
	pack_start(camframe);
	pack_start(histoframe, PACK_SHRINK);
	
	// finalize
	show_all_children();
	
	//!< @todo add histogram toggling
//	if(!histogram.get_active())
//		histogramframe.hide();
	
	if (is_parent)
		init();
}

CamView::~CamView() {
	//!< @todo store (gui) configuration here?
	fprintf(stderr, "%x:CamView::~CamView()\n", (int) pthread_self());
}

void CamView::enable_gui() {
	DevicePage::enable_gui();
	
	e_exposure.set_sensitive(true);
	e_offset.set_sensitive(true);
	e_interval.set_sensitive(true);
	e_gain.set_sensitive(true);
	
	fliph.set_sensitive(true);
	flipv.set_sensitive(true);
	crosshair.set_sensitive(true);
	grid.set_sensitive(true);
	
	capture.set_sensitive(true);
	display.set_sensitive(true);
	store.set_sensitive(true);
	store_n.set_sensitive(true);
	
//	e_avg.set_sensitive(true);
//	e_rms.set_sensitive(true);
}

void CamView::disable_gui() {
	DevicePage::disable_gui();
	
	e_exposure.set_sensitive(false);
	e_offset.set_sensitive(false);
	e_interval.set_sensitive(false);
	e_gain.set_sensitive(false);
	
	fliph.set_sensitive(false);
	flipv.set_sensitive(false);
	crosshair.set_sensitive(false);
	grid.set_sensitive(false);

	capture.set_sensitive(false);
	display.set_sensitive(false);
	store.set_sensitive(false);
	store_n.set_sensitive(false);

//	e_avg.set_sensitive(false);
//	e_rms.set_sensitive(false);
}

void CamView::clear_gui() {
	DevicePage::clear_gui();
	
	e_exposure.set_text("N/A");
	e_offset.set_text("N/A");
	e_interval.set_text("N/A");
	e_gain.set_text("N/A");
	e_res.set_text("N/A");
	e_mode.set_text("N/A");
	e_stat.set_text("N/A");

	capture.set_state(SwitchButton::CLEAR);
	display.set_state(SwitchButton::CLEAR);
	store.set_state(SwitchButton::CLEAR);
	
	store_n.set_text("10");

	e_avg.set_text("N/A");
	e_rms.set_text("N/A");	
	e_datamin.set_text("N/A");
	e_datamax.set_text("N/A");
	minval.set_value(0);
	maxval.set_value(1 << camctrl->get_depth());
}

void CamView::init() {
	fprintf(stderr, "%x:CamView::init()\n", (int) pthread_self());
	// Init new camera control connection for this viewer
	camctrl = new CamCtrl(log, foamctrl.host, foamctrl.port, devname);
	// Downcast to generic device control pointer for base class (DevicePage in this case)
	devctrl = (DeviceCtrl *) camctrl;
	
	camctrl->signal_monitor.connect(sigc::mem_fun(*this, &CamView::on_monitor_update));
	camctrl->signal_message.connect(sigc::mem_fun(*this, &CamView::on_message_update));
	camctrl->signal_connect.connect(sigc::mem_fun(*this, &CamView::on_connect_update));
}

void CamView::on_glarea_view_update() {
	// Callback for glarea update on viewstate (zoom, scale, shift)
	zoomfit.set_active(glarea.getzoomfit());		
}

void CamView::do_update() {
	glarea.setcrosshair(crosshair.get_active());
	glarea.setgrid(grid.get_active());
	// Flip settings
	glarea.setfliph(fliph.get_active());
	glarea.setflipv(flipv.get_active());
	// Zoom settings
	glarea.setzoomfit(zoomfit.get_active());
	glarea.do_update();
	do_histo_update();
}

void CamView::do_histo_update() {	
	int pixels = 0;											//!< Number of pixels
	int max = 1 << camctrl->get_depth(); //!< Maximum intensity in image
	int dmin=max, dmax=0;								//!< Data range
	double sum = 0;
	double sumsquared = 0;
	double rms = max;
	bool overexposed = false;						//!< Overexposed flag
	
	// Analyze histogram data if available. histo is a linear array from 0 to
	// the maximum intensity and counts pixels for each intensity bin.
	if (histo) {
		for (int i = 0; i < max; i++) {
			pixels += histo[i];
			sum += (double)i * histo[i];
			sumsquared += (double)(i * i) * histo[i];
			
			if (i >= 0.98 * max && histo[i])
				overexposed = true;
		}
		
		sum /= pixels;
		sumsquared /= pixels;
		rms = sqrt(sumsquared - sum * sum) / sum;
	}
	
	minval.set_range(0, 1 << camctrl->get_depth());
	maxval.set_range(0, 1 << camctrl->get_depth());
	e_avg.set_text(format("%.2lf", sum));
	e_rms.set_text(format("%.3lf", rms));
	
	e_datamin.set_text(format("%d", camctrl->monitor.min));
	e_datamax.set_text(format("%d", camctrl->monitor.max));
	// Update min/max if necessary

	//<! @todo Add contrast feature
//	if (contrast.get_active()) {
//		minval.set_value(sum - 5 * sum * rms);
//		maxval.set_value(sum + 5 * sum * rms);
//	}
	
	// Draw the histogram
	
	if (!histoframe.is_visible())
		return;
	
	const int hscale = 1 + 10 * pixels / max;
	
	// Color red if overexposed
	//!< @todo implement underover
	//if (overexposed && underover.get_active() && lastupdate & 1)
	if (overexposed)
		histopixbuf->fill(0xff000000);
	else
		histopixbuf->fill(0xffffff00);
	
	// Draw histogram, make everything white that should not be black.
	uint8_t *out = (uint8_t *)histopixbuf->get_pixels();
	
	if(histo) {
		for(int i = 0; i < max; i++) {
			int height = histo[i] * 100 / hscale;
			if(height > 100)
				height = 100;
			for(int y = 100 - height; y < 100; y++) {
				uint8_t *p = out + 3 * ((i * 256 / max) + 256 * y);
				p[0] = 0;
				p[1] = 0;
				p[2] = 0;
			}
		}
	}
	
	// Make vertical bars (red and cyan) at minval and maxval:
	int x1 = clamp(minval.get_value_as_int() * 256 / max, 0, 255);
	int x2 = clamp(maxval.get_value_as_int() * 256 / max, 0, 255);
	
	for(int y = 0; y < 100; y += 2) {
		uint8_t *p = out + 3 * (x1 + 256 * y);
		p[0] = 255;
		p[1] = 0;
		p[2] = 0;
		p = out + 3 * (x2 + 256 * y);
		p[0] = 0;
		p[1] = 255;
		p[2] = 255;
	}
	
	histoimage.queue_draw();
}

//! @todo Implement on_timeout method here
bool CamView::on_timeout() {
	if(waitforupdate && time(NULL) - lastupdate < 5)
		return true;

	fprintf(stderr, "%x:CamView::on_timeout()\n", (int) pthread_self());

	return true;
}

void CamView::on_monitor_update() {
	force_update();
	// Get new image if dispay button is in 'OK'
	if (display.get_state(SwitchButton::OK))
		camctrl->grab(0, 0, camctrl->get_width(), camctrl->get_height(), 1, false);
}

void CamView::force_update() {
	//! @todo difference between on_monitor_update
	//! @todo need mutex here?
	glarea.linkData((void *) camctrl->monitor.image, camctrl->monitor.depth, camctrl->monitor.x2 - camctrl->monitor.x1, camctrl->monitor.y2 - camctrl->monitor.y1);
	
	// Do histogram, make local copy if needed
	if (camctrl->monitor.histo) {
		if (!histo)
			histo = (uint32_t *) malloc((1 << camctrl->get_depth()) * sizeof *histo);
		memcpy(histo, camctrl->monitor.histo, (1 << camctrl->get_depth()) * sizeof *histo);
	} 
	else if (histo) {
		free(histo);
		histo = 0;
	}
	
	e_avg.set_text(format("%g", camctrl->monitor.avg));
	e_rms.set_text(format("%g", camctrl->monitor.rms));
	
	do_histo_update();
}

void CamView::on_connect_update() {
	fprintf(stderr, "%x:CamView::on_connect_update(conn=%d)\n", (int) pthread_self(), devctrl->is_connected());
	if (devctrl->is_connected())
		enable_gui();
	else
		disable_gui();
}

void CamView::on_message_update() {
	DevicePage::on_message_update();
	
	// Set values in text entries
	e_exposure.set_text(format("%g", camctrl->get_exposure()));
	e_offset.set_text(format("%g", camctrl->get_offset()));
	e_interval.set_text(format("%g", camctrl->get_interval()));
	e_gain.set_text(format("%g", camctrl->get_gain()));
	e_res.set_text(format("%dx%dx%d", camctrl->get_width(), camctrl->get_height(), camctrl->get_depth()));

	// Set 'Mode' text entry, change color appropriately
	e_mode.set_text(camctrl->get_modestr());
	if (camctrl->get_mode() == CamCtrl::WAITING || camctrl->get_mode() == CamCtrl::OFF)
		e_mode.entry.modify_base(STATE_NORMAL, Gdk::Color("orange"));
	else if (camctrl->get_mode() == CamCtrl::SINGLE || camctrl->get_mode() == CamCtrl::RUNNING)
		e_mode.entry.modify_base(STATE_NORMAL, Gdk::Color("lightgreen"));
	else
		e_mode.entry.modify_base(STATE_NORMAL, Gdk::Color("red"));
	
	if (camctrl->get_mode() == CamCtrl::OFF || camctrl->get_mode() == CamCtrl::WAITING)
		capture.set_state(SwitchButton::CLEAR);
	else if (camctrl->get_mode() == CamCtrl::CONFIG) 
		capture.set_state(SwitchButton::WAITING);
	else if (camctrl->get_mode() == CamCtrl::SINGLE || camctrl->get_mode() == CamCtrl::RUNNING) 
		capture.set_state(SwitchButton::OK);
	else
		capture.set_state(SwitchButton::ERROR);
	
	if (camctrl->is_ok()) {
		e_stat.entry.modify_base(STATE_NORMAL, Gdk::Color("lightgreen"));
		e_stat.set_text("Ok");
	}
	else {
		e_stat.entry.modify_base(STATE_NORMAL, Gdk::Color("red"));
		e_stat.set_text("Err: " + camctrl->get_errormsg());
	}	
	
	store_n.set_text(format("%d", camctrl->get_nstore()));
	if (camctrl->get_nstore() <= 0)
		store.set_state(SwitchButton::CLEAR);
	else
		store.set_state(SwitchButton::WAITING);

}

void CamView::on_info_change() {
	fprintf(stderr, "%x:CamView::on_info_change()\n", (int) pthread_self());
	camctrl->set_exposure(strtod(e_exposure.get_text().c_str(), NULL));
	camctrl->set_offset(strtod(e_offset.get_text().c_str(), NULL));
	camctrl->set_interval(strtod(e_interval.get_text().c_str(), NULL));
	camctrl->set_gain(strtod(e_gain.get_text().c_str(), NULL));
}

void CamView::on_zoom100_activate() {
	zoomfit.set_active(false);
	glarea.setscale(0.0);
}

void CamView::on_zoomin_activate() {
	zoomfit.set_active(false);
	glarea.scalestep(1.0/3.0);
}

void CamView::on_zoomout_activate() {
	zoomfit.set_active(false);
	glarea.scalestep(-1.0/3.0);
}

void CamView::on_capture_clicked() {
	if (camctrl->get_mode() == CamCtrl::RUNNING || 
			camctrl->get_mode() == CamCtrl::SINGLE) {
		fprintf(stderr, "%x:CamView::on_capture_update(): Stopping camera.\n", (int) pthread_self());
		camctrl->set_mode(CamCtrl::WAITING);
	}
	else {
		fprintf(stderr, "%x:CamView::on_capture_update(): Starting camera.\n", (int) pthread_self());
		camctrl->set_mode(CamCtrl::RUNNING);
	}
}

void CamView::on_display_clicked() {
	//! @bug Does not work when activated before capturing frames?
	if (display.get_state(SwitchButton::CLEAR)) {
		display.set_state(SwitchButton::OK);
		camctrl->grab(0, 0, camctrl->get_width(), camctrl->get_height(), 1, false);
	}
	else
		display.set_state(SwitchButton::CLEAR);
}

void CamView::on_store_clicked() {
	int nstore = atoi(store_n.get_text().c_str());
	fprintf(stderr, "%x:CamView::on_store_update() n=%d\n", (int) pthread_self(), nstore);
	
	if (nstore > 0 || nstore == -1)
		camctrl->store(nstore);
}

bool CamView::on_histo_clicked(GdkEventButton *event) {
	if (event->type != GDK_BUTTON_PRESS)
		return false;
	
	int shift = camctrl->get_depth() - 8;
	if (shift < 0)
		shift = 0;
	double value = event->x * (1 << shift);
	
	if (event->button == 1)
		minval.set_value(value);
	if (event->button == 2) {
		minval.set_value(value - (16 << shift));
		maxval.set_value(value + (16 << shift));
	}
	if (event->button == 3)
		maxval.set_value(value);
	
	//!< @todo contrast
	//contrast.set_active(false);
	do_update();
	
	return true;
}

