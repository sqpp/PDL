/* Stub implementation of rs232/slicer API for native Linux build.
 * Serial/slicer hardware is not used; all functions no-op or return "not present". */

#include "platform/pdw_linux_types.h"
#include "Headers/SLICER.H"
#ifndef EXTERN_C
#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif
#endif
#include "utils/rs232.h"
#include <stddef.h>

#define SLICER_BUFSIZE 10000

static unsigned short s_freqdata[SLICER_BUFSIZE];
static unsigned char  s_linedata[SLICER_BUFSIZE];
static unsigned long  s_cpstn;

int rs232_connect(SLICER_IN_STR *pInSlicer, SLICER_OUT_STR *pOutSlicer)
{
	(void)pInSlicer;
	if (pOutSlicer) {
		pOutSlicer->freqdata = s_freqdata;
		pOutSlicer->linedata = s_linedata;
		pOutSlicer->cpstn    = &s_cpstn;
		pOutSlicer->bufsize  = SLICER_BUFSIZE;
	}
	return RS232_NO_DUT;
}

int rs232_transmit_data(unsigned char buffer[], int nBytes)
{
	(void)buffer;
	(void)nBytes;
	return RS232_NO_CONNECTION;
}

int rs232_get_rx_data(unsigned char buffer[], int nBytes)
{
	(void)buffer;
	(void)nBytes;
	return RS232_NO_CONNECTION;
}

int rs232_disconnect(void)
{
	return RS232_SUCCESS;
}

int rs232_read(void)
{
	return -1;
}

int slicer_read(void)
{
	return -1;
}

int OpenComPort(void)
{
	return RS232_NO_DUT;
}

int WriteComPort(char *szLine)
{
	(void)szLine;
	return 0;
}

int CloseComPort(void)
{
	return 0;
}

static int s_comPortsArr[11];

int *FindComPorts(void)
{
	s_comPortsArr[0] = 0;
	return s_comPortsArr;
}

int GetRs232DriverType(void)
{
	return DRIVER_TYPE_NOT_LOADED;
}
