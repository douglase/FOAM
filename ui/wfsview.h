/*
 wfsview.h -- FOAM GUI wavefront sensor pane
 Copyright (C) 2009--2010 Tim van Werkhoven <t.i.m.vanwerkhoven@xs4all.nl>
 
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
/*!
 @file wfsview.h
 @author Tim van Werkhoven (t.i.m.vanwerkhoven@xs4all.nl)
 
 @brief This is the FOAM GUI wavefront sensor pane
 */

#ifndef HAVE_WFSVIEW_H
#define HAVE_WFSVIEW_H

#include <gtkmm.h>

#include "log.h"
#include "foamcontrol.h"
#include "widgets.h"

using namespace Gtk;
using namespace std;

/*!
 @brief This class gives information on one wavefront sensor
 @todo Document this
*/
class WfsInfo: public HBox {
	FoamControl foamcontrol;
	Log &log;

	struct info_t {
		int id;
		//wfstype_t type;
		string name;
	} info;
	
	Label thumb;
	Label wfs;
	Label wfsid;
	
public:
	WfsInfo(Log &log);
	~WfsInfo() { };
};

/*!
 @brief This page gives an overview of all wavefront sensors
 
 This gives an overview of all wavefront sensors, with one global control 
 connection, and one for every wavefront sensor (issued through WfsInfo). At
 the top there will be some global information, then each WFS will have one 
 WfsInfo row in the VBox, and will display a thumbnail, name and id.
*/
class WfsPage: public VBox {
	FoamControl foamcontrol;
	Log &log;
	
	HBox status;
	LabeledEntry numwfs;
	
public:
	WfsPage(Log &log);
	~WfsPage() { };

};

#endif // HAVE_WFSVIEW_H

/*!
 \page dev_cam_wfs Wavefront sensor devices: WfsView & WfsCtrl
 
 \section wfsview_wfsview WfsView
 
 \section wfsview_wfsctrl WfsCtrl
 
 \section wfsview_derived Derived classes
 
 The following classes are dervied from the Wavefront sensor device:
 - \subpage dev_cam_wfs_sh "Shack-Hartmann Wavefront sensor devices"
*/