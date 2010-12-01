/*
 camview.h -- camera control class
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

#ifndef HAVE_CAMVIEW_H
#define HAVE_CAMVIEW_H

#include <gtkmm.h>
#include <gdkmm/pixbuf.h>
#include <gtkglmm.h>

#include "widgets.h"

#include "deviceview.h"
#include "camctrl.h"
#include "glviewer.h"

/*!
 @brief Generic camera viewing class
 
 This is the GUI element for CamCtrl, it shows basic controls for generic 
 cameras and can display frames.
 */
class CamView: public DevicePage {
protected:
	Gtk::Frame infoframe;
	Gtk::Frame dispframe;
	Gtk::Frame ctrlframe;
	Gtk::Frame camframe;
	Gtk::Frame histoframe;
	
	// Info stuff
	HBox infohbox;
	LabeledEntry e_exposure;						//!< For exposure time, RW
	LabeledEntry e_offset;							//!< For offset, RW
	LabeledEntry e_interval;						//!< For interval, RW
	LabeledEntry e_gain;								//!< For gain, RW
	LabeledEntry e_res;									//!< For resolution, RO
	LabeledEntry e_mode;								//!< For mode, RO
	LabeledEntry e_stat;								//!< For status, RO
	
	// Display stuff
	//!< @todo contrast, underover, colorsel, histogram
	HBox disphbox;
	CheckButton flipv;									//!< Flip image vertically (only GUI)
	CheckButton fliph;									//!< Flip image horizontally (only GUI)
	CheckButton crosshair;							//!< Show crosshair
	CheckButton grid;										//!< Show grid
	VSeparator vsep1;
	Button zoomin;											//!< Zoom in, CamView::on_zoomin_activate()
	Button zoomout;											//!< Zoom out, CamView::on_zoomout_activate()
	Button zoom100;											//!< Zoom to original size, CamView::on_zoom100_activate()
	ToggleButton zoomfit;								//!< Zoom to fit to window

	// control stuff
	// Need: darkflat, fsel, tiptilt, capture, thumb, ...?
	HBox ctrlhbox;
	//Button refresh;
	SwitchButton capture;								//!< Start/stop capturing frames, CamView::on_capture_clicked()
	SwitchButton display;								//!< Start/stop displaying frames, CamView::on_display_clicked()
	SwitchButton store;									//!< Start/stop storing frames on the camera, CamView::on_store_clicked()
	Entry store_n;											//!< How many frames to store when clicking CamView:store
		
	// Camera image
	HBox camhbox;
	OpenGLImageViewer glarea;
	
	// Histogram stuff
	HBox histohbox;
	LabeledEntry mean;									//!< Shows mean value
	LabeledEntry stddev;								//!< Shows sigma

	bool waitforupdate;
	time_t lastupdate;
	float dx;
	float dy;
	int s;
	
	// User interaction
	void on_zoom100_activate();					//!< Zoom to original size
	void on_zoomin_activate();					//!< Zoom in
	void on_zoomout_activate();					//!< Zoom out
	void on_capture_clicked();					//!< (De-)activate camera when user presses CamView::capture button.
	void on_display_clicked();					//!< (De-)activate camera frame grabbing when user presses CamView::display button.
	void on_store_clicked();						//!< Called when user clicks CamView::store
	void on_info_change();							//!< Propagate user changed settings in GUI to camera

	//!< @todo Sort these functions out
	void on_image_realize();
	void on_image_expose_event(GdkEventExpose *event);
	void on_image_configure_event(GdkEventConfigure *event);
	bool on_image_scroll_event(GdkEventScroll *event);
	bool on_image_motion_event(GdkEventMotion *event);
	bool on_image_button_event(GdkEventButton *event);
	
	// GUI updates
	//!< @todo Sort these functions out
	void on_glarea_view_update();				//!< Callback from glarea class
	void force_update();
	void do_update();

	// Overload from DeviceView:
	virtual void disable_gui();
	virtual void enable_gui();
	virtual void clear_gui();

	virtual void on_message_update();
	virtual void on_connect_update();
	
	// New event capture
	virtual void on_monitor_update();		//!< Display new image from camera
	bool on_timeout();

public:
	CamCtrl *camctrl;
	CamView(Log &log, FoamControl &foamctrl, string n, bool is_parent=false);
	~CamView();
	
	virtual void init();
};


#endif // HAVE_CAMVIEW_H


/*!
 \page dev_cam Camera devices : CamView & CamCtrl
 
 \section camview_camview CamView
 
 Shows a basic GUI for a generic camera. See CamView
 
 \section camview_camctrl CamCtrl
 
 Controls a generic camera. See CamCtrl.
 
 \section camview_derived Derived classes
 
 The following classes are dervied from the Camera device:
 - \subpage dev_cam_wfs "Wavefront sensor device"
 
 */
