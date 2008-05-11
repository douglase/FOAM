/*! 
 @file foam_primemod-mcmath.c
 @author @authortim
 @date 2008-04-18
 
 @brief This is the McMath prime-module which can be used at that telescope.
 */

// HEADERS //
/***********/

// We need these for modMessage, see foam_cs.c
extern pthread_mutex_t mode_mutex;
extern pthread_cond_t mode_cond;

//#define FOAM_MCMATH_DISPLAY 1
//#undef FOAM_MCMATH_DISPLAY

// GLOBALS //
/***********/

// Displaying
#ifdef FOAM_MCMATH_DISPLAY

// these are done at compile time (in Makefile)
//#define FOAM_MODULES_DISLAY_OPENGL
//#ifndef FOAM_MODULES_DISLAY_SHSUPPORT
//#define FOAM_MODULES_DISLAY_SHSUPPORT
//#endif

mod_display_t disp;
#endif

// ITIFG camera & buffer
mod_itifg_cam_t dalsacam;
mod_itifg_buf_t buffer;

// DAQboard types
mod_daq2k_board_t daqboard;

// Okotech DM type
mod_okodm_t okodm;

// Shack Hartmann tracking info
mod_sh_track_t shtrack;

// field images here. 
// We store darkfield * 256 in dark, 256 * (1/(flatfield-darkfield)) in
// gain, and then calculate corrim = (raw*256 - dark) * gain / 256
#define MAXSUBAPS 16*16
#define MAXSUBAPSIZE 16*16
uint8_t corrim[MAXSUBAPS * MAXSUBAPSIZE] __attribute__ ((aligned (32)));
uint16_t dark[MAXSUBAPS * MAXSUBAPSIZE] __attribute__ ((aligned (32)));
uint16_t gain[MAXSUBAPS * MAXSUBAPSIZE] __attribute__ ((aligned (32)));

int modInitModule(control_t *ptc, config_t *cs_config) {
	logInfo(0, "This is the McMath-Pierce prime module, enjoy.");
	
	// populate ptc here
	ptc->mode = AO_MODE_LISTEN;			// start in listen mode (safe bet, you probably want this)
	ptc->calmode = CAL_INFL;			// this is not really relevant initialliy
	ptc->logfrac = 100;                 // log verbose messages only every 100 frames
	ptc->wfs_count = 1;					// 2 FW, 1 WFS and 2 WFC
	ptc->wfc_count = 2;
	ptc->fw_count = 2;
	
	// allocate memory for filters, wfcs and wfss
	// use malloc to make the memory globally available
	ptc->filter = (filtwheel_t *) calloc(ptc->fw_count, sizeof(filtwheel_t));
	ptc->wfc = (wfc_t *) calloc(ptc->wfc_count, sizeof(wfc_t));
	ptc->wfs = (wfs_t *) calloc(ptc->wfs_count, sizeof(wfs_t));
	
	// configure WFS 0
	ptc->wfs[0].name = "SH WFS";
	ptc->wfs[0].res.x = 256;
	ptc->wfs[0].res.y = 256;
	ptc->wfs[0].bpp = 8;
	// this is where we will look for dark/flat/sky images
	ptc->wfs[0].darkfile = "mcmath_dark.gsldump";	
	ptc->wfs[0].flatfile = "mcmath_flat.gsldump";
	ptc->wfs[0].skyfile = "mcmath_sky.gsldump";
	ptc->wfs[0].scandir = AO_AXES_XY;
    	ptc->wfs[0].id = 0;
	ptc->wfs[0].fieldframes = 1000;     // take 1000 frames for a dark or flatfield
	
	// configure WFC 0
	ptc->wfc[0].name = "Okotech DM";
	ptc->wfc[0].nact = 37;
	ptc->wfc[0].gain.p = 1.0;
	ptc->wfc[0].gain.i = 1.0;
	ptc->wfc[0].gain.d = 1.0;
	ptc->wfc[0].type = WFC_DM;
    ptc->wfc[0].id = 0;
	
	// configure WFC 1
	ptc->wfc[1].name = "TT";
	ptc->wfc[1].nact = 2;
	ptc->wfc[1].gain.p = 1.0;
	ptc->wfc[1].gain.i = 1.0;
	ptc->wfc[1].gain.d = 1.0;
	ptc->wfc[1].type = WFC_TT;
    ptc->wfc[1].id = 1;
	
	// configure filter 0
	ptc->filter[0].name = "Telescope FW";
	ptc->filter[0].id = 0;
    ptc->filter[0].delay = 2;
	ptc->filter[0].nfilts = 4;
	ptc->filter[0].filters[0] = FILT_PINHOLE;
	ptc->filter[0].filters[1] = FILT_OPEN;
    ptc->filter[0].filters[2] = FILT_TARGET;
    ptc->filter[0].filters[3] = FILT_CLOSED;
	
	ptc->filter[1].name = "WFS FW";
	ptc->filter[1].id = 1;
	ptc->filter[1].nfilts = 2;
    ptc->filter[1].delay = 2;
    ptc->filter[1].filters[0] = FILT_PINHOLE;
	ptc->filter[1].filters[1] = FILT_OPEN;
	
	// configure ITIFG camera & buffer
	
	dalsacam.module = 48;
	dalsacam.device_name = "/dev/ic0dma";
	dalsacam.config_file = "../config/dalsa-cad6-pcd.cam";
	
	buffer.frames = 8;
	
	// configure the daqboard
	
	daqboard.device = "daqBoard2k0";	// we use the first daqboard
	daqboard.nchans = 4;				// we use 4 analog chans [-10, 10] V
	daqboard.minvolt = -10.0;
	daqboard.maxvolt = 10.0;
	daqboard.iop2conf[0] = 0;
	daqboard.iop2conf[1] = 0;
	daqboard.iop2conf[2] = 1;
	daqboard.iop2conf[3] = 1;			// use digital IO ports for {out, out, in, in}
	
	// configure DM here
	
	okodm.minvolt = 0;					// nice voltage range is 0--255, middle is 180
	okodm.midvolt = 180;
	okodm.maxvolt = 255;
	okodm.nchan = 38;					// 37 acts + substrate = 38 channels
	okodm.port = "/dev/port";			// access pci board here
	okodm.pcioffset = 4;				// offset is 4 (sizeof(int)?)
	okodm.pcibase[0] = 0xc000;			// base addresses from lspci -v
	okodm.pcibase[1] = 0xc400;
	okodm.pcibase[2] = 0xffff;
	okodm.pcibase[3] = 0xffff;
	
	// shtrack configuring
    // we have a CCD of WxH, with a lenslet array of WlxHl, such that
    // each lenslet occupies W/Wl x H/Hl pixels, and we use track.x x track.y
    // pixels to track the CoG or do correlation tracking.
	shtrack.cells.x = 16;				// we're using a 8x8 lenslet array
	shtrack.cells.y = 16;
	shtrack.shsize.x = ptc->wfs[0].res.x/shtrack.cells.x;
	shtrack.shsize.y = ptc->wfs[0].res.y/shtrack.cells.y;
	shtrack.track.x = shtrack.shsize.x/2;   // tracker windows are half the size of the lenslet grid things
	shtrack.track.y = shtrack.shsize.y/2;
	shtrack.pinhole = "mcmath_pinhole.gsldump";
	shtrack.influence = "mcmath_influence.gsldump";
	shtrack.samxr = -1;			// 1 row edge erosion
	shtrack.samini = 10;			// minimum intensity for subaptselection 10
	// init the shtrack module now
	modInitSH(&shtrack);	
	
	// configure cs_config here
	cs_config->listenip = "0.0.0.0";	// listen on any IP by defaul
	cs_config->listenport = 10000;		// listen on port 10000 by default
	cs_config->use_syslog = false;		// don't use the syslog
	cs_config->syslog_prepend = "foam-mm";	// prepend logging with 'foam-mm'
	cs_config->use_stdout = true;		// do use stdout
	cs_config->loglevel = LOGDEBUG;		// log error, info and debug
	cs_config->infofile = NULL;			// don't log anything to file
	cs_config->errfile = NULL;
	cs_config->debugfile = NULL;
	


	drvInitBoard(&dalsacam);
	drvInitBufs(&buffer, &dalsacam);
	
	// update the pointer to the wfs image
	ptc->wfs[0].image = buffer.data;
	
	return EXIT_SUCCESS;
}

int modPostInitModule(control_t *ptc, config_t *cs_config) {
	// we initialize OpenGL here, because it doesn't like threading
	// a lot
#ifdef FOAM_MCMATH_DISPLAY
	// init display
	disp.caption = "WFS #1";
	disp.res.x = ptc->wfs[0].res.x;
	disp.res.y = ptc->wfs[0].res.y;
	disp.autocontrast = 0;
	disp.brightness = 0;
	disp.contrast = 5;
	disp.dispsrc = DISPSRC_RAW;         // use the raw ccd output
	disp.dispover = DISPOVERLAY_GRID;   // display the SH grid
	disp.col.r = 255;
	disp.col.g = 255;
	disp.col.b = 255;
	
	displayInit(&disp);
#endif
	return EXIT_SUCCESS;
}

void modStopModule(control_t *ptc) {
#ifdef FOAM_MCMATH_DISPLAY
	displayFinish(&disp);
#endif
	
	drvStopGrab(&dalsacam);
	drvStopBufs(&buffer, &dalsacam);
	drvStopBoard(&dalsacam);
}

// OPEN LOOP ROUTINES //
/*********************/

int modOpenInit(control_t *ptc) {
	
	// start grabbing frames
	return drvInitGrab(&dalsacam);
}

int modOpenLoop(control_t *ptc) {
	static char title[64];
	// get an image, without using a timeout
	if (drvGetImg(&dalsacam, &buffer, NULL, &(ptc->wfs->image)) != EXIT_SUCCESS)
		return EXIT_FAILURE;
	
	
	//MMDarkFlatCorrByte(&(ptc->wfs[0]));
	
#ifdef FOAM_MCMATH_DISPLAY
    if (ptc->frames % ptc->logfrac == 0) {
	displayDraw((&ptc->wfs[0]), &disp, &shtrack);
	displaySDLEvents(&disp);
	logInfo(0, "Current framerate: %.2f FPS", ptc->fps);
	snprintf(title, 64, "%s (O) %.2f FPS", disp.caption, ptc->fps);
	SDL_WM_SetCaption(title, 0);
    }
#endif
	return EXIT_SUCCESS;
}

int modOpenFinish(control_t *ptc) {
	// stop grabbing frames
	return drvStopGrab(&dalsacam);
}

// CLOSED LOOP ROUTINES //
/************************/

int modClosedInit(control_t *ptc) {
	// set disp source to calib
	disp.dispsrc = DISPSRC_CALIB;		
	// start grabbing frames
	return drvInitGrab(&dalsacam);
}

int modClosedLoop(control_t *ptc) {
	static char title[64];
	// get an image, without using a timeout
	if (drvGetImg(&dalsacam, &buffer, NULL, &(ptc->wfs->image)) != EXIT_SUCCESS)
		return EXIT_FAILURE;
	
	
	MMDarkFlatCorrByte(&(ptc->wfs[0]), &shtrack);
	
#ifdef FOAM_MCMATH_DISPLAY
    if (ptc->frames % ptc->logfrac == 0) {
	displayDraw((&ptc->wfs[0]), &disp, &shtrack);
	logInfo(0, "Current framerate: %.2f FPS", ptc->fps);
	snprintf(title, 64, "%s (C) %.2f FPS", disp.caption, ptc->fps);
	SDL_WM_SetCaption(title, 0);
    }
#endif
	return EXIT_SUCCESS;
}

int modClosedFinish(control_t *ptc) {
	// stop grabbing frames
	return drvStopGrab(&dalsacam);
}

// MISC ROUTINES //
/*****************/

int modCalibrate(control_t *ptc) {
	FILE *fieldfd;		// to open some files
	char title[64]; 	// for the window title
	wfs_t *wfsinfo = &(ptc->wfs[0]); // shortcut
	dispsrc_t oldsrc = disp.dispsrc; // store the old display source here since we might just have to show dark or flatfields
	int oldover = disp.dispover;	// store the old overlay here

	if (ptc->calmode == CAL_DARK) {
		// take dark frames, and average
		logInfo(0, "Starting darkfield calibration now");
		if (drvInitGrab(&dalsacam) != EXIT_SUCCESS) 
			return EXIT_FAILURE;

		// check if memory is allocated yet
		if (wfsinfo->darkim == NULL) {
			wfsinfo->darkim = gsl_matrix_float_calloc(wfsinfo->res.x, wfsinfo->res.y);
		}

		MMAvgFramesByte(wfsinfo->darkim, &(ptc->wfs[0]), wfsinfo->fieldframes);
		if (drvStopGrab(&dalsacam) != EXIT_SUCCESS)
			return EXIT_FAILURE;
		// saving image for later usage
		fieldfd = fopen(wfsinfo->darkfile, "w+");	
		if (!fieldfd)  {
			logWarn("Could not open darkfield storage file '%s', not saving darkfield (%s).", wfsinfo->darkfile, strerror(errno));
			return EXIT_SUCCESS;
		}
		gsl_matrix_float_fprintf(fieldfd, wfsinfo->darkim, "%.10f");
		fclose(fieldfd);
		logInfo(0, "Darkfield calibration done, and stored to disk.");
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
		// take flat frames, and average
		if (drvInitGrab(&dalsacam) != EXIT_SUCCESS) 
			return EXIT_FAILURE;
		// check if memory is allocated yet
		if (wfsinfo->flatim == NULL) {
			wfsinfo->flatim = gsl_matrix_float_calloc(wfsinfo->res.x, wfsinfo->res.y);
		}
		MMAvgFramesByte(wfsinfo->flatim, &(ptc->wfs[0]), wfsinfo->fieldframes);
		if (drvStopGrab(&dalsacam) != EXIT_SUCCESS)
			return EXIT_FAILURE;
		// saving image for later usage
		fieldfd = fopen(wfsinfo->flatfile, "w+");	
		if (!fieldfd)  {
			logWarn("Could not open flatfield storage file '%s', not saving flatfield (%s).", wfsinfo->flatfile, strerror(errno));
			return EXIT_SUCCESS;
		}
		gsl_matrix_float_fprintf(fieldfd, wfsinfo->flatim, "%.10f");
		fclose(fieldfd);
		logInfo(0, "Flatfield calibration done, and stored to disk.");
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
	else if (ptc->calmode == CAL_SUBAPSEL) {
		logInfo(0, "Starting subaperture selection now");
		// init grabbing
		if (drvInitGrab(&dalsacam) != EXIT_SUCCESS) 
			return EXIT_FAILURE;
		// get a single image
		drvGetImg(&dalsacam, &buffer, NULL, &(wfsinfo->image));
		
		// stop grabbing
		if (drvStopGrab(&dalsacam) != EXIT_SUCCESS)
			return EXIT_FAILURE;

		uint8_t *tmpimg = (uint8_t *) wfsinfo->image;
		int tmpmax = tmpimg[0];
		int tmpmin = tmpimg[0];
		int tmpsum, i;
		for (i=0; i<wfsinfo->res.x*wfsinfo->res.y; i++) {
			tmpsum += tmpimg[i];
			if (tmpimg[i] > tmpmax) tmpmax = tmpimg[i];
			else if (tmpimg[i] < tmpmin) tmpmin = tmpimg[i];
		}           
		logInfo(0, "Image info: sum: %d, avg: %f, range: (%d,%d)", tmpsum, (float) tmpsum / (wfsinfo->res.x*wfsinfo->res.y), tmpmin, tmpmax);

		// run subapsel on this image
		modSelSubaptsByte((uint8_t *) wfsinfo->image, &shtrack, wfsinfo);
			//modSelSubaptsByte(void *image, mod_sh_track_t *shtrack, wfs_t *shwfs, int *totnsubap, float samini, int samxr) 

	
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
	float tmpfloat;
	
 	if (strncmp(list[0],"help",3) == 0) {
		// give module specific help here
		if (count > 1) { 
			
			if (strncmp(list[1], "disp",3) == 0) {
				tellClient(client->buf_ev, "\
200 OK HELP DISPLAY\n\
display <source>:       change the display source.\n\
   <sources:>\n\
   raw:                 direct images from the camera.\n\
   calib:               dark/flat corrected images.\n\
   dark:                show the darkfield being used.\n\
   flat:                show the flatfield being used.\n\
   <overlays:>\n\
   subap:               toggle displat of the subapertures.\n\
   grid:                toggle display of the grid.\n\
   vecs:                toggle display of the displacement vectors.\
   col [f] [f] [f]:     change the overlay color (OpenGL only).\
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
   selsubap:            select some subapertures.\
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
resetdm [i]:            reset the DM to a certain voltage for all acts. def=0\n\
resetdaq [i]:           reset the DAQ analog outputs to a certain voltage. def=0\n\
set [prop]:             set or query certain properties.\n\
calibrate <mode>:       calibrate the ao system (dark, flat, subapt, etc).\
");
		}
	}
#ifdef FOAM_MCMATH_DISPLAY
 	else if (strncmp(list[0], "disp",3) == 0) {
		if (count > 1) {
			if (strncmp(list[1], "raw",3) == 0) {
				tellClient(client->buf_ev, "200 OK DISPLAY RAW");
				disp.dispsrc = DISPSRC_RAW;
			}
			else if (strncmp(list[1], "cal",3) == 0) {
				disp.dispsrc = DISPSRC_CALIB;
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
 	else if (strcmp(list[0], "resetdm") == 0) {
		if (count > 1) {
			tmpint = strtol(list[1], NULL, 10);
			
			if (tmpint >= okodm.minvolt && tmpint <= okodm.maxvolt) {
				if (drvSetAllOkoDM(&okodm, tmpint) == EXIT_SUCCESS)
					tellClients("200 OK RESETDM %dV", tmpint);
				else
					tellClient(client->buf_ev, "300 ERROR RESETTING DM");
				
			}
			else {
				tellClient(client->buf_ev, "403 INCORRECT VOLTAGE!");
			}
		}
		else {
			if (drvRstOkoDM(&okodm) == EXIT_SUCCESS)
				tellClients("200 OK RESETDM 0V");
			else 
				tellClient(client->buf_ev, "300 ERROR RESETTING DM");
			
		}
	}
 	else if (strcmp(list[0], "resetdaq") == 0) {
		if (count > 1) {
			tmpfloat = strtof(list[1], NULL);
			
			if (tmpfloat >= daqboard.minvolt && tmpfloat <= daqboard.maxvolt) {
				drvDaqSetDACs(&daqboard, (int) 65536*(tmpfloat-daqboard.minvolt)/(daqboard.maxvolt-daqboard.minvolt));
				tellClients("200 OK RESETDAQ %fV", tmpfloat);
			}
			else {
				tellClient(client->buf_ev, "403 INCORRECT VOLTAGE!");
			}
		}
		else {
			drvDaqSetDACs(&daqboard, 65536*(-daqboard.minvolt)/(daqboard.maxvolt-daqboard.minvolt));
			tellClients("200 OK RESETDAQ 0.0V");			
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
fieldframes (ff):       %d\n\
SH array:               %dx%d cells\n\
cell size:              %dx%d pixels\n\
track size:             %dx%d pixels\n\
ccd size:               %dx%d pixels\n\
samxr:                  %d\n\
samini:                 %.2f\n\
", ptc->logfrac, ptc->wfs[0].fieldframes, shtrack.cells.x, shtrack.cells.y,\
shtrack.shsize.x, shtrack.shsize.y, shtrack.track.x, shtrack.track.y, ptc->wfs[0].res.x, ptc->wfs[0].res.y, shtrack.samxr, shtrack.samini);
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
			else if (strncmp(list[1], "sel",3) == 0) {
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

int drvSetActuator(wfc_t *wfc) {
	if (wfc->type == 0) {			// Okotech DM
		// use okodm routines here
	}
	else if (wfc == 1) {	// Tip-tilt mirror
		// use daq routines here
	}
	
	return EXIT_SUCCESS;
}

int drvSetupHardware(control_t *ptc, aomode_t aomode, calmode_t calmode) {
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

int MMAvgFramesByte(gsl_matrix_float *output, wfs_t *wfs, int rounds) {
	int k, i, j;
	float min, max, sum, tmpvar;
	uint8_t *imgsrc;
    logDebug(0, "Averaging %d frames now (dark, flat, whatever)", rounds);

	gsl_matrix_float_set_zero(output);
	for (k=0; k<rounds; k++) {
		if ((k % (rounds/10)) == 0 && k > 0)
       			logDebug(0 , "Frame %d", k);

		drvGetImg(&dalsacam, &buffer, NULL, &(wfs->image));
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

// Dark flat calibration
int MMDarkFlatCorrByte(wfs_t *wfs, mod_sh_track_t *shtrack) {
	logDebug(LOG_SOMETIMES, "Dark correcting now, flat not implemented yet");
	int sn;
	size_t i, j; // size_t because gsl wants this
	uint8_t* imagesrc = (uint8_t*) wfs->image;
	
	if (wfs->darkim == NULL || wfs->flatim == NULL) {
		logWarn("Dark- or flatfield not available, please calibrate first");
		return EXIT_FAILURE;
	}
	// loop over all subapertures, and correct only the
	// tracker windows for dark and flat
	for (sn=0; sn<shtrack->nsubap; sn++) {
		// correcting tracker window for subapt 'sn' now
		// which is shtrack->track.x by track.y big
		// and which is located at shtrack->subc.x by subc.y

		for (i=0; i < shtrack->track.y; i++) {
			for (j=0; j < shtrack->track.x; j++) {
				// source pixel at:
				// img[subc.x + subc.y *res.x + i *res.x + j]
				// dst at corrim,
				// corrim = img-dark
				gsl_matrix_float_set(wfs->corrim, i + shtrack->subc[sn].y, j + shtrack->subc[sn].x, \
				(((float) imagesrc[shtrack->subc[sn].y * wfs->res.x + shtrack->subc[sn].x + i*wfs->res.x +j]) -\
				gsl_matrix_float_get(wfs->darkim, i + shtrack->subc[sn].y, j + shtrack->subc[sn].x))  \
				);
			}
		}
	}
	
	// now do raw-dark/(flat-dark), flat-dark is already stored in wfs->flatim
	//gsl_matrix_float_sub (wfs->corrim, wfs->darkim);
	//gsl_matrix_float_div_elements (wfs->corrim, wfs->flatim);

	return EXIT_SUCCESS;
}

