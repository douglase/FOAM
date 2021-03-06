/*
 simulcam.h -- atmosphere/telescope simulator camera header
 Copyright (C) 2010--2011 Tim van Werkhoven <werkhoven@strw.leidenuniv.nl>
 
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


#ifndef HAVE_SIMULCAM_H
#define HAVE_SIMULCAM_H

#ifdef HAVE_CONFIG_H
#include "autoconfig.h"
#endif

#include <stdint.h>
#include <math.h>
#include <fftw3.h>
#include <gsl/gsl_vector.h>

#include "config.h"
#include "io.h"

#include "camera.h"
#include "simseeing.h"
#include "simulwfc.h"
#include "shwfs.h"

using namespace std;

const string simulcam_type = "simulcam";

/*!
 @brief Simulation class for seeing + camera
 
 SimulCam is derived from Camera. Given a static input wavefront, it simulates
 a Shack-Hartmann wavefront sensor (i.e. the CCD). 
 
 SimulCam depends on several other classes: SimSeeing is used for simulating 
 atmospheric seeing, SimulWfc is used for simulating an error generated by a
 wavefront corrector (deformable mirror type). Additionally, simulcam needs
 another SimulWfc instance for correcting a measured wavefront error (if
 applicable). Shwfs is used 
 
 At initialisation, the static input wavefront is read from disk by SimSeeing. 
 When SimulCam is running (in cam_handler()), simulating one frame goes 
 through several steps:
 
 - Seeing is simulated by calling SimSeeing::get_wavefront() in simul_seeing().
 - A WFC error is simulated in simul_wfcerr(). This can be useful to test 
  routines -- an error generated by the same device that corrects it should work 
  at 100%.
 - A WFC correction is applied in simul_wfc().
 - The telescope mask is applied (i.e. a circular crop mask) in simul_telescope()
 - The microlens array is simulated in simul_wfs()
 - The frame capture on the CCD is simulated in simul_capture().
  
 \section simulcam_cfg Configuration parameters
 
 - noisefac: SimulCam::noisefac
 - noiseamp: SimulCam::noiseamp
 - mlafac: SimulCam::mlafac

 Configuration parameters for SimSeeing:
 
 - wavefront_file: SimSeeing::wffile
 - seeingfac: SimSeeing::seeingfac
 - windspeed.{x,y}: SimSeeing::windspeed
 - windtype: SimSeeing::windtype (also see SimSeeing::wind_t)
 - cropsize.{x,y}: SimSeeing::cropsize

 Configuration parameters for Shwfs:
 - See Shwfs::

 Configuration parameters for SimulWfc:
 - See SimulWfc::
 
 \section simulcam_netio Network commands
 
 - get/set noisefac: see above
 - get/set noiseamp: see above
 - get/set seeingfac: see above
 - get/set mlafac: see above
 - get/set windspeed: see above
 - get/set windtype: see above
 - get/set wfcerr_retain: see SimulCam::wfcerr_retain. 
 - get/set telapt_fill: subaperture should have at least this fraction of light in order to be considered
 - get/set simwf: simulate wavefront (atmospheric seeing)
 - get/set simtel: simulate telescope (aperture)
 - get/set simwfcerr: simulate wfc as error source
 - get/set simmla: siulate microlens array to ccd
 - get/set simwfc: simulate wavefront corrector device
 
 \section simulcam_dep Dependencies

 - SimSeeing for generating wavefront screens
 - SimulWfc for simulating a 'wfc error' and for correcting wavefront errors
 - Shwfs for simulating a SHWFS.
 
*/
class SimulCam: public Camera {
private:
	SimSeeing seeing;										//!< This class simulates the atmosphere

	SimulWfc &simwfcerr;								//!< This class simulates a wavefront corrector as a source of errors
	SimulWfc &simwfc;										//!< This class simulates a wavefront corrector
	
	size_t out_size;										//!< Size of frame_out
	void *frame_out;										//!< Frame to store simulated image
	gsl_matrix *frame_raw;							//!< Raw frame used to calculate wavefront errors etc.

	double telradius;										//!< Telescope radius (fraction of CCD)
	gsl_matrix *telapt;									//!< Telescope aperture mask
	double telapt_fill;									//!< How much subaperture should be within the telescope aperture to be processed
	
	double noisefac;										//!< Noise fill factor for CCD
	double noiseamp;										//!< Noise amplitude as fraction of maximum intensity
	double mlafac;											//!< Factor to multiply wavefront with before imaging (i.e. image magnification) (like seeingfac, except this also takes simulated correction into account)
	double wfcerr_retain;								//!< Ratio of old and new random wfc error to add. wfc_err = old_err * fac + new_err * (1-fac). 0.9 means the wavefront changes very slowly (high correlation between consecutive frames), 0.1 means it changes very rapidly (low correlation).
	gsl_vector_float *wfcerr_act;				//!< Vector to store simulated wfc errr actuation command
	
	void setup();												//!< Allocate memory etc.
	
public:
	Shwfs shwfs;												//!< Reference to WFS we simulate (i.e. for configuration)
	SimulCam(Io &io, foamctrl *const ptc, const string name, const string port, Path const &conffile, SimulWfc &_simwfc, SimulWfc &_simwfcerr, const bool online=true);
	~SimulCam();

	bool do_simwf;											//!< Simulate seeing wavefront?
	bool do_simtel;											//!< Simulate telescope aperture?
	bool do_simwfcerr;									//!< Simulate wavefront corrector as error?
	bool do_simmla;											//!< Simulate microlens array?
	bool do_simwfc;											//!< Simulate wavefront corrector?
	
	void set_noisefac(const double val) { noisefac = val; }
	double get_noisefac() const { return noisefac; }
	void set_noiseamp(const double val) { noiseamp = val; }
	double get_noiseamp() const { return noiseamp; }
	void set_seeingfac(const double val) { seeing.seeingfac = val; }
	double get_seeingfac() const { return seeing.seeingfac; }
	void set_mlafac(const double val) { mlafac = val; }
	double get_mlafac() const { return mlafac; }
	void set_wfcerr_retain(const double val) { wfcerr_retain = val; }
	double get_wfcerr_retain() const { return wfcerr_retain; }
	void set_telapt_fill(const double val) { telapt_fill = val; }
	double get_telapt_fill() const { return telapt_fill; }
	
	/*! @brief Generate binary circulate telescope aperture mask
	 
	 The telescope mask will have value 1 inside the radius and 0 outside.
	 @param [out] *apt Will hold the aperture mask
	 @param [in] rad Radius of the aperture mask
	 */
	void gen_telapt(gsl_matrix *const apt, const double rad) const;


	/*! @brief Initialise simulation by zero'ing out matrix
	 
	 @param [out] *wave_in Matrix to initialise
	 */
	void simul_init(gsl_matrix *const wave_in);
	
	/*! @brief Simulate atmospheric seeing given pre-allocated matrix
	 
	 Uses SimulSeeing for generation of the simulated perturbed wavefront.
	 @param [out] *wave_out Will hold a simulated wavefront
	 */
	int simul_seeing(gsl_matrix *const wave_out);
	
	/*! @brief Simulate telescope mask given input wavefront
	 
	 Apply telescope mask generated with gen_telapt() to wavefront.
	 @param [in, out] *wave_in Wavefront to be multiplied with telescope mask
	 */
	void simul_telescope(gsl_matrix *const wave_in) const;

	/*! @brief Simulate wavefront corrector given input wavefront
	 
	 Apply simulated wavefront correction to input wavefront by adding a correction to the input wave.
	 @param [in, out] *wave_in Wavefront to be corrected
	 */
	void simul_wfc(gsl_matrix *const wave_in) const;

	/*! @brief Simulate wavefront corrector *error* given input wavefront
	 
	 Generate a random actuation vector for the WFC and apply this to the 
	 wavefront. This can be used to test if a setup can correct for the errors 
	 it introducs itself. Using this as an error source should give 100% correction
	 @param [in, out] *wave_in Wavefront where the error is to be applied
	 */
	void simul_wfcerr(gsl_matrix *const wave_in);

	/*! @brief Simulate wavefront sensor optics given an input wavefront.
	 
	 Simulates wavefront sensor optics (i.e. microlens array) given an input wavefront.
	 @param [in, out] *wave_in Will hold the processed wavefront as an *image*
	 */
	void simul_wfs(gsl_matrix *const wave_in) const;

	/*! @brief Simulate CCD frame capture given input image
	 
	 Given an input image (as double matrix), simulate the CCD frame capture
	 process including things as exposure, offset and noise. 
	 
	 This function is a wrapper for _simul_capture() which does all the work. 
	 The wrapper casts the data pointer to the correct type.
	 
	 @param [in] *im_in Image to be processed
	 @param [out] *frame_out Output frame in arbitrary type (like uint8_t, uint16_t) (pre-allocated)
	 */
	void simul_capture(gsl_matrix *const im_in, void *const frame_out) const;
	template <class T> void _simul_capture(gsl_matrix *const im_in, T *const frame_out) const;
	
	// From Camera::
	void cam_handler();
	void cam_set_exposure(const double value);
	double cam_get_exposure();
	void cam_set_interval(const double value);
	double cam_get_interval();
	void cam_set_gain(const double value);
	double cam_get_gain();
	void cam_set_offset(const double value);
	double cam_get_offset();
	
	void cam_set_mode(const mode_t newmode);
	void do_restart();
	
	// From Devices::
	void on_message(Connection *const conn, string);
};

#endif // HAVE_SIMULCAM_H

/*!
 \page dev_cam_simulcam Simulation camera devices
 
 The SimulCam class is an end-to-end simulation camera.
 
 */
