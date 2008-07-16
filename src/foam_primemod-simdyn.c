/*
 Copyright (C) 2008 Tim van Werkhoven (tvwerkhoven@xs4all.nl)
 
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
 @file foam_primemod-simdyn.c
 @author @authortim
 @date 2008-07-15
 
 @brief This is a dynamical simulation mode, which simulates an AO system at wavefront level 
 
 This primemodule can be used to simulate a complete AO setup, starting with a perturbed wavefront
 and following this through the complete optical setup.
 */

// HEADERS //
/***********/

// We need these for modMessage, see foam_cs.c
extern pthread_mutex_t mode_mutex;
extern pthread_cond_t mode_cond;

// GLOBALS //
/***********/

#define FOAM_CONFIG_PRE "simdyn"

// Displaying
#ifdef FOAM_SIMDYN_DISPLAY
mod_display_t disp;
#endif

// Shack Hartmann tracking info
mod_sh_track_t shtrack;
// Simulation parameters
mod_sim_t simparams;

int modInitModule(control_t *ptc, config_t *cs_config) {
	logInfo(0, "This is the dynamical simulation (simdyn) prime module, enjoy.");
	
	// populate ptc here
	ptc->mode = AO_MODE_LISTEN;			// start in listen mode (safe bet, you probably want this)
	ptc->calmode = CAL_INFL;			// this is not really relevant initialliy
	ptc->logfrac = 10;                  // log verbose messages only every 100 frames
	ptc->misclogfile = FOAM_CONFIG_PRE "-datalog.dat"; 	// name the logfile
	ptc->wfs_count = 1;					// just 1 'wfs' for simulation
	ptc->wfc_count = 1;					// one TT mirror
	ptc->fw_count = 2;					// 2 filterwheels (which we actually don't use)
	
	// allocate memory for filters, wfc's and wfs's
	ptc->filter = (filtwheel_t *) calloc(ptc->fw_count, sizeof(filtwheel_t));
	ptc->wfc = (wfc_t *) calloc(ptc->wfc_count, sizeof(wfc_t));
	ptc->wfs = (wfs_t *) calloc(ptc->wfs_count, sizeof(wfs_t));

	// OPENING LOGFILE //
	/////////////////////

	ptc->misclog = fopen(ptc->misclogfile, "w+");		// open the logfile
	if (!ptc->misclog) 
		logErr("Could not open misc logfile '%s'!", ptc->misclogfile);
	
	ptc->domisclog = false;								// start with logging turned off

	// CONFIGURE WAVEFRONT CORRECTORS //
	////////////////////////////////////

	// configure WFC 0
	/*
	ptc->wfc[1].name = "DM";
	ptc->wfc[1].nact = 37;
	ptc->wfc[1].gain.p = 1.0;
	ptc->wfc[1].gain.i = 1.0;
	ptc->wfc[1].gain.d = 1.0;
	ptc->wfc[1].type = WFC_DM;
    ptc->wfc[0].id = 1;
	ptc->wfc[1].calrange[0] = -1.0;
	ptc->wfc[1].calrange[1] = 1.0;
	*/
	
	// configure TT WFC
	
	ptc->wfc[0].name = "TT";
	ptc->wfc[0].nact = 2;
	ptc->wfc[0].gain.p = 1.0;
	ptc->wfc[0].gain.i = 1.0;
	ptc->wfc[0].gain.d = 1.0;
	ptc->wfc[0].type = WFC_TT;
	ptc->wfc[0].id = 2;
	ptc->wfc[0].calrange[0] = -1.0;
	ptc->wfc[0].calrange[1] = 1.0;
	
	
	// CONFIGURE FILTERS //
	///////////////////////

	// configure filter 0
	ptc->filter[0].name = "Telescope FW";
	ptc->filter[0].id = 0;
    ptc->filter[0].delay = 2;
	ptc->filter[0].nfilts = 4;
	ptc->filter[0].filters[0] = FILT_PINHOLE;
	ptc->filter[0].filters[1] = FILT_OPEN;
    ptc->filter[0].filters[2] = FILT_TARGET;
    ptc->filter[0].filters[3] = FILT_CLOSED;

	// configure filter 1
	ptc->filter[1].name = "WFS FW";
	ptc->filter[1].id = 1;
	ptc->filter[1].nfilts = 2;
    ptc->filter[1].delay = 2;
    ptc->filter[1].filters[0] = FILT_PINHOLE;
	ptc->filter[1].filters[1] = FILT_OPEN;
	
	// CONFIGURE WAVEFRONT SENSORS //
	/////////////////////////////////

	// configure WFS 0
	ptc->wfs[0].name = "SH WFS - dyn";
	ptc->wfs[0].res.x = 256;
	ptc->wfs[0].res.y = 256;
	ptc->wfs[0].bpp = 8;
	// this is where we will look for dark/flat/sky images
	ptc->wfs[0].darkfile = FOAM_CONFIG_PRE "_dark.gsldump";	
	ptc->wfs[0].flatfile = FOAM_CONFIG_PRE "_flat.gsldump";
	ptc->wfs[0].skyfile = FOAM_CONFIG_PRE "_sky.gsldump";
	ptc->wfs[0].scandir = AO_AXES_XY;
	ptc->wfs[0].id = 0;
	ptc->wfs[0].fieldframes = 1000;     // take 1000 frames for a dark or flatfield
		
	// CONFIGURE SIMULATION MODULE //
	/////////////////////////////////
		
	simparams.wind.x = 5;
	simparams.wind.y = 5;
	simparams.error = ERR_SEEING;
	simparams.errwfc= &(ptc->wfc[0]);
	simparams.corr = &(ptc->wfc[0]);
	simparams.noise = 0;
	simparams.seeingfac = 0.3;
	// these files are needed for AO simulation and will be read in by simInit()
	simparams.wf = "../config/wavefront.png";
	simparams.apert = "../config/apert15-256.pgm";
	simparams.actpat = "../config/dm37-256.pgm";
	// resolution of the simulated image
	simparams.currimgres.x = ptc->wfs[0].res.x;
	simparams.currimgres.y = ptc->wfs[0].res.y;
	// These need to be init to NULL
	simparams.shin = simparams.shout = simparams.plan_forward = NULL;
	simparams.wisdomfile = FOAM_CONFIG_PRE "_fftw-wisdom";
	if(simInit(&simparams) != EXIT_SUCCESS)
		logErr("Failed to initialize simulation module.");

	ptc->wfs[0].image = (void *) simparams.currimg;

	// CONFIGURE SHTRACK MODULE //
	//////////////////////////////
	
    // we have an image of WxH, with a lenslet array of WlxHl, such that
    // each lenslet occupies W/Wl x H/Hl pixels, and we use track.x x track.y
    // pixels to track the CoG or do correlation tracking.
	shtrack.cells.x = 8;				// we're using a 16x16 lenslet array (fake)
	shtrack.cells.y = 8;
	shtrack.shsize.x = ptc->wfs[0].res.x/shtrack.cells.x;
	shtrack.shsize.y = ptc->wfs[0].res.y/shtrack.cells.y;
	shtrack.track.x = shtrack.shsize.x/2;   // tracker windows are half the size of the lenslet grid things
	shtrack.track.y = shtrack.shsize.y/2;
	shtrack.pinhole = FOAM_CONFIG_PRE "_pinhole.gsldump";
	shtrack.influence = FOAM_CONFIG_PRE "_influence.gsldump";
	shtrack.measurecount = 2;
	shtrack.skipframes = 2;
	shtrack.samxr = -1;			// 1 row edge erosion
	shtrack.samini = 30;			// minimum intensity for subaptselection 10
	// init the shtrack module now
	if (modInitSH(&(ptc->wfs[0]), &shtrack) != EXIT_SUCCESS)
		logErr("Failed to initialize shack-hartmann module.");
	
	// CONFIGURE CS_CONFIG SETTINGS //
	//////////////////////////////////
	
	cs_config->listenip = "0.0.0.0";	// listen on any IP by defaul
	cs_config->listenport = 10000;		// listen on port 10000 by default
	cs_config->use_syslog = false;		// don't use the syslog
	cs_config->syslog_prepend = "foam-stat";	// prepend logging with 'foam-stat'
	cs_config->use_stdout = true;		// do use stdout
	cs_config->loglevel = LOGDEBUG;		// log error, info and debug
	cs_config->infofile = NULL;			// don't log anything to file
	cs_config->errfile = NULL;
	cs_config->debugfile = NULL;
	
	return EXIT_SUCCESS;
}

int modPostInitModule(control_t *ptc, config_t *cs_config) {
	// we initialize OpenGL here, because it doesn't like threading
	// a lot
#ifdef FOAM_SIMDYN_DISPLAY
	// CONFIGURE DISPLAY MODULE //
	//////////////////////////////
		
	disp.caption = "WFS #1";
	disp.res.x = ptc->wfs[0].res.x;
	disp.res.y = ptc->wfs[0].res.y;
	disp.autocontrast = 0;
	disp.brightness = 0;
	disp.contrast = 1;
	disp.dispsrc = DISPSRC_RAW;         // use the raw ccd output
	disp.dispover = DISPOVERLAY_GRID;   // display the SH grid
	disp.col.r = 255;
	disp.col.g = 255;
	disp.col.b = 255;
	
	displayInit(&disp);
#endif
	
#ifdef __APPLE__
	// We need this to fix autoreleasepool errors, I think
	// see https://savannah.gnu.org/bugs/?20957
	// and http://www.idevgames.com/forum/archive/index.php/t-7710.html
	// call this at the beginning of the thread:
	// update: This is objective C and does not work out of the box...
	//NSAutoreleasePool	 *autoreleasepool = [[NSAutoreleasePool alloc] init];
#endif
	
	return EXIT_SUCCESS;
}

void modStopModule(control_t *ptc) {
#ifdef FOAM_SIMDYN_DISPLAY
	displayFinish(&disp);
#endif
	// close log file
	fclose(ptc->misclog);
	
#ifdef __APPLE__
	// need to call this before the thread dies:
	// update: This is objective C and does not work out of the box...	
//	[autoreleasepool release];
	// but there is a problem as this is not always the end of the thread
	// it IS for the two-threaded model (1 networking, 1 ao) however,
	// so if compiling on OS X, we can only use two threads I'm afraid...
#endif
}

// OPEN LOOP ROUTINES //
/*********************/

int modOpenInit(control_t *ptc) {
	int i;
	// set disp source to full calib
	disp.dispsrc = DISPSRC_FULLCALIB;

	// set actuators to center
	for (i=0; i < ptc->wfc_count; i++) {
		gsl_vector_float_set_zero(ptc->wfc[i].ctrl);
		drvSetActuator(ptc, i);
	}

	// nothing to init for static simulation
	return EXIT_SUCCESS;
}

int modOpenLoop(control_t *ptc) {
	static char title[64];
	int sn;
	
	// Get simulated image for the first WFS
	drvGetImg(ptc, 0);
	
	// dark-flat the whole frame
	MMDarkFlatFullByte(&(ptc->wfs[0]), &shtrack);
	
	modCogTrack(ptc->wfs[0].corrim, DATA_GSL_M_F, ALIGN_RECT, &shtrack, NULL, NULL);
	
	// log offsets measured
	if (ptc->domisclog && shtrack.nsubap > 0) {
		fprintf(ptc->misclog, "O, %ld, %d", ptc->frames, shtrack.nsubap);
		for (sn = 0; sn < shtrack.nsubap; sn++)
			fprintf(ptc->misclog, ", %f, %f", \
					gsl_vector_float_get(shtrack.disp, 2*sn + 0), \
					gsl_vector_float_get(shtrack.disp, 2*sn + 1));
		fprintf(ptc->misclog, "\n");
	}

#ifdef FOAM_SIMDYN_DISPLAY
    if (ptc->frames % ptc->logfrac == 0) {
		displayDraw((&ptc->wfs[0]), &disp, &shtrack);
		displaySDLEvents(&disp);
		logInfo(0, "Current framerate: %.2f FPS", ptc->fps);
		snprintf(title, 64, "%s (O) %.2f FPS", disp.caption, ptc->fps);
		SDL_WM_SetCaption(title, 0);
    }
#endif
	usleep(100000);
	return EXIT_SUCCESS;
}

int modOpenFinish(control_t *ptc) {
	// stop
	return EXIT_SUCCESS;
}

// CLOSED LOOP ROUTINES //
/************************/

int modClosedInit(control_t *ptc) {
	// set disp source to calib
	disp.dispsrc = DISPSRC_FASTCALIB;		
	// start
	return EXIT_SUCCESS;
}

int modClosedLoop(control_t *ptc) {
	static char title[64];
	int sn;
	
	// Get simulated image for the first WFS
	drvGetImg(ptc, 0);
	
	// dark-flat the whole frame
	MMDarkFlatSubapByte(&(ptc->wfs[0]), &shtrack);

	// try to get the center of gravity 
	//modCogTrack(ptc->wfs[0].corrim, DATA_GSL_M_F, ALIGN_RECT, &shtrack, NULL, NULL);
	modCogTrack(ptc->wfs[0].corr, DATA_UINT8, ALIGN_SUBAP, &shtrack, NULL, NULL);
	
	modCalcCtrl(ptc, &shtrack, 0, -1);
	
	// log offsets measured
	if (ptc->domisclog && shtrack.nsubap > 0) {
		fprintf(ptc->misclog, "C, %ld, %d", ptc->frames, shtrack.nsubap);
		for (sn = 0; sn < shtrack.nsubap; sn++)
			fprintf(ptc->misclog, ", %f, %f", \
					gsl_vector_float_get(shtrack.disp, 2*sn + 0), \
					gsl_vector_float_get(shtrack.disp, 2*sn + 1));
		fprintf(ptc->misclog, "\n");
	}

#ifdef FOAM_SIMDYN_DISPLAY
    if (ptc->frames % ptc->logfrac == 0) {
		displayDraw((&ptc->wfs[0]), &disp, &shtrack);
		displaySDLEvents(&disp);
		logInfo(0, "Current framerate: %.2f FPS", ptc->fps);
		logInfo(0, "Displacements per subapt in (x, y) pairs (%d subaps):", shtrack.nsubap);
		for (sn = 0; sn < shtrack.nsubap; sn++)
			logInfo(LOG_NOFORMAT, "(%.1f,%.1f)", \
			gsl_vector_float_get(shtrack.disp, 2*sn + 0), \
			gsl_vector_float_get(shtrack.disp, 2*sn + 1));

		logInfo(LOG_NOFORMAT, "\n");
		snprintf(title, 64, "%s (C) %.2f FPS", disp.caption, ptc->fps);
		SDL_WM_SetCaption(title, 0);
    }
#endif
	usleep(100000);
	return EXIT_SUCCESS;
}

int modClosedFinish(control_t *ptc) {
	// stop
	return EXIT_SUCCESS;
}

// MISC ROUTINES //
/*****************/

int modCalibrate(control_t *ptc) {
	float stats[3];						// to use with imgGetStats
	FILE *fieldfd;						// to open some files (dark, flat, ...)
	char title[64];						// for the window title
	int i, j, sn;
//	float min, max, sum, pix;			// some fielding stats
	wfs_t *wfsinfo = &(ptc->wfs[0]);	// shortcut
	dispsrc_t oldsrc = disp.dispsrc;	// store the old display source here since we might just have to show dark or flatfields
	int oldover = disp.dispover;		// store the old overlay here

	if (ptc->calmode == CAL_DARK) {
		// take dark frames, and average
		logInfo(0, "Starting darkfield calibration now");

		// simulate the image, it should take care of dark- and
		// flat fielding as well
		drvGetImg(ptc, 0);

		// copy darkfield to darkim
		for (i=0; i<wfsinfo->res.y; i++) 
			 for (j=0; j<wfsinfo->res.x; j++) 
				 gsl_matrix_float_set(wfsinfo->darkim, i, j, simparams.currimg[i*wfsinfo->res.x + j]);

		// saving image for later usage
		fieldfd = fopen(wfsinfo->darkfile, "w+");	
		if (!fieldfd)  {
			logWarn("Could not open darkfield storage file '%s', not saving darkfield (%s).", wfsinfo->darkfile, strerror(errno));
			return EXIT_SUCCESS;
		}
		gsl_matrix_float_fprintf(fieldfd, wfsinfo->darkim, "%.10f");
		fclose(fieldfd);

		imgGetStats(wfsinfo->image, DATA_UINT8, &(wfsinfo->res), -1, stats);
		logInfo(0, "Darkfield calibration done (m: %f, m: %f, avg: %f), and stored to disk.", stats[0], stats[1], stats[2]);
		
		// set new display settings to show the darkfield
		disp.dispsrc = DISPSRC_DARK;
		disp.dispover = 0;
		displayDraw(wfsinfo, &disp, &shtrack);
		snprintf(title, 64, "%s - Darkfield", disp.caption);
		SDL_WM_SetCaption(title, 0);
		
		// reset the display settings
		disp.dispsrc = oldsrc;
		disp.dispover = oldover;
	}
	else if (ptc->calmode == CAL_FLAT) {
		logInfo(0, "Starting flatfield calibration now");

		// simulate the image, it should take care of dark- and
		// flat fielding as well
		drvGetImg(ptc, 0);
		
		// saving image for later usage
		fieldfd = fopen(wfsinfo->flatfile, "w+");	
		if (!fieldfd)  {
			logWarn("Could not open flatfield storage file '%s', not saving flatfield (%s).", wfsinfo->flatfile, strerror(errno));
			return EXIT_SUCCESS;
		}
		gsl_matrix_float_fprintf(fieldfd, wfsinfo->flatim, "%.10f");
		fclose(fieldfd);

		imgGetStats(wfsinfo->image, DATA_UINT8, &(wfsinfo->res), -1, stats);
		logInfo(0, "Flatfield calibration done (m: %f, m: %f, avg: %f), and stored to disk.", stats[0], stats[1], stats[2]);
		
		// set new display settings to show the darkfield
		disp.dispsrc = DISPSRC_FLAT;
		disp.dispover = 0;
		displayDraw(wfsinfo, &disp, &shtrack);
		snprintf(title, 64, "%s - Flatfield", disp.caption);
		SDL_WM_SetCaption(title, 0);
		// reset the display settings
		disp.dispsrc = oldsrc;
		disp.dispover = oldover;
	}
	else if (ptc->calmode == CAL_DARKGAIN) {
		// This part takes the dark and flat fields (darkim and flatim, stored 
		// as gsl matrix in float format) and converts these into convenient 
		// dark and gain fields that can be used later on. Darkim is the average
		// of several darkfields and is multiplied with 256 in a uint16_t array 
		// 'dark'. Flatim is also an average, and is used as: 256 * 
		// mean(flat-dark) / (flat - dark) to produce the uint16_t matrix 'gain'. 
		// Once we have 'dark' and 'gain', dark fielding is done as:
		// (((uint16_t) raw*256)-dark)*gain/256 in that order.
		
		logInfo(0, "Taking dark and flat images to make convenient images to correct (dark/gain).");		
		
		// get mean(flat-dark) value for all subapertures (but not the whole image)
		float tmpavg;
		for (sn=0; sn < shtrack.nsubap; sn++) {
			for (i=0; i< shtrack.track.y; i++) {
				for (j=0; j< shtrack.track.x; j++) {
					tmpavg += (gsl_matrix_float_get(wfsinfo->flatim, shtrack.subc[sn].y + i, shtrack.subc[sn].x + j) - \
						gsl_matrix_float_get(wfsinfo->darkim, shtrack.subc[sn].y + i, shtrack.subc[sn].x + j));
				}
			}
		}
		tmpavg /= ((shtrack.cells.x * shtrack.cells.y) * (shtrack.track.x * shtrack.track.y));
		
		// make actual matrices from dark and flat
		uint16_t *darktmp = (uint16_t *) wfsinfo->dark;
		uint16_t *gaintmp = (uint16_t *) wfsinfo->gain;

		// Here we loop over the subapertures one by one, then calculate 
		// 'dark' as: 256 * rawdark, rawdark the raw average of N darkfields, stored as float
		// 'gain' as: 256 * tmpavg / (rawdark - rawflat), with rawflat the raw avg of N flatfields
		for (sn=0; sn < shtrack.nsubap; sn++) {
			for (i=0; i< shtrack.track.y; i++) {
				for (j=0; j< shtrack.track.x; j++) {
					// dark = 256 * rawdark
					darktmp[sn*(shtrack.track.x*shtrack.track.y) + i*shtrack.track.x + j] = \
						(uint16_t) (256.0 * gsl_matrix_float_get(wfsinfo->darkim, shtrack.subc[sn].y + i, shtrack.subc[sn].x + j));
					// gain = 256 * tmpavg / (rawdark - rawflat)
					gaintmp[sn*(shtrack.track.x*shtrack.track.y) + i*shtrack.track.x + j] = (uint16_t) (256.0 * tmpavg / \
						(gsl_matrix_float_get(wfsinfo->flatim, shtrack.subc[sn].y + i, shtrack.subc[sn].x + j) - \
						 gsl_matrix_float_get(wfsinfo->darkim, shtrack.subc[sn].y + i, shtrack.subc[sn].x + j)));
				}
			}
		}

		logInfo(0, "Dark and gain fields initialized");
	}
	else if (ptc->calmode == CAL_SUBAPSEL) {
		logInfo(0, "Starting subaperture selection now");

		// get a fake image, drvGetImg() knows about CAL_SUBAPSEL
		drvGetImg(ptc, 0);
		uint8_t *tmpimg = (uint8_t *) wfsinfo->image;
		
		// run subapsel on this image
		modSelSubapts(wfsinfo->image, DATA_UINT8, ALIGN_RECT, &shtrack, wfsinfo);

		logInfo(0, "Subaperture selection complete, found %d subapertures.", shtrack.nsubap);

		// set new display settings to show the darkfield
		disp.dispsrc = DISPSRC_RAW;
		disp.dispover = DISPOVERLAY_SUBAPS | DISPOVERLAY_GRID;
		displayDraw(wfsinfo, &disp, &shtrack);
		snprintf(title, 64, "%s - Subaps", disp.caption);
		SDL_WM_SetCaption(title, 0);
		// reset the display settings
		disp.dispsrc = oldsrc;
		disp.dispover = oldover;
	}
	else if (ptc->calmode == CAL_PINHOLE) {
		logInfo(0, "Pinhole calibration, getting WFS reference coordinates now");
		
		// Get a fake image
		drvGetImg(ptc, 0);
		uint8_t *tmpimg = (uint8_t *) wfsinfo->image;
		
		// perform a pinhole calibration
		calibPinhole(ptc, 0, &shtrack);
	}
	else if (ptc->calmode == CAL_INFL) {
		logInfo(0, "Influence matrix calibration.");
		
		// Get a fake image
		drvGetImg(ptc, 0);
		uint8_t *tmpimg = (uint8_t *) wfsinfo->image;
		
		// perform an influence matrix calibration
		calibWFC(ptc, 0, &shtrack);
	}
	
	return EXIT_SUCCESS;
}

int modMessage(control_t *ptc, const client_t *client, char *list[], const int count) {
	// Quick recap of messaging codes:
	// 400 UNKNOWN
	// 401 UNKNOWN MODE
	// 402 MODE REQUIRES ARG
	// 403 MODE FORBIDDEN
	// 300 ERROR
	// 200 OK 
	int tmpint;
	long tmplong;
	float tmpfloat;
	
 	if (strncmp(list[0],"help",3) == 0) {
		// give module specific help here
		if (count > 2) { 
			if (strncmp(list[1], "set",3) == 0 && strncmp(list[2], "err",3) == 0) {
				tellClient(client->buf_ev, "\
						200 OK HELP SET ERR\n\
						set error:\n\
						source [src]:        error source, can be 'seeing', 'wfc', or 'off'.\n\
						-:                   if no prop is given, query the values.\
						");
			}
		}
		else if (count > 1) { 
			
			if (strncmp(list[1], "disp",3) == 0) {
				tellClient(client->buf_ev, "\
200 OK HELP DISPLAY\n\
display <source>:       change the display source.\n\
   <sources:>\n\
   raw:                 direct images from the camera.\n\
   cfull:               full dark/flat corrected images.\n\
   cfast:               fast partial dark/flat corrected images.\n\
   dark:                show the darkfield being used.\n\
   flat:                show the flatfield being used.\n\
   <overlays:>\n\
   subap:               toggle displat of the subapertures.\n\
   grid:                toggle display of the grid.\n\
   vecs:                toggle display of the displacement vectors.\n\
   col [i] [i] [i]:     change the overlay color (OpenGL only).\
");
			}
			else if (strncmp(list[1], "vid",3) == 0) {
				tellClient(client->buf_ev, "\
200 OK HELP VID\n\
vid <mode> [val]:       configure the video output.\n\
   auto:                use auto contrast/brightness.\n\
   c [i]:               use manual c/b with this contrast.\n\
   b [i]:               use manual c/b with this brightness.\
");
			}
			else if (strncmp(list[1], "set",3) == 0) {
				tellClient(client->buf_ev, "\
200 OK HELP SET\n\
set [prop] [val]:       set or query property values.\n\
   lf [i]:              set the logfraction.\n\
   ff [i]:              set the number of frames to use for dark/flats.\n\
   seeingfac [f]:       set the seeing factor (0--1).\n\
   err:                 set simulated error related settings.\n\
   windx [i]:           set the wind in x direction (pixels/frame).\n\
   windy [i]:           set the wind in y direction (pixels/frame).\n\
   samini [f]:          set the minimum intensity for subapt selection.\n\
   samxr [i]:           set maxr used for subapt selection.\n\
   -:                   if no prop is given, query the values.\
");
			}
			else if (strncmp(list[1], "cal",3) == 0) {
				tellClient(client->buf_ev, "\
200 OK HELP CALIBRATE\n\
calibrate <mode>:       calibrate the ao system.\n\
   dark:                take a darkfield by averaging %d frames.\n\
   flat:                take a flatfield by averaging %d frames.\n\
   gain:                calc dark/gain to do actual corrections with.\n\
   subap:               select some subapertures.\n\
   pinhole:             select reference coordinates for WFS.\n\
   influence:           calibrate the influence matrix.\n\
", ptc->wfs[0].fieldframes, ptc->wfs[0].fieldframes);
			}
			else // we don't know. tell this to parseCmd by returning 0
				return 0;
		}
		else {
			tellClient(client->buf_ev, "\
=== prime module options ===\n\
display <source>:       tell foam what display source to use.\n\
vid <auto|c|v> [i]:     use autocontrast/brightness, or set manually.\n\
set [prop]:             set or query certain properties.\n\
calibrate <mode>:       calibrate the ao system (dark, flat, subapt, etc).\n\
saveimg [i]:            save the next i frames to disk.\
");
		}
	}
#ifdef FOAM_SIMDYN_DISPLAY
 	else if (strncmp(list[0], "disp",3) == 0) {
		if (count > 1) {
			if (strncmp(list[1], "raw",3) == 0) {
				tellClient(client->buf_ev, "200 OK DISPLAY RAW");
				disp.dispsrc = DISPSRC_RAW;
			}
			else if (strncmp(list[1], "cfull",3) == 0) {
				disp.dispsrc = DISPSRC_FULLCALIB;
				tellClient(client->buf_ev, "200 OK DISPLAY CALIB");
			}
			else if (strncmp(list[1], "cfast",3) == 0) {
				disp.dispsrc = DISPSRC_FASTCALIB;
				tellClient(client->buf_ev, "200 OK DISPLAY CALIB");
			}
			else if (strncmp(list[1], "grid",3) == 0) {
				logDebug(0, "overlay was: %d, is: %d, mask: %d", disp.dispover, disp.dispover ^ DISPOVERLAY_GRID, DISPOVERLAY_GRID);
				disp.dispover ^= DISPOVERLAY_GRID;
				tellClient(client->buf_ev, "200 OK TOGGLING GRID OVERLAY");
			}
			else if (strncmp(list[1], "subaps",3) == 0) {
				disp.dispover ^= DISPOVERLAY_SUBAPS;
				tellClient(client->buf_ev, "200 OK TOGGLING SUBAPERTURE OVERLAY");
			}
			else if (strncmp(list[1], "vectors",3) == 0) {
				disp.dispover ^= DISPOVERLAY_VECTORS;
				tellClient(client->buf_ev, "200 OK TOGGLING DISPLACEMENT VECTOR OVERLAY");
			}
			else if (strncmp(list[1], "col",3) == 0) {
				if (count > 4) {
					disp.col.r = strtol(list[2], NULL, 10);
					disp.col.g = strtol(list[3], NULL, 10);
					disp.col.b = strtol(list[4], NULL, 10);
					tellClient(client->buf_ev, "200 OK COLOR IS NOW (%d,%d,%d)", disp.col.r, disp.col.g, disp.col.b);
				}
				else {
					tellClient(client->buf_ev, "402 COLOR REQUIRES RGB FLOAT TRIPLET");
				}
			}
			else if (strncmp(list[1], "dark",3) == 0) {
				if  (ptc->wfs[0].darkim == NULL) {
					tellClient(client->buf_ev, "400 ERROR DARKFIELD NOT AVAILABLE");
				}
				else {
					disp.dispsrc = DISPSRC_DARK;
					tellClient(client->buf_ev, "200 OK DISPLAY DARK");
				}
			}
			else if (strncmp(list[1], "flat",3) == 0) {
				if  (ptc->wfs[0].flatim == NULL) {
					tellClient(client->buf_ev, "400 ERROR FLATFIELD NOT AVAILABLE");
				}
				else {
					disp.dispsrc = DISPSRC_FLAT;
					tellClient(client->buf_ev, "200 OK DISPLAY FLAT");
				}
			}
			else {
				tellClient(client->buf_ev, "401 UNKNOWN DISPLAY");
			}
		}
		else {
			tellClient(client->buf_ev, "402 DISPLAY REQUIRES ARGS");
		}
	}
#endif
	else if (strcmp(list[0],"saveimg") == 0) {
		if (count > 1) {
			tmplong = strtol(list[1], NULL, 10);
			ptc->saveimg = tmplong;
			tellClient(client->buf_ev, "200 OK SAVING NEXT %ld IMAGES", tmplong);
		}
		else {
			tellClient(client->buf_ev,"402 SAVEIMG REQUIRES ARG (# FRAMES)");
		}		
	}		
 	else if (strncmp(list[0], "set",3) == 0) {
		if (count > 2) {
			tmpint = strtol(list[2], NULL, 10);
			tmpfloat = strtof(list[2], NULL);
			if (strcmp(list[1], "lf") == 0) {
				ptc->logfrac = tmpint;
				tellClient(client->buf_ev, "200 OK SET LOGFRAC TO %d", tmpint);
			}
			else if (strcmp(list[1], "ff") == 0) {
				ptc->wfs[0].fieldframes = tmpint;
				tellClient(client->buf_ev, "200 OK SET FIELDFRAMES TO %d", tmpint);
			}
			else if (strcmp(list[1], "windx") == 0) {
				simparams.wind.x = tmpint;
				tellClient(client->buf_ev, "200 OK SET WIND X TO %d", tmpint);
			}
			else if (strcmp(list[1], "windy") == 0) {
				simparams.wind.y = tmpint;
				tellClient(client->buf_ev, "200 OK SET WIND Y TO %d", tmpint);
			}
			else if (strcmp(list[1], "see") == 0) {
				simparams.seeingfac = tmpfloat;
				tellClient(client->buf_ev, "200 OK SET SEEINGFACTOR TO %f", tmpfloat);
			}
			else if (strcmp(list[1], "noise") == 0) {
				simparams.noise = tmpint;
				tellClient(client->buf_ev, "200 OK SET NOISE TO %d", tmpint);
			}
			else if (strcmp(list[1], "corr") == 0) {
				if (tmpint >= ptc->wfc_count) {
					tellClient(client->buf_ev, "400 WFC %d INVALID", tmpint);
				}
				else {
					simparams.corr = &(ptc->wfc[tmpint]);
					tellClient(client->buf_ev, "200 OK USING WFC %d FOR CORRECTION", tmpint);
				}
			}
			else if (strcmp(list[1], "err") == 0) {
				if (strcmp(list[2], "see") == 0) {
					simparams.error = ERR_SEEING;
					tellClient(client->buf_ev, "200 OK SET ERROR TO SEEING");
				}
				else if (strcmp(list[2], "wfc") == 0) {
					simparams.error = ERR_WFC;
					if (count > 3) {
						tmpint = strtol(list[3], NULL, 10);
						if (tmpint >= 0 && tmpint < ptc->wfc_count)  {
							simparams.errwfc = &(ptc->wfc[tmpint]);
							tellClient(client->buf_ev, "200 OK SET ERROR TO WFC %d", tmpint);
						}
						else {
							simparams.errwfc = &(ptc->wfc[0]);
							tellClient(client->buf_ev, "400 WFC %d INVALID, DEFAULTING TO 0", tmpint);
						}

					}
					else if (simparams.errwfc == NULL) {
						simparams.errwfc = &(ptc->wfc[0]);
						tellClient(client->buf_ev, "200 OK SET ERROR TO WFC 0");
					}
				}
				else if (strcmp(list[2], "off") == 0) {
					simparams.error = ERR_NONE;
					tellClient(client->buf_ev, "200 OK DISABLED ERROR");
				}
				else
					tellClient(client->buf_ev, "400 UNKNOWN ERROR SOURCE");
			}
			else if (strcmp(list[1], "samini") == 0) {
				shtrack.samini = tmpfloat;
				tellClient(client->buf_ev, "200 OK SET SAMINI TO %.2f", tmpfloat);
			}
			else if (strcmp(list[1], "samxr") == 0) {
				shtrack.samxr = tmpint;
				tellClient(client->buf_ev, "200 OK SET SAMXR TO %d", tmpint);
			}
			else {
				tellClient(client->buf_ev, "401 UNKNOWN PROPERTY, CANNOT SET");
			}
		}
		else {
			tellClient(client->buf_ev, "200 OK VALUES AS FOLLOWS:\n\
logfrac (lf):           %d\n\
datalogging (data):     %d\n\
fieldframes (ff):       %d\n\
SH array:               %dx%d cells\n\
cell size:              %dx%d pixels\n\
track size:             %dx%d pixels\n\
ccd size:               %dx%d pixels\n\
error source:           %d\n\
error WFC:              %d\n\
noise:                  %d\n\
correcting WFC:         %d\n\
seeingfac:              %f\n\
wind (x,y):             (%d,%d)\n\
samxr:                  %d\n\
samini:                 %.2f",\
ptc->logfrac, \
ptc->domisclog, \
ptc->wfs[0].fieldframes, \
shtrack.cells.x, shtrack.cells.y,\
shtrack.shsize.x, shtrack.shsize.y, \
shtrack.track.x, shtrack.track.y, \
ptc->wfs[0].res.x, ptc->wfs[0].res.y, \
simparams.error, \
simparams.errwfc->id, \
simparams.noise, \
simparams.corr->id, \
simparams.seeingfac, \
simparams.wind.x, simparams.wind.y, \
shtrack.samxr, \
shtrack.samini);
		}
	}
 	else if (strncmp(list[0], "vid",3) == 0) {
		if (count > 1) {
			if (strncmp(list[1], "auto",3) == 0) {
				disp.autocontrast = 1;
				tellClient(client->buf_ev, "200 OK USING AUTO SCALING");
			}
			else if (strcmp(list[1], "c") == 0) {
				if (count > 2) {
					tmpfloat = strtof(list[2], NULL);
					disp.autocontrast = 0;
					disp.contrast = tmpfloat;
					tellClient(client->buf_ev, "200 OK CONTRAST %f", tmpfloat);
				}
				else {
					tellClient(client->buf_ev, "402 NO CONTRAST GIVEN");
				}
			}
			else if (strcmp(list[1], "b") == 0) {
				if (count > 2) {
					tmpint = strtol(list[2], NULL, 10);
					disp.autocontrast = 0;
					disp.brightness = tmpint;
					tellClient(client->buf_ev, "200 OK BRIGHTNESS %d", tmpint);
				}
				else {
					tellClient(client->buf_ev, "402 NO BRIGHTNESS GIVEN");
				}
			}
			else {
				tellClient(client->buf_ev, "401 UNKNOWN VID");
			}
		}
		else {
			tellClient(client->buf_ev, "402 VID REQUIRES ARGS");
		}
	}
 	else if (strncmp(list[0], "cal",3) == 0) {
		if (count > 1) {
			if (strncmp(list[1], "dark",3) == 0) {
				ptc->mode = AO_MODE_CAL;
				ptc->calmode = CAL_DARK;
                tellClient(client->buf_ev, "200 OK DARKFIELDING NOW");
				pthread_cond_signal(&mode_cond);
				// add message to the users
			}
			else if (strncmp(list[1], "subap",3) == 0) {
				ptc->mode = AO_MODE_CAL;
				ptc->calmode = CAL_SUBAPSEL;
                tellClient(client->buf_ev, "200 OK SELECTING SUBAPTS");
				pthread_cond_signal(&mode_cond);
			}
			else if (strncmp(list[1], "flat",3) == 0) {
				ptc->mode = AO_MODE_CAL;
				ptc->calmode = CAL_FLAT;
				tellClient(client->buf_ev, "200 OK FLATFIELDING NOW");
				pthread_cond_signal(&mode_cond);
			}
			else if (strncmp(list[1], "gain",3) == 0) {
				ptc->mode = AO_MODE_CAL;
				ptc->calmode = CAL_DARKGAIN;
				tellClient(client->buf_ev, "200 OK CALCULATING DARK/GAIN NOW");
				pthread_cond_signal(&mode_cond);
			}			
			else if (strncmp(list[1], "pinhole",3) == 0) {
				ptc->mode = AO_MODE_CAL;
				ptc->calmode = CAL_PINHOLE;
				tellClient(client->buf_ev, "200 OK PINHOLE CALIBRATION NOW");
				pthread_cond_signal(&mode_cond);
			}
			else if (strncmp(list[1], "influence",3) == 0) {
				ptc->mode = AO_MODE_CAL;
				ptc->calmode = CAL_INFL;
				tellClient(client->buf_ev, "200 OK INFLUENCE CALIBRATION NOW");
				pthread_cond_signal(&mode_cond);
			}			
			else {
				tellClient(client->buf_ev, "401 UNKNOWN CALIBRATION");
			}
		}
		else {
			tellClient(client->buf_ev, "402 CALIBRATE REQUIRES ARGS");
		}
	}
	else { // no valid command found? return 0 so that the main thread knows this
		return 0;
	} // strcmp stops here
	
	// if we end up here, we didn't return 0, so we found a valid command
	return 1;
}

// SITE-SPECIFIC ROUTINES //
/**************************/

int drvSetActuator(control_t *ptc, int wfc) {
	// Not a lot of actuating to be done for simulation, since 
	// we do not need to 'set' anything. If any control vector
	// is set, it will be used automatically in drvGetImg
	
	return EXIT_SUCCESS;
}

int drvSetupHardware(control_t *ptc, aomode_t aomode, calmode_t calmode) {
	// This function is nothing more than displaying some info. Everything
	// is handled in drvGetImg, which checks calibration modes and
	// handles those settings accordingly (i.e. give dark image back
	// during dark fielding, flat during flat, etc).
    if (aomode == AO_MODE_CAL) {
        if (calmode == CAL_DARK) {
            logInfo(0, "Configuring hardware for darkfield calibration");
        }
        else if (calmode == CAL_FLAT) {
            logInfo(0, "Configuring hardware for flatfield calibration");
        }
        else if (calmode == CAL_INFL) {
            logInfo(0, "Configuring hardware for influence matrix calibration");
        }
        else if (calmode == CAL_PINHOLE) {
            logInfo(0, "Configuring hardware for subaperture reference calibration");
        }
        else {
            logWarn("No special setup needed for this calibration mode, ignored");
        }
    }
    else if (aomode == AO_MODE_OPEN || aomode == AO_MODE_CLOSED) {
        logInfo(0, "Configuring hardware for open/closed loop mode calibration");
    }
    else {
        logWarn("No special setup needed for this aomode, ignored");
    }        
    
    return EXIT_SUCCESS;
}

int MMAvgFramesByte(control_t *ptc, gsl_matrix_float *output, wfs_t *wfs, int rounds) {
	int k, i, j;
	float min, max, sum, tmpvar;
	uint8_t *imgsrc;
    logDebug(0, "Averaging %d frames now (dark, flat, whatever)", rounds);
	
	gsl_matrix_float_set_zero(output);
	for (k=0; k<rounds; k++) {
		if ((k % (rounds/10)) == 0 && k > 0)
			logDebug(0 , "Frame %d", k);
		
		drvGetImg(ptc, 0);
		imgsrc = (uint8_t *) wfs->image;
		
		for (i=0; i<wfs->res.y; i++) {
			for (j=0; j<wfs->res.x; j++) {
				tmpvar = gsl_matrix_float_get(output, i, j) + (float) imgsrc[i*wfs->res.x +j];
				gsl_matrix_float_set(output, i, j, tmpvar);
			}
		}
	}
	
	gsl_matrix_float_scale( output, 1/(float) rounds);
	gsl_matrix_float_minmax(output, &min, &max);
	for (j=0; j<wfs->res.x; j++) 
		for (i=0; i<wfs->res.y; i++) 
			sum += gsl_matrix_float_get(output, i, j);
	
	logDebug(0, "Result: min: %.2f, max: %.2f, sum: %.2f, avg: %.2f", \
			 min, max, sum, sum/(wfs->res.x * wfs->res.y) );
	
	return EXIT_SUCCESS;
}

// Dark flat calibration, only for subapertures we found previously
int MMDarkFlatSubapByte(wfs_t *wfs, mod_sh_track_t *shtrack) {
	// if correct, CAL_DARKGAIN was run before this is called,
	// and we have wfs->dark and wfs->gain to scale the image with like
	// corrected = ((img*256 - dark) * gain)/256. In ASM (MMX/SSE2)
	// this can be done relatively fast (see ao3.c)
	int sn, i, j, off;
	uint32_t tmp;
	uint8_t *tsrc = (uint8_t *) wfs->image;
	uint16_t *tdark = (uint16_t *) wfs->dark;
	uint16_t *tgain = (uint16_t *) wfs->gain;
	uint8_t *tcorr = (uint8_t *) wfs->corr;
	
	for (sn=0; sn< shtrack->nsubap; sn++) {
		// we loop over each subaperture, and correct only these
		// pixels, because we only measure those anyway, so it's
		// faster
		off = sn * (shtrack->track.x * shtrack->track.y);
		tsrc = ((uint8_t *) wfs->image) + shtrack->subc[sn].y * wfs->res.x + shtrack->subc[sn].x;
		for (i=0; i<shtrack->track.y; i++) {
			for (j=0; j<shtrack->track.x; j++) {
				// we're simulating saturated calculations here
				// with quite some effort. This goes must better
				// in MMX/SSE2.
				
				tmp = (( ((uint16_t) tsrc[off+i*wfs->res.x + j]) << 8) - tdark[off+i*shtrack->track.x + j]); 
				
				// Here we check if src - dark < 0, and we set 
				// the pixel to zero if true
				if (tmp > (tsrc[off+i*wfs->res.x + j] << 8))
					tmp = 0;
					//tcorr[off+i*shtrack->track.x + j] = 0;
				// Here we check if we overflow the pixel range
				// after applying a gain, if so we set the pixel
				// to 255
				else if ((tmp = ((tmp * tgain[off+i*shtrack->track.x + j]) >> 16)) > 255)
					tmp = 255;
					//tcorr[off+i*shtrack->track.x + j] = 255;
				// if none of this happens, we just set the pixel
				// to the value that it should have
				else
					tmp = 1;
					//tcorr[off+i*shtrack->track.x + j] = tmp;
				
				// TvW 2008-07-02, directly copy raw to corr for the moment, this is statsim anyway
				// leaving the above intact will give a hint on the performance of this method though,
				// although the results of the above statements are thrown away
				tcorr[off+i*shtrack->track.x + j] = tsrc[i*wfs->res.x + j];//- tdark[off+i*shtrack->track.x + j];
			}
		}
	}
//	float srcst[3];
//	float corrst[3];
//	imgGetStats(wfs->corr, DATA_UINT16, NULL, shtrack->nsubap * shtrack->track.x * shtrack->track.y, corrst);
//	imgGetStats(wfs->image, DATA_UINT8, &(wfs->res), -1, srcst);
//	logDebug(LOG_SOMETIMES, "SUBCORR: corr: min: %f, max: %f, avg: %f", corrst[0], corrst[1], corrst[2]);
//	logDebug(LOG_SOMETIMES, "SUBCORR: src: min: %f, max: %f, avg: %f", srcst[0], srcst[1], srcst[2]);
	
	return EXIT_SUCCESS;
}

// Dark flat calibration, currently only does raw-dark
int MMDarkFlatFullByte(wfs_t *wfs, mod_sh_track_t *shtrack) {
	logDebug(LOG_SOMETIMES, "Slow full-frame darkflat correcting now");
	size_t i, j; // size_t because gsl wants this
	uint8_t* imagesrc = (uint8_t*) wfs->image;
	
	if (wfs->darkim == NULL || wfs->flatim == NULL || wfs->corrim == NULL) {
		logWarn("Dark, flat or correct image memory not available, please calibrate first");
		return EXIT_FAILURE;
	}
	// copy the image to corrim, while doing dark/flat fielding at the same time
	float pix1, pix2;
	for (i=0; (int) i < wfs->res.y; i++) {
		for (j=0; (int) j < wfs->res.x; j++) {
			// pix 1 is flat - dark
			pix1 = (gsl_matrix_float_get(wfs->flatim, i, j) - \
					gsl_matrix_float_get(wfs->darkim, i, j));
			// pix 2 is max(raw - dark, 0)
			pix2 = fmax(imagesrc[i*wfs->res.x +j] - \
						gsl_matrix_float_get(wfs->darkim, i, j), 0);
			// if flat - dark is 0, we set the output to zero to prevent 
			// dividing by zero, otherwise we take max(pix2 / pix1, 255)
			// we multiply by 128 because (raw-dark)/(flat-dark) is
			// 1 on average (I guess), multiply by static 128 to get an image
			// at all. Actually this should be average(flat-dark), but that's
			// too expensive here, this should work fine :P
			if (pix1 <= 0)
				gsl_matrix_float_set(wfs->corrim, i, j, imagesrc[i*wfs->res.x +j]);
				//gsl_matrix_float_set(wfs->corrim, i, j, 0.0);
			else 
				gsl_matrix_float_set(wfs->corrim, i, j, imagesrc[i*wfs->res.x +j]);
				//gsl_matrix_float_set(wfs->corrim, i, j, \
									 fmin(128 * pix2 / pix1, 255));
			gsl_matrix_float_set(wfs->corrim, i, j, imagesrc[i*wfs->res.x +j]);
			
		}
	}
//	float corrstats[3];
//	float srcstats[3];
//	imgGetStats(imagesrc, DATA_UINT8, &(wfs->res), -1, srcstats);
//	imgGetStats(wfs->corrim, DATA_GSL_M_F, &(wfs->res), -1, corrstats);	
//	logDebug(LOG_SOMETIMES, "FULLCORR: src: min %f, max %f, avg %f", srcstats[0], srcstats[1], srcstats[2]);
//	logDebug(LOG_SOMETIMES, "FULLCORR: corr: min %f, max %f, avg %f", corrstats[0], corrstats[1], corrstats[2]);

	return EXIT_SUCCESS;
}


int drvGetImg(control_t *ptc, int wfs) {
	int i;
	if (ptc->mode == AO_MODE_CAL) {
		if (ptc->calmode == CAL_DARKGAIN || ptc->calmode == CAL_DARK) {
			// give flat 0 intensity image back
			if (simFlat(&simparams, 0) != EXIT_SUCCESS)
				return EXIT_FAILURE;

			// add some noise, if requested
			if (simparams.noise != 0)
				if (simNoise(&simparams, simparams.noise) != EXIT_SUCCESS)
					return EXIT_FAILURE;
		}
		else if (ptc->calmode == CAL_FLAT) {
			// give flat 32 intensity image back
			if (simFlat(&simparams, 32) != EXIT_SUCCESS)
				return EXIT_FAILURE;
			
			// add some noise, if requested
			if (simparams.noise != 0)
				if (simNoise(&simparams, simparams.noise) != EXIT_SUCCESS)
					return EXIT_FAILURE;
		}
		else if (ptc->calmode == CAL_PINHOLE || ptc->calmode == CAL_SUBAPSEL) {
			// take flat 32 intensity image, and pass through simTel, simSHWFS
			if (simFlat(&simparams, 32) != EXIT_SUCCESS)
				return EXIT_FAILURE;

			
			if (simTel(&simparams) != EXIT_SUCCESS)
				return EXIT_FAILURE;

			if (simSHWFS(&simparams, &shtrack) != EXIT_SUCCESS)
				return EXIT_FAILURE;
			
			// add some noise, if requested
			if (simparams.noise != 0)
				if (simNoise(&simparams, simparams.noise) != EXIT_SUCCESS)
					return EXIT_FAILURE;
		}
		else if (ptc->calmode == CAL_INFL) {
			// take flat 32 intensity image, and pass through simTel, simWFC and simSHWFS
			if (simFlat(&simparams, 32) != EXIT_SUCCESS)
				return EXIT_FAILURE;
			
			
			if (simWFC(simparams.corr, &simparams) != EXIT_SUCCESS)
				return EXIT_FAILURE;
			
			if (simTel(&simparams) != EXIT_SUCCESS)
				return EXIT_FAILURE;

			if (simSHWFS(&simparams, &shtrack) != EXIT_SUCCESS)
				return EXIT_FAILURE;			
			
			// add some noise, if requested
			if (simparams.noise != 0)
				if (simNoise(&simparams, simparams.noise) != EXIT_SUCCESS)
					return EXIT_FAILURE;
		}
	}
	else { // if we're not calibrating, return a normal image:
		if (simparams.error == ERR_SEEING) {
			// Simulate wind (i.e. get the new origin to crop from)
			if (simWind(&simparams) != EXIT_SUCCESS)
				return EXIT_FAILURE;
			
			// Simulate atmosphere, i.e. get a crop of the wavefront
			if (simAtm(&simparams) != EXIT_SUCCESS)
				return EXIT_FAILURE;
			logDebug(LOG_SOMETIMES, "Simulate seeing as error");
		}
		else if (simparams.error == ERR_WFC && simparams.errwfc != NULL) {
			// log simulated error
			/*
			if (ptc->domisclog) {
				fprintf(ptc->misclog, "WFC ERR, %d, %d", simparams.errwfc->id, simparams.errwfc->nact);
				for (i=0; i<simparams.errwfc->nact; i++)
					fprintf(ptc->misclog, ", %f", \
							gsl_vector_float_get(simctrl,i));

				fprintf(ptc->misclog, "\n");
			}
			*/
			// Simulate a WFC error
			if (simWFCError(&simparams, simparams.errwfc, 1, 40) != EXIT_SUCCESS)
				return EXIT_FAILURE;

			logDebug(LOG_SOMETIMES, "Use a WFC (%s) as error", simparams.errwfc->name);
		}
		else if (simparams.error == ERR_NONE) {

			if (simFlat(&simparams, 32) != EXIT_SUCCESS)
				return EXIT_FAILURE;

			logDebug(LOG_SOMETIMES, "No error, flat WF");
		}

		if (simparams.corr != NULL) {
			// log WFC signal
			if (ptc->domisclog) {
				fprintf(ptc->misclog, "WFC CORR, %d, %d", simparams.corr->id, simparams.corr->nact);
				for (i=0; i<simparams.corr->nact; i++)
					fprintf(ptc->misclog, ", %f", \
							gsl_vector_float_get(simparams.corr->ctrl,i));

				fprintf(ptc->misclog, "\n");
			}
			// Simulate the WFCs themselves
			if (simWFC(simparams.corr, &simparams) != EXIT_SUCCESS)
				return EXIT_FAILURE;
		}
		
		// Simulate telescope aperture
		if (simTel(&simparams) != EXIT_SUCCESS)
			return EXIT_FAILURE;
		
		// Simulate SH WFS
		if (simSHWFS(&simparams, &shtrack) != EXIT_SUCCESS)
			return EXIT_FAILURE;

		if (simparams.noise != 0) {
			logDebug(LOG_SOMETIMES, "Noise with amp %d", simparams.noise);
			if (simNoise(&simparams, simparams.noise) != EXIT_SUCCESS)
				return EXIT_FAILURE;
		}


	}
	
	if (ptc->saveimg > 0) { // user wants to save images, do so now!
		char *fname;
		asprintf(&fname, "foam-" FOAM_CONFIG_PRE "-cap-%05ld.pgm", ptc->capped);
		modWritePGMArr(fname, simparams.currimg, DATA_UINT8, simparams.currimgres, 0, 1);
		ptc->capped++;
		ptc->saveimg--;
	}
		
	//ptc->wfs[0].image = (void *) simparams.currimg;
	return EXIT_SUCCESS;
}
