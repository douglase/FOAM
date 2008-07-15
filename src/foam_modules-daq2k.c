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
	@file foam_modules-daq2k.c
	@author @authortim
	@date 2008-07-15
 
	@brief This file contains routines to drive a DaqBoard 2000 PCI board
 \section Info
 
 The IOtech DaqBoard/2000 series are PCI cards which have several digital and analog I/O ports, which 
 can be used to aqcuire data from various sources. This board can also be used to drive tip-tilt
 mirrors, telescope (using analog outputs) and filterwheels (using several digital output ports)
 though, making it a good choice for a general purpose IO board in AO setups.
 
 This module supports multiple daqboards with various DAC channels, and supports the 8225 digital IO chips 
 providing a total of 3 8-bit ports, of which the last port is split into two 4 bit ports.
 
 It does not support 'banks'.
 
 More information can be found at the manufacturers website:\n
 <tt>http://www.iotech.com/catalog/daq/dbseries2.html</tt>
 
 Especially take a look at the programmer's manual which has a useful function reference at the end.
 
 This module can compile on its own:\n
 <tt>gcc foam_modules-daq2k.c -lc -lm -ldaqx -Wall -DFOAM_MODDAQ2K_ALONE=1 -I../../../../misc/daqboard_iotech220_portedto26/include/ -L../../../../misc/daqboard_iotech220_portedto26/lib</tt>
 
 \section Functions
 
 \li drvInitDaq2k() - Initialize the Daqboard 2000 (call this first!)
 \li drvCloseDaq2k() - Close the Daqboard 2000 (call this at the end!)
 \li drvDaqSetDAC() - Write analog output to specific ports (ranges from 0 to 65535 (16bit))
 \li drvDaqSetDACs() - Write analog output to all (ranges from 0 to 65535 (16bit))
 \li drvDaqSetP2() - Write digital output to specific ports
 
 \section Configuration
 
 Configuration for this modules goes through define statements:
 
 \li \b FOAM_MODDAQ2K_ALONE (*undef*), ifdef, compiles on its own (implies FOAM_DEBUG)
 \li \b FOAM_DEBUG (*undef*), ifdef, gives lowlevel prinft debug statements
 
 \section Dependencies
 
 This module depends on the daqx library used to access daqboards.
 
 \section History
 
 \li 2008-04-14: api change, configuration done with datatypes instead of defines
 \li 2008-04-02: init
*/

// HEADERS //
/***********/

#include "foam_modules-daq2k.h"

// LOCAL FUNCTIONS //
/*******************/

/*!
 @brief Local function to initialize the DAC part of the Daqboard
 
 This configures the digital to analog converting ports on the Daqboard
 to be used. All channels are configured to output constant DC voltages 
 which are initialized at 0V.
 
 If some configuration fails, this function sets board->dacinit to 0.
 This function returns EXIT_SUCCESS immediately if the corresponding 
 board->fd is -1 (and thus the devices failed to open).
 
 @param [in] *board The board to initialize the DACs for
 @return EXIT_SUCCESS on success, EXIT_FAILURE otherwise.
 */
static int initDaqDac(mod_daq2k_board_t *board);

/*!
 @brief Local function to initialize the digital IO part of the Daqboard
 
 Configures the digital IO ports (portA, portB and portC, 8bit each) 
 corresponding to the board->iop2conf configuration.
 
 If some configuration fails, board->iop2init is set to 0.
 This function returns EXIT_SUCCESS immediately if the corresponding 
 board->fd is -1 (and thus the devices failed to open).
 
 @param [in] *board The board to initialize the IO ports for
 @return EXIT_SUCCESS on success, EXIT_FAILURE otherwise.
 */
static int initDaqIOP2(mod_daq2k_board_t *board);


static int initDaqDac(mod_daq2k_board_t *board) {
	// FD not open? then just return
	if (board->fd == -1)
		return EXIT_SUCCESS;
	
	int chan;
	DaqError err;	
	CHAR errmsg[512];
#ifdef FOAM_DEBUG
	printf("Opening %d DAC channels on board %s, channel...", board->nchans, board->device);
#else
	logDebug(0,"Opening %d DAC channels on board %s, channel...", board->nchans, board->device);
#endif
	
	for (chan=0; chan < board->nchans; chan++) {
		// configure output mode on this channel to be DdomVoltage (i.e. a constant DC)
		daqDacSetOutputMode(board->fd, DddtLocal, chan, DdomVoltage);
		// set the initial voltage to 0, does the least harm in any situation ;)
		err = daqDacWt(board->fd, DddtLocal, chan, (WORD) 0);
		// oops, we got an error! return immediately, and do not use Daqboard DAC routines anymore
		if (err != DerrNoError) {
			daqFormatError(err, (PCHAR) errmsg);
#ifdef FOAM_DEBUG
			printf("Error writing voltage to DAC ports for board %s: %s\n", board->device, errmsg);
#else
			logWarn("Error writing voltage to DAC ports for board %s: %s", board->device, errmsg);
#endif
			board->dacinit = 0;
			return EXIT_FAILURE;
		}
#ifdef FOAM_DEBUG
		printf("%d...", chan);
#else
		logDebug(LOG_NOFORMAT ,"%d...", chan);
#endif
		
	}
	
#ifdef FOAM_DEBUG
	printf("done!\n");
#else
	logDebug(LOG_NOFORMAT ,"done!\n");
#endif
	
	return EXIT_SUCCESS;
}

static int initDaqIOP2(mod_daq2k_board_t *board) {
	// FD not open? then just return
	if (board->fd == -1)
		return EXIT_SUCCESS;
	
	DaqError err;
	CHAR errmsg[512];	
	DWORD config;
	
	// set digital IO ports A, B as outputs, C as input
	// Create 8255 config number for these (0,0,1,1) settings
	//  first 0: A as output
	//  second 0: B as output
	// 3rd, 4th 1: C low and high nibble as inputs
#ifdef FOAM_DEBUG
	printf("Setting up P2 on board %s as: (0x%x, 0x%x, 0x%x, 0x%x) ", board->device, \
		   board->iop2conf[0], board->iop2conf[1], \
		   board->iop2conf[2], board->iop2conf[3]);
#endif
	
	err = daqIOGet8255Conf(board->fd, \
						   (BOOL) board->iop2conf[0], \
						   (BOOL) board->iop2conf[1], \
						   (BOOL) board->iop2conf[2], \
						   (BOOL) board->iop2conf[3], &config);
	if (err != DerrNoError) {
		daqFormatError(err, (PCHAR) errmsg);
#ifdef FOAM_DEBUG
		printf("Error configuring digital IO on 8255 for board %s: %s\n", board->device, errmsg);
#else
		logWarn("Error configuring digital IO on 8255 for board %s: %s", board->device, errmsg);
#endif		
		board->iop2init = 0;
		return EXIT_FAILURE;
	}
	
	
	// write settings and config number to internal register
	err = daqIOWrite(board->fd, DiodtLocal8255, Diodp8255IR, 0, DioepP2, config);
	
	if (err != DerrNoError) {
		daqFormatError(err, (PCHAR) errmsg);
#ifdef FOAM_DEBUG
		printf("Error configuring digital IO on 8255 for board %s: %s\n", board->device, errmsg);
#else
		logWarn("Error configuring digital IO on 8255 for board %s: %s", board->device, errmsg);
#endif
		board->iop2init = 0;
		return EXIT_FAILURE;
	}
	
	// init IO ports to 0 (off) no error checking because we don't really care here
	daqIOWrite(board->fd, DiodtLocal8255, Diodp8255A, 0, DioepP2, 1);
	daqIOWrite(board->fd, DiodtLocal8255, Diodp8255B, 0, DioepP2, 1);
	daqIOWrite(board->fd, DiodtLocal8255, Diodp8255C, 0, DioepP2, 1);
	
#ifdef FOAM_DEBUG
	printf("Successfully set up P2!\n");
#endif
	
	return EXIT_SUCCESS;
}

// PUBLIC FUNCTIONS //
/********************/

int drvInitDaq2k(mod_daq2k_board_t *board) {
	// set these variables to 1, assume success
	board->dacinit = 1;
	board->iop2init = 1;
	
	board->fd = daqOpen(board->device);
	if (board->fd == -1) {
#ifdef FOAM_DEBUG
		printf("Could not connect to board %s: %s\n", board->device, strerror(errno));
#else
		logWarn("Could not connect to board %s: %s", board->device, strerror(errno));
#endif
	}
	
#ifdef FOAM_DEBUG
	printf("Opened daqboard %s\n", board->device);
#endif
	
	// try to init the DAC circuits
	initDaqDac(board);
	
	// try to init the digital IO circuits
	initDaqIOP2(board);

	
	if (board->dacinit != 1 && board->iop2init != 1) {
#ifdef FOAM_DEBUG
		printf("Failed to set up Daqboard %s\n", board->device);
#else
		logWarn("Failed to set up Daqboard %s", board->device);
#endif
		return EXIT_FAILURE;
	}

	
	if (board->iop2init != 1)
#ifdef FOAM_DEBUG
		printf("Failed to set IO ports on Daqboard %s\n", board->device);
#else
		logWarn("Failed to set IO ports on Daqboard %s", board->device);
#endif
	
	if (board->dacinit != 1)
#ifdef FOAM_DEBUG
		printf("Failed to set up DAC units on Daqboard %s\n", board->device);
#else
		logWarn("Failed to set up DAC units on Daqboard %s", board->device);
#endif
	
#ifdef FOAM_DEBUG
	printf("Daqboard %s is now set up!\n", board->device);
#endif
	
	return EXIT_SUCCESS;		
}

void drvCloseDaq2k(mod_daq2k_board_t *board) {
	
	// close open daqboard (fd != -1)
	if (board->fd >= 0)
		daqClose(board->fd);
	
}

int drvDaqSetP2(mod_daq2k_board_t *board, int port, int bitpat) {
	// port must be either 0, 1, 2 or 3 for portA, portB, portC high and low
	// respectively
	if (board->fd == -1)
		return EXIT_SUCCESS;

	// init IO ports to 0 (off) no error checking because we don't really care here
	// we mask bitpat with either 2^8 in the case when writing to 8-bit ports
	// or with 2^4 in the case when writing to 4-bit ports.
	switch (port) {
		case 0:
			if (board->iop2conf[0] != 0) // port is not output, can't write
				return EXIT_FAILURE;

			daqIOWrite(board->fd, DiodtLocal8255, Diodp8255A, 0, DioepP2, bitpat & 255);
			break;
		case 1:
			if (board->iop2conf[1] != 0) // port is not output, can't write
				return EXIT_FAILURE;

			daqIOWrite(board->fd, DiodtLocal8255, Diodp8255B, 0, DioepP2, bitpat & 255);
			break;
		case 2:
			if (board->iop2conf[2] != 0) // port is not output, can't write
				return EXIT_FAILURE;

			daqIOWrite(board->fd, DiodtLocal8255, Diodp8255CHigh, 0, DioepP2, bitpat & 16);
			break ;
		case 3:
			if (board->iop2conf[3] != 0) // port is not output, can't write
				return EXIT_FAILURE;

			daqIOWrite(board->fd, DiodtLocal8255, Diodp8255CLow, 0, DioepP2, bitpat & 16);
			break;
	}
	
	return EXIT_SUCCESS;
}

void drvDaqSetDAC(mod_daq2k_board_t *board, int chan, int val) {
	if (board->fd == -1)
		return;
	
	daqDacWt(board->fd, DddtLocal, (DWORD) chan, (WORD) (val & 0xffff));
}

void drvDaqSetDACs(mod_daq2k_board_t *board, int val) {
	if (board->fd == -1)
		return;
	
	int i;
	for (i=0; i<board->nchans; i++)
		daqDacWt(board->fd, DddtLocal, (DWORD) i, (WORD) val);
}

#ifdef FOAM_MODDAQ2K_ALONE
int main() {
	int i, j;
	mod_daq2k_board_t board = {
		.device = "daqBoard2k0",
		.nchans = 4,
		.minvolt = -10.0,
		.maxvolt = 10.0,
		.iop2conf = {0,0,1,1},
	};
	
	if (drvInitDaq2k(&board) != EXIT_SUCCESS)
		exit(-1);
	
	printf("Opened DAQboard %s!\n", board.device);
	
	// set stdout to unbuffered, otherwise we won't see intermediate messages
	setvbuf(stdout, NULL, _IONBF, 0);
	
	// setting digital IO ports now //
	//////////////////////////////////
#if (0)
	printf("Trying to set some bit patterns values on P2:\n");
	printf("\n");
	printf("portA and portB (8bit): ");
	for (i=1; i<256; i *= 2) { 
		printf("0x%u...", i);
		if (drvDaqSetP2(&board, 0, i) != EXIT_SUCCESS || drvDaqSetP2(&board, 1, i) != EXIT_SUCCESS) 
			printf("(failed), ");
		else 
			printf("(ok), ");
		sleep(1);
	}
	i=255;
	printf("0x%u...", i);
	if (drvDaqSetP2(&board, 0, i) != EXIT_SUCCESS || drvDaqSetP2(&board, 1, i) != EXIT_SUCCESS) 
		printf("(failed), ");
	else 
		printf("(ok), ");
	
	printf("\n");
	sleep(1);
	
	printf("\n");
	printf("portC low and high (4bit), this should fail in default config: ");
	for (i=1; i<16; i *= 2) {
		printf("0x%u...", i);
		if (drvDaqSetP2(&board, 2, i) != EXIT_SUCCESS || drvDaqSetP2(&board, 3, i) != EXIT_SUCCESS) 
			printf("(failed), ");
		else 
			printf("(ok), ");
		sleep(1);
	}
	
	i=15;
	printf("0x%u...", i);
	if (drvDaqSetP2(&board, 2, i) != EXIT_SUCCESS || drvDaqSetP2(&board, 3, i) != EXIT_SUCCESS) 
		printf("(failed), ");
	else 
		printf("(ok), ");
	
	printf("\n");
	sleep(1);
	printf("\n");
	
	// setting digital io now, do FW //
	///////////////////////////////////
	
	printf("Will now drive filterwheel connected to port A, sending values 0 through 7 by using the first three bits\n");
	for (i=0; i<8; i++) {
		printf("0x%u...", i);
		drvDaqSetP2(&board,0,i);
		sleep(1);
	}
	printf("done\n");
	sleep(1);
#endif
	// setting analog outputs  now //
	/////////////////////////////////
	printf("Setting some voltages on channels 0 and 1 of board 0 now:\n");
	for (j=0; j<2; j++) {
		printf("(chan %d: going through voltages 0 -- 10 in 20 seconds)\n", j);
		for (i=0; i<=100; i++) {
			if (i % 10 == 0) printf("%d%%", i);
			else printf(".");
			//drvDaqSetDACs(&board, i*65536/100);
			drvDaqSetDAC(&board, j, 65536/2 + i*65536/2/100);
			usleep(200000);
		}
		printf("..done\n");
		printf("\n");
	}
	
	drvCloseDaq2k(&board);	
	printf("Closed DAQboard!\n");
	return EXIT_SUCCESS;
}
#endif
