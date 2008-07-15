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
	@file foam_modules-sim.c
	@author @authortim
	@date 2008-07-15

	@brief This file contains the functions to run @name in simulation mode.
	
	\section Info
	This module provides some functions to run an AO system in simulation mode.
 	
	\section Functions

	The functions provided to the outside world are:
	\li simInit() - Initialize the module, using a filled mod_sim_t struct
	\li simFlat() - Generate a flat wavefront, can be used to do artificial dark- or flatfielding
	\li simNoise() - Simulate noise, will be added to the simulation image. Use in conjunction with simFlat().
	\li simWind() - Simulates wind, i.e. move the origin with the windspeed
	\li simAtm() - Crop a piece of the big wavefront to the size of the CCD
	\li simTel() - Simulate a telescope aperture, i.e. crop the wavefront and set to zero outside the aperture
	\li simWFC() - Wrapper for WFC simulation
	\li	simTT() - Simulate a tip-tilt mirror, i.e. generate a wavefront slope
	\li	simWFCError() - Introduce a wavefront error generated by a wavefront corrector (TT, DM), which can be corrected perfectly
	\li simSHWFS() - Simulate a SH WFS, i.e. split the wavefront up in subapertures and image these independently
 
	\section Dependencies
	
	This module depends on:
	\li foam_modules-calib.c - to calibrate the simulated WFC and TT mirror
	\li foam_modules-sh.c - to work with Shack-Hartmann trackers.
	\li foam_modules-img.c - to read and write images.
*/

// HEADERS //
/***********/

#include "foam_modules-sim.h"

// ROUTINES //
/************/

int simInit(mod_sim_t *simparams) {
	
	// Initialize the module:
	// - Load the wavefront into memory
	if (modReadIMGArrByte(simparams->wf, &(simparams->wfimg), &(simparams->wfres)) != EXIT_SUCCESS)
		return EXIT_FAILURE;
	
	// - Load the aperture into memory	
	if (modReadIMGArrByte(simparams->apert, &(simparams->apertimg), &(simparams->apertres)) != EXIT_SUCCESS)
		return EXIT_FAILURE;
	
	// - Load the actuator pattern into memory
	if (modReadIMGArrByte(simparams->actpat, &(simparams->actpatimg), &(simparams->actpatres)) != EXIT_SUCCESS)
		return EXIT_FAILURE;
	
	// - Check sanity of values in simparams
	if (simparams->wfres.x < simparams->currimgres.x + 2*simparams->wind.x) {
		logWarn("Simulated wavefront too small (%d) for current x-wind (%d), setting to zero.", 
				simparams->wfres.x, simparams->wind.x);
		simparams->wind.x = 0;
	}
	
	if (simparams->wfres.y < simparams->currimgres.y + 2*simparams->wind.y) {
		logWarn("Simulated wavefront too small (%d) for current y-wind (%d), setting to zero.", 
				simparams->wfres.y, simparams->wind.y);
		simparams->wind.y = 0;
	}

	// - Allocate memory for (simulated) WFS output
	simparams->currimg = (uint8_t *) malloc(simparams->currimgres.x	 * simparams->currimgres.y * sizeof(uint8_t));


	logInfo(0, "Simulation module initialized. Currimg (%dx%d)", simparams->currimgres.x, simparams->currimgres.y);
	return EXIT_SUCCESS;
}

// This function moves the current origin around using 'wind'
int simWind(mod_sim_t *simparams) {
	// Check to see if we are using wind
	if (simparams->wind.x == 0 && simparams->wind.y == 0)
		return EXIT_SUCCESS;
	
	logDebug(LOG_SOMETIMES, "Simulating wind.");
	// Apply 'wind' to the simulated wavefront (i.e. move the origin a few pixels)
	simparams->currorig.x += simparams->wind.x;
	simparams->currorig.y += simparams->wind.y;
	
 	// if the origin is out of bound, reverse the wind direction and move that way
	// X ORIGIN TOO BIG:
	if (simparams->currorig.x + simparams->currimgres.x >= simparams->wfres.x) {
		simparams->wind.x *= -1;						// Reverse X wind speed
		simparams->currorig.x += 2*simparams->wind.x;	// move in the new wind direction, 
		// twice to compensate for the wind already applied above
	}
	// X ORIGIN TOO SMALL
	if (simparams->currorig.x < 0) {
		simparams->wind.x *= -1;	
		simparams->currorig.x += 2*simparams->wind.x;
	}
	
	// Y ORIGIN TOO BIG
	if (simparams->currorig.y + simparams->currimgres.y >= simparams->wfres.y) {
		simparams->wind.y *= -1;						// Reverse Y wind speed
		simparams->currorig.y += 2*simparams->wind.y;	// move in the new wind direction
	}
	// Y ORIGIN TOO SMALL
	if (simparams->currorig.y < 0) {
		simparams->wind.y *= -1;
		simparams->currorig.y += 2*simparams->wind.y;
	}
	
	
	return EXIT_SUCCESS;
}

// This function simulates the wind in the wavefront, mainly
int simAtm(mod_sim_t *simparams) {
	int i,j;
	logDebug(LOG_SOMETIMES, "Simulating atmosphere.");
	uint8_t min, max, pix;
	uint64_t sum;
	
	// Now crop the image, i.e. take a part of the big wavefront and store it in 
	// the location of the wavefront sensor image
	min = max = simparams->wfimg[0];
	for (i=0; i< simparams->currimgres.y; i++) { // y coordinate
		for (j=0; j < simparams->currimgres.x; j++) { // x coordinate 
			pix = simparams->wfimg[(simparams->currorig.y + i)*simparams->wfres.x + simparams->currorig.x + j];
			simparams->currimg[i*simparams->currimgres.x + j] = pix;
//			if (pix > max) pix = max;
//			else if (pix < min) pix = min;
//			sum += pix;
		}
	}
	//logDebug(LOG_SOMETIMES, "Atmosphere: min %f, max %f, sum: %f, avg: %f", min, max, sum, sum/( simparams->currimgres.x *  simparams->currimgres.y));
	
	return EXIT_SUCCESS;
}

int simFlat(mod_sim_t *simparams, int intensity) {
	int i,j;
	logDebug(LOG_SOMETIMES, "Simulating flat field intensity %d.", intensity);
	
	for (i=0; i< simparams->currimgres.y; i++) // y coordinate
		for (j=0; j < simparams->currimgres.x; j++) // x coordinate 
			simparams->currimg[i*simparams->currimgres.x + j] = intensity;
	
	return EXIT_SUCCESS;
}

int simNoise(mod_sim_t *simparams, int var) {
	long i;
	uint32_t old;
	double n;
	logDebug(LOG_SOMETIMES, "Simulation noise, variation %d.", var);
	
	for (i=0; i< simparams->currimgres.y * simparams->currimgres.x; i++) { // loop over all pixels
		old = simparams->currimg[i];

		if ((simparams->currimg[i] += drand48() * var) < old)
			simparams->currimg[i] = old;
	}
	
	return EXIT_SUCCESS;
}

int simTT(mod_sim_t *simparams, gsl_vector_float *ctrl, int mode) {
	int i,j;
	// amplitude of the TT mirror (multiplied simulated TT output by this factor)
	uint8_t amp = 127;
	uint8_t off = 128;
	coord_t res = simparams->currimgres;
	
	// first simulate rails (i.e. crop ctrl above abs(1))
	// if (ctrl[0] > 1.0) ctrl[0] = 1.0;
	// if (ctrl[0] < -1.0) ctrl[0] = -1.0;
	// if (ctrl[1] > 1.0) ctrl[1] = 1.0;
	// if (ctrl[1] < -1.0) ctrl[1] = -1.0;

	if (mode == 0) { // mode == 0 SETS the wavefront with offset
		for (i=0; i<res.y; i++)
			for (j=0; j<res.x; j++)
				simparams->currimg[i*res.x + j] = off + ((((float) i/(res.y-1))-0.5) * 2 * amp * gsl_vector_float_get(ctrl, 1)) + \
								   ((((float) j/(res.x-1) )-0.5) * 2 * amp * gsl_vector_float_get(ctrl, 0));
	}
	else { // mode != 0 ADDS to the current wavefront w/o offset
		for (i=0; i<res.y; i++)
			for (j=0; j<res.x; j++)
				simparams->currimg[i*res.x + j] += ((((float) i/(res.y-1))-0.5) * 2 * amp * gsl_vector_float_get(ctrl, 1)) + \
								   ((((float) j/(res.x-1) )-0.5) * 2 * amp * gsl_vector_float_get(ctrl, 0));
	}
	
	// this had problems with integer divisions:
	//image[i*res.x + j] += (((i/res.y)-0.5) * 2 * amp * ctrl[1]) + (((j/res.x)-0.5) * 2 * amp * ctrl[0]);
	
	return EXIT_SUCCESS;	
}

int simTel(mod_sim_t *simparams) {
	int i;
	
	// Multiply wavefront with aperture
	for (i=0; i < simparams->currimgres.x * simparams->currimgres.y; i++)
	 	if (simparams->apertimg[i] == 0) simparams->currimg[i] = 0;
	
	return EXIT_SUCCESS;
}

int simSHWFS(mod_sim_t *simparams, mod_sh_track_t *shwfs) {
	logDebug(LOG_SOMETIMES, "Simulating SH WFSs now.");
	
	int zeropix, i;
	int nx, ny;
	int xc, yc;
	int jp, ip;
	FILE *fp;
	
	double tmp;
	double min = 0.0, max = 0.0;
	
	// we take the subaperture, which is shsize.x * .y big, and put it in a larger matrix
	nx = (shwfs->shsize.x * 2);
	ny = (shwfs->shsize.y * 2);

	// init data structures for images, fill with zeroes
	if (simparams->shin == NULL) {
		simparams->shin = fftw_malloc ( sizeof ( fftw_complex ) * nx * ny );
		for (i=0; i< nx*ny; i++)
			simparams->shin[i][0] = simparams->shin[i][1] = 0.0;
	}
	
	if (simparams->shout == NULL) {
		simparams->shout = fftw_malloc ( sizeof ( fftw_complex ) * nx * ny );
		for (i=0; i< nx*ny; i++)
			simparams->shout[i][0] = simparams->shout[i][1] = 0.0;
	}
	
	// init FFT plan, measure how we can compute an FFT the fastest on this machine
	if (simparams->plan_forward == NULL) {
		logDebug(0, "Setting up plan for fftw");
		
		fp = fopen(simparams->wisdomfile,"r");
		if (fp == NULL)	{				// File does not exist, generate new plan
			logInfo(0, "No FFTW plan found in %s, generating new plan, this may take a while.", simparams->wisdomfile);
			simparams->plan_forward = fftw_plan_dft_2d(nx, ny, simparams->shin, simparams->shout, FFTW_FORWARD, FFTW_EXHAUSTIVE);

			// Open file for writing now
			fp = fopen(simparams->wisdomfile,"w");
			if (fp == NULL) {
				logDebug(0, "Could not open file %s for saving FFTW wisdom.", simparams->wisdomfile);
				return EXIT_FAILURE;
			}
			fftw_export_wisdom_to_file(fp);
			fclose(fp);
		}
		else {
			logInfo(0, "Importing FFTW wisdom file.");
			logInfo(0, "If this is the first time this program runs on this machine, this is bad.");
			logInfo(0, "In that case, please delete '%s' and rerun the program. It will generate new wisdom which is A Good Thing.", \
				simparams->wisdomfile);
			if (fftw_import_wisdom_from_file(fp) == 0) {
				logWarn("Importing wisdom failed.");
				return EXIT_FAILURE;
			}
			// regenerating plan using wisdom imported above.
			simparams->plan_forward = fftw_plan_dft_2d(nx, ny, simparams->shin, simparams->shout, FFTW_FORWARD, FFTW_EXHAUSTIVE);
			fclose(fp);
		}
		
	}
		
	if (simparams->shout == NULL || simparams->shin == NULL || simparams->plan_forward == NULL)
		logErr("Some allocation of memory for FFT failed.");
	
//	if (ptc.wfs[0].cells[0] * shsize[0] != ptc.wfs[0].res.x || \
//		ptc.wfs[0].cells[1] * shsize[1] != ptc.wfs[0].res.y)
//		logErr("Incomplete SH cell coverage! This means that nx_subapt * width_subapt != width_img x: (%d*%d,%d) y: (%d*%d,%d)", \
//			ptc.wfs[0].cells[0], shsize[0], ptc.wfs[0].res.x, ptc.wfs[0].cells[1], shsize[1], ptc.wfs[0].res.y);

	
	logDebug(LOG_SOMETIMES, "Beginning imaging simulation.");

	// now we loop over each subaperture:
	for (yc=0; yc< shwfs->cells.y; yc++) {
		for (xc=0; xc< shwfs->cells.x; xc++) {
			// we're at subapt (xc, yc) here...

			// possible approaches on subapt selection for simulation:
			//  - select only central apertures (use radius)
			//  - use absolute intensity (still partly illuminated apts)
			//  - count pixels with intensity zero

			zeropix = 0;
			for (ip=0; ip< shwfs->shsize.y; ip++)
				for (jp=0; jp< shwfs->shsize.x; jp++)
					 	if (simparams->currimg[yc * shwfs->shsize.y * simparams->currimgres.x + xc*shwfs->shsize.x + ip * simparams->currimgres.x + jp] == 0)
							zeropix++;
			
			// allow one quarter of the pixels to be zero, otherwise set subapt to zero and continue
			if (zeropix > shwfs->shsize.y*shwfs->shsize.x/4) {
				for (ip=0; ip<shwfs->shsize.y; ip++)
					for (jp=0; jp<shwfs->shsize.x; jp++)
						simparams->currimg[yc*shwfs->shsize.y*simparams->currimgres.x + xc*shwfs->shsize.x + ip*simparams->currimgres.x + jp] = 0;
				
				// skip over the rest of the for loop started ~20 lines back
				continue;
			}
			
			// We want to set the helper arrays to zero first
			// otherwise we get trouble? TODO: check this out
			for (i=0; i< nx*ny; i++)
				simparams->shin[i][0] = simparams->shin[i][1] = \
					simparams->shout[i][0] = simparams->shout[i][1] = 0.0;
//			for (i=0; i< nx*ny; i++)
			
			// add markers to track copying:
			//for (i=0; i< 2*nx; i++)
				//simparams->shin[i][0] = 1;

			//for (i=0; i< ny; i++)
				//simparams->shin[nx/2+i*nx][0] = 1;

			// loop over all pixels in the subaperture, copy them to subapt:
			// I'm pretty sure these index gymnastics are correct (2008-01-18)
			for (ip=0; ip < shwfs->shsize.y; ip++) { 
				for (jp=0; jp < shwfs->shsize.x; jp++) {
					// we need the ipth row PLUS the rows that we skip at the top (shwfs->shsize.y/2+1)
					// the width is not shwfs->shsize.x but twice that plus 2, so mulyiply the row number
					// with that. Then we need to add the column number PLUS the margin at the beginning 
					// which is shwfs->shsize.x/2 + 1, THAT's the right subapt location.
					// For the image itself, we need to skip to the (ip,jp)th subaperture,
					// which is located at pixel yc * the height of a cell * the width of the picture
					// and the x coordinate times the width of a cell time, that's at least the first
					// subapt pixel. After that we add the subaperture row we want which is located at
					// pixel ip * the width of the whole image plus the x coordinate

					simparams->shin[(ip+ ny/4)*nx + (jp + nx/4)][0] = \
						simparams->currimg[yc*shwfs->shsize.y*simparams->currimgres.x + xc*shwfs->shsize.x + ip*simparams->currimgres.x + jp];
					// old: simparams->shin[(ip+ shwfs->shsize.y/2 +1)*nx + jp + shwfs->shsize.x/2 + 1][0] = 
				}
			}
			
			// now image the subaperture, first generate EM wave amplitude
			// this has to be done using complex numbers over the BIG subapt
			// we know that exp ( i * phi ) = cos(phi) + i sin(phi),
			// so we split it up in a real and a imaginary part
			// TODO dit kan hierboven al gedaan worden
			for (ip=shwfs->shsize.y/2; ip<shwfs->shsize.y + shwfs->shsize.y/2; ip++) {
				for (jp=shwfs->shsize.x/2; jp<shwfs->shsize.x+shwfs->shsize.x/2; jp++) {
					tmp = simparams->seeingfac * simparams->shin[ip*nx + jp][0]; // multiply for worse seeing
					//use fftw_complex datatype, i.e. [0] is real, [1] is imaginary
					
					// SPEED: cos and sin are SLOW, replace by taylor series
					// optimilization with parabola, see http://www.devmaster.net/forums/showthread.php?t=5784
					// and http://lab.polygonal.de/2007/07/18/fast-and-accurate-sinecosine-approximation/
					// wrap tmp to (-pi, pi):
					//tmp -= ((int) ((tmp+3.14159265)/(2*3.14159265))) * (2* 3.14159265);
					//simparams->shin[ip*nx + jp][1] = 1.27323954 * tmp -0.405284735 * tmp * fabs(tmp);
					// wrap tmp + pi/2 to (-pi,pi) again, but we know tmp is already in (-pi,pi):
					//tmp += 1.57079633;
					//tmp -= (tmp > 3.14159265) * (2*3.14159265);
					//simparams->shin[ip*nx + jp][0] = 1.27323954 * tmp -0.405284735 * tmp * fabs(tmp);
					
					// used to be:
					simparams->shin[ip*nx + jp][1] = sin(tmp);
					// TvW, disabling sin/cos
					simparams->shin[ip*nx + jp][0] = cos(tmp);
					//simparams->shin[ip*nx + jp][0] = tmp;
				}
			}

			// now calculate the  FFT
			fftw_execute ( simparams->plan_forward );

			// now calculate the absolute squared value of that, store it in the subapt thing
			// also find min and maximum here
			for (ip=0; ip<ny; ip++) {
				for (jp=0; jp<nx; jp++) {
					tmp = simparams->shin[ip*nx + jp][0] = \
					 fabs(pow(simparams->shout[ip*nx + jp][0],2) + pow(simparams->shout[ip*nx + jp][1],2));
					 if (tmp > max) max = tmp;
					 else if (tmp < min) min = tmp;
				}
			}
			// copy subaparture back to main images
			// note: we don't want the center of the fourier transformed image, but we want all corners
			// because the FT begins in the origin. Therefore we need to start at coordinates
			//  nx-(nx_subapt/2), ny-(ny_subapt/2)
			// e.g. for 32x32 subapts and (nx,ny) = (64,64), we start at
			//  (48,48) -> (70,70) = (-16,-16)
			// so we need to wrap around the matrix, which results in the following
			float tmppixf;
			//uint8_t tmppixb;
			for (ip=ny/4; ip<ny*3/4; ip++) { 
				for (jp=nx/4; jp<nx*3/4; jp++) {
					tmppixf = simparams->shin[((ip+ny/2)%ny)*nx + (jp+nx/2)%nx][0];

					simparams->currimg[yc*shwfs->shsize.y*simparams->currimgres.x + xc*shwfs->shsize.x + (ip-ny/4)*simparams->currimgres.x + (jp-nx/4)] = 255.0*(tmppixf-min)/(max-min);
				}
			}
			
		} 
	} // end looping over subapts	
	
	return EXIT_SUCCESS;
}

int simWFCError(mod_sim_t *simparams, wfc_t *wfc, int method, int period) {
	// static ctrl vector so we don't lose it every time this function is called
	static gsl_vector_float *simctrl = NULL;
	int nact, i;
	static int count=0;
	float ctrl;

	// allocate memory for the simulation controls
	if (simctrl == NULL) {
		simctrl = gsl_vector_float_alloc(wfc->nact);
	}
	// if nact > size, we previously allocated memory for a smaller WFC,
	// if so, reallocate some memory and continue
	else if (wfc->nact > simctrl->size) {
		gsl_vector_float_free(simctrl);
		simctrl = gsl_vector_float_alloc(wfc->nact);
	}
	// if simctrl is NULL by now, something went wrong with allocation, stop
	if (!simctrl)
		logErr("Failed to allocate memory for simulation control vector.");
	
	

	// we use this counter to make periodic signals
	// because count is declared statically, it will retain its
	// value between different function calls
	count++;
	
	// make a fake control vector for the error
	if (method == 1) {
		// method 1: regular sawtooth drift here:
		ctrl = ((count % period)/(float)period * 2 - 1); 
		ctrl = fabs(ctrl)*2-1;
		gsl_vector_float_set_all(simctrl, ctrl);
	}
	else {
		// method 2: random drift:
		for (i=0; i<wfc->nact; i++) {
			// let the error drift around
			ctrl = gsl_vector_float_get(simctrl,i) + (drand48()-0.5)*0.05;
			// put bounds on the error range
			if (ctrl > 1) ctrl = 1.0;
			else if (ctrl < -1) ctrl = -1.0;
			gsl_vector_float_set(simctrl, i, ctrl);
		}
	}
	
	// log simulated error
	/*
	if (ptc->domisclog) {
		fprintf(ptc->misclog, "WFC ERR, %d, %d", wfc->id, wfc->nact);
		for (i=0; i<wfc->nact; i++)
			fprintf(ptc->misclog, ", %f", \
				gsl_vector_float_get(simctrl,i));

		fprintf(ptc->misclog, "\n");
	}
	*/
	
	// What routine do we need to call to simulate this WFC?
	if (wfc->type == WFC_TT)
		if (simTT(simparams, simctrl, 0) != EXIT_SUCCESS)
			return EXIT_FAILURE;
	else if (wfc->type == WFC_DM)
		if (simDM(simparams, wfc->ctrl, wfc->nact, 0, -1) != EXIT_SUCCESS) // last arg is for niter. -1 for autoset
			return EXIT_FAILURE;
	
	logDebug(LOG_SOMETIMES | LOG_NOFORMAT, "Error: %d with %d acts: ", wfc->id, wfc->nact);
	for (i=0; i<wfc->nact; i++)
		logDebug(LOG_SOMETIMES | LOG_NOFORMAT, "%f, ", gsl_vector_float_get(simctrl,i));
		
	logDebug(LOG_SOMETIMES | LOG_NOFORMAT, "\n");
	
	return EXIT_SUCCESS;
}

int simWFC(wfc_t *wfc, mod_sim_t *simparams) { 
	int i;
	logDebug(LOG_SOMETIMES, "Simulation WFC %d (%s) with  %d actuators", wfc->id, wfc->name, wfc->nact);

	// log simulated correction
	/*
	if (ptc->domisclog) {
		fprintf(ptc->misclog, "WFC CORR, %d, %d", wfc->id, wfc->nact);
		for (i=0; i<wfc->nact; i++)
			fprintf(ptc->misclog, ", %f", \
				gsl_vector_float_get(wfc->ctrl,i));

		fprintf(ptc->misclog, "\n");
	}
	*/

	if (wfc->type == WFC_TT) {
		simTT(simparams, wfc->ctrl, 1);
	}
	else if (wfc->type == WFC_DM) {
		simDM(simparams, wfc->ctrl, wfc->nact, 1, -1);
	}	
	else {
		logWarn("Unknown WFC (%d) encountered (not TT or DM, type: %d, name: %s)", wfc->id, wfc->type, wfc->name);
		return EXIT_FAILURE;
	}
					
	return EXIT_SUCCESS;
}

int simDM(mod_sim_t *simparams, gsl_vector_float *ctrl, int nact, int mode, int niter) {
	float pi = 4.0*atan(1), rho, omega, sum, sdif, update;
	int i, iter;							// some counters
	
	float amp = 5.0;						// amplitude of the DM response (used to scale the output)
	float limit = (1.E-8);					// limit value for SOR iteration
	
	int voltage[nact]; 						// voltages for electrodes
	int volt;								// temporary storage for voltage
	coord_t res = simparams->currimgres;	// simulated wavefront resolution
	
	static uint8_t *actvolt=NULL;			// this must hold the actuator pattern with
											// the voltages per actuator
	static float *resp=NULL;				// store the mirror response here temporarily
	
	// allocate memory for actuator pattern with voltages
	if (actvolt == NULL) {
		actvolt = malloc((res.x) * (res.y) * sizeof(*actvolt));
		if (actvolt == NULL)
			logErr("Error allocating memory for actuator voltage image");
		
	}
	
	// allocate memory for the mirror response
	if (resp == NULL) {
		resp = malloc((res.x) * (res.y) * sizeof(*resp));
		if (resp == NULL)
			logErr("Error allocating memory for mirror response");
		
	}
	
	// input is linear and [-1,1], 'output' v must be [0,255] and linear in v^2
	logDebug(LOG_SOMETIMES, "Simulating DM with voltages:");
	for (i = 0; i < nact; i++) {
		// first simulate rails (i.e. crop ctrl above abs(1))
		if (gsl_vector_float_get(ctrl, i) > 1.0) gsl_vector_float_set(ctrl, i, 1.0);
		else if (gsl_vector_float_get(ctrl, i) < -1.0) gsl_vector_float_set(ctrl, i, -1.0);

		// we do Sqrt(255^2 (i+1) * 0.5) here to convert from [-1,1] (linear) to [0,255] (quadratic)
		voltage[i] = (uint8_t) round( sqrt(65025.0*(gsl_vector_float_get(ctrl, i)+1)*0.5 ) ); 
		logDebug(LOG_NOFORMAT | LOG_SOMETIMES, "%d ", voltage[i]);
	}
	logDebug(LOG_NOFORMAT | LOG_SOMETIMES, "\n");
	
	// set actuator voltages on electrodes *act is the actuator pattern,
	// where the value of the pixel associates that pixel with an actuator
	// *actvolt will hold this same pattern, but then with voltage
	// related values which serve as input for solving the mirror function
	for (i = 0; i < res.x*res.y; i++){
		volt = simparams->actpatimg[i];
		if (volt > 0)
			actvolt[i] = pow(voltage[volt-1] / 255.0, 2.0) / 75.7856;
		// 75.7856*2 gets 3 um deflection when all voltages 
		// are set to 180; however, since the reflected wavefront sees twice
		// the surface deformation, we simply put a factor of two here
	}
	
	// compute spectral radius rho and SOR omega factor. These are 
	// approximate values. Get the number of iterations.
	
	rho = (cos(pi/((double)res.x)) + cos(pi/((double)res.y)))/2.0;
	omega = 2.0/(1.0 + sqrt(1.0 - rho*rho));
	
	if (niter <= 0) // Set the number of iterations if the argument was <= 0
		niter = (int) (2.0*sqrt((double) res.x*res.y));
	
	// Calculation of the response. This is a Poisson PDE problem
	// where the actuator pattern represents the source term and and
	// mirror boundary a Dirichlet boundary condition. Because of the
	// arbitrary nature of the latter we use a relaxation algorithm.
	// This is a Simultaneous Over-Relaxation (SOR) algorithm described
	// in Press et al., "Numerical Recipes", Section 17.

	// loop over all iterations
	for (iter = 1; iter <= niter; iter++) {
		
		sum = 0.0;
		sdif = 0.0;
		// start at 1+res.x because this pixel has four neighbours,
		// stop at the last but (1+res.x) pixel but again this has four neighbours.
		for (i=1+res.x; i<(res.x*res.y)-1-res.x; i++) {
			if (simparams->apertimg[i] > 0) { // is this pixel within the telescope aperture?
				update = -resp[i] - (actvolt[i] - resp[i-res.x] - resp[i+res.x] - resp[i+1] - resp[i-1])/4.;
				resp[i] = resp[i] + omega*update;
				sum += resp[i];
				sdif += (omega*update)*(omega*update);
			}
			else
				resp[i] = 0.;
			
		}
		
		sdif = sqrt(sdif/(sum*sum));
		
		if (sdif < limit) // Stop iterating if changes are small snough
			break;
		
	} // end iteration loop
	
	if (mode == 1) { // *update* the image, so there should already be an image
		for (i = 0; i < res.x*res.y; i++)
			simparams->currimg[i] += amp*resp[i];
	}
	else if (mode == 0) { // *set* the image, so the previous image will be overwritten
		for (i = 0; i < res.x*res.y; i++)
			simparams->currimg[i] = amp*resp[i];
	}
	
	return EXIT_SUCCESS; // successful completion
}
