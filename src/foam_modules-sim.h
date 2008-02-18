/*! 
	@file foam_modules-sim.h
	@author @authortim
	@date November 30 2007

	@brief This header file prototypes simulation functions.
*/
#ifndef FOAM_MODULES_SIM
#define FOAM_MODULES_SIM

#include "foam_cs_library.h"		// we link to the main program here (i.e. we use common (log) functions)
#include "foam_modules-display.h"	// we need the display module to show debug output
#include "foam_modules-sh.h"		// we want the SH subroutines so we can track targets
#include "foam_modules-dm.h"		// we want the DM subroutines here too
#include "foam_modules-calib.h"		// we want the calibration
#include <fftw3.h> 					// we need this for modParseSH()

// These are defined in foam_cs_library.c
extern control_t ptc;
extern config_t cs_config;

// PROTOTYPES //
/**************/

/*!
@brief Simulates sensor(s) read-out and outputs to ptc.wfs[n].image.

During simulation, this function takes care of simulating the atmosphere, 
the telescope, the tip-tilt mirror, the DM and the (SH) sensor itself, since
these all occur in sequence. The sensor output will be stored in ptc.wfs[n].image,
which must be globally available and allocated.

@return EXIT_SUCCESS on success, EXIT_FAILURE otherwise.
*/
int drvReadSensor();

/*!
@brief This sets the various actuators (WFCs).

This simulates setting the actuators to the right voltages etc. 

@return EXIT_SUCCESS on success, EXIT_FAILURE otherwise.
*/
int drvSetActuator();

/*!
@brief This drives the filterwheel.

This simulates setting the filterwheel. The actual simulation is not done here,
but in drvReadSensor() which checks which filterwheel is being used and 
acts accordingly. With a real filterwheel, this would set some hardware address.

@return EXIT_SUCCESS on success, EXIT_FAILURE otherwise.
*/
int drvFilterWheel(control_t *ptc, fwheel_t mode);

/*!
@brief Simulates the SH sensor

This simulates the SH WFS
*/
int modSimSH();

/*!
@brief Calculates DM output voltages, which are in turn set by drvSetActuator().

@return EXIT_SUCCESS on success, EXIT_FAILURE otherwise.
*/
int modCalcDMVolt();


/*!
@brief This routine, currently not used, can be used to simulate an object (e.g. wavefront)
*/
int simObj(char *file, float *image);

/*!
@brief \a simAtm() reads a fits file with simulated atmospheric distortion.

This fuction works in wavefront-space.
TODO: add doc
*/
int simAtm(char *file, int res[2], int origin[2], float *image);

/*!
@brief Simulates wind by chaning the origin that simAtm reads in
*/
int modSimWind();
	
/*!
@brief \a simTel() simulates the effect of the telescope (aperture) on the output of \a simAtm().

This fuction works in wavefront-space, and basically multiplies the aperture function with
the wavefront from \a simAtm().
*/
int simTel(char *file, float *image);

/*!
@brief \a simWFC() simulates a certain wave front corrector, like a TT or a DM.

This fuction works in wavefront-space.
*/
int simWFC(control_t *ptc, int wfcid, int nact, float *ctrl, float *image);

/*!
@brief Reads a pgm file from disk into memory

@param [in] *fname the filename to read
@param [out] **dbuf the buffer that the image will be read to (will be allocated dynamically)
@param [out] *t_nx will hold the x resolution of the image
@param [out] *t_ny will hold the y resolution of the image
@param [out] *t_ngray will hold the number of different graylevels in the image
*/
int modReadPGM(char *fname, double **dbuf, int *t_nx, int *t_ny, int *t_ngray);

#endif /* FOAM_MODULES_SIM */
