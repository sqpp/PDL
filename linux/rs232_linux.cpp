/*
 * Linux RS232 support via termios.
 * - Decode input: bitstream converter → freqdata/linedata → pdl_decode()
 * - Message output: optional second open for WriteComPort()
 * Proprietary Windows slicer driver is not available.
 */
#ifdef __linux__
#include "platform/pdl_linux_types.h"
#include "Headers/SLICER.H"
#ifndef EXTERN_C
#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif
#endif
#include "utils/rs232.h"
#include "Headers/pdl.h"
#include "Headers/decode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <termios.h>
#include <pthread.h>
#include <sys/select.h>

#define SLICER_BUFSIZE 10000

static unsigned short s_freqdata[SLICER_BUFSIZE];
static unsigned char  s_linedata[SLICER_BUFSIZE];
static unsigned long  s_cpstn;

static int s_fd_out = -1;   /* WriteComPort / OpenComPort */
static int s_fd_in = -1;    /* rs232_connect decode input */
static int s_comPortsArr[16];
static char s_port_paths[16][64];
static int s_nports = 0;

static double s_nTiming = 500.0;
static volatile int s_rx_alive = 0;
static pthread_t s_rx_thread;
static int s_rx_thread_running = 0;

static speed_t baud_to_speed(int bitrate)
{
	switch (bitrate) {
	case 1200: return B1200;
	case 2400: return B2400;
	case 4800: return B4800;
	case 9600: return B9600;
	case 19200: return B19200;
	case 38400: return B38400;
	case 57600: return B57600;
	case 115200: return B115200;
	default: return B19200;
	}
}

static int open_tty(const char *path, int bitrate)
{
	int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) return -1;
	struct termios tio;
	if (tcgetattr(fd, &tio) != 0) { close(fd); return -1; }
	cfmakeraw(&tio);
	speed_t sp = baud_to_speed(bitrate > 0 ? bitrate : 19200);
	cfsetispeed(&tio, sp);
	cfsetospeed(&tio, sp);
	tio.c_cflag |= (CLOCAL | CREAD);
	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = 1; /* 100 ms */
	if (tcsetattr(fd, TCSANOW, &tio) != 0) { close(fd); return -1; }
	return fd;
}

static const char *port_path_for_index(int one_based)
{
	if (s_nports == 0) FindComPorts();
	if (one_based < 1 || one_based > s_nports) return NULL;
	return s_port_paths[one_based - 1];
}

static void update_nTiming(void)
{
	extern double ct1600;
	switch (Profile.comPortRS232) {
	case 1:
		s_nTiming = 500.0;
		break;
	case 3:
		s_nTiming = 1.0 / ((float)8000 * 839.22e-9);
		break;
	case 2:
	default:
		s_nTiming = (ct1600 > 1.0) ? ct1600 : 993.0;
		break;
	}
}

int rs232_read(void)
{
	if (s_fd_in < 0) return 0;

	unsigned char byData[256];
	ssize_t nread = read(s_fd_in, byData, sizeof(byData));
	if (nread < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
		return -1;
	}
	if (nread == 0) return 0;

	for (ssize_t i = 0; i < nread; i++) {
		for (int j = 7; j >= 0; j--) {
			int bit;
			if (Profile.fourlevel) {
				j--;
				bit = (byData[i] >> j) & 3;
			} else {
				bit = (byData[i] >> j) & 1;
			}
			s_linedata[s_cpstn] = (unsigned char)(bit << 4);
			s_freqdata[s_cpstn] = (unsigned short)(s_nTiming > 65535.0 ? 65535.0 : s_nTiming);
			s_cpstn++;
			if (s_cpstn >= SLICER_BUFSIZE)
				s_cpstn = 0;
		}
	}
	return (int)nread;
}

int slicer_read(void)
{
	/* Proprietary Windows slicer packets are not supported on Linux. */
	return -1;
}

static void *rx_thread_fn(void *arg)
{
	(void)arg;
	while (s_rx_alive) {
		if (s_fd_in < 0) break;
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(s_fd_in, &rfds);
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 50000;
		int r = select(s_fd_in + 1, &rfds, NULL, NULL, &tv);
		if (r > 0 && FD_ISSET(s_fd_in, &rfds))
			rs232_read();
	}
	return NULL;
}

static void stop_rx_thread(void)
{
	if (!s_rx_thread_running) return;
	s_rx_alive = 0;
	pthread_join(s_rx_thread, NULL);
	s_rx_thread_running = 0;
}

int rs232_connect(SLICER_IN_STR *pInSlicer, SLICER_OUT_STR *pOutSlicer)
{
	if (pOutSlicer) {
		pOutSlicer->freqdata = s_freqdata;
		pOutSlicer->linedata = s_linedata;
		pOutSlicer->cpstn    = &s_cpstn;
		pOutSlicer->bufsize  = SLICER_BUFSIZE;
	}

	rs232_disconnect();

	int port = pInSlicer && pInSlicer->com_port ? (int)pInSlicer->com_port : Profile.comPort;
	const char *path = port_path_for_index(port);
	if (!path) {
		fprintf(stderr, "RS232 decode: no port (comPort=%d)\n", port);
		return RS232_NO_DUT;
	}

	if (Profile.comPortRS232 <= 0)
		Profile.comPortRS232 = 2; /* default converter timing */
	update_nTiming();

	int bitrate = Profile.comRS232bitrate > 0 ? Profile.comRS232bitrate : 19200;
	s_fd_in = open_tty(path, bitrate);
	if (s_fd_in < 0) {
		fprintf(stderr, "RS232 decode: open %s failed: %s\n", path, strerror(errno));
		return RS232_NO_DUT;
	}

	memset(s_freqdata, 0, sizeof(s_freqdata));
	memset(s_linedata, 0, sizeof(s_linedata));
	s_cpstn = 0;

	s_rx_alive = 1;
	if (pthread_create(&s_rx_thread, NULL, rx_thread_fn, NULL) != 0) {
		close(s_fd_in);
		s_fd_in = -1;
		s_rx_alive = 0;
		fprintf(stderr, "RS232 decode: rx thread failed\n");
		return RS232_UNKNOWN;
	}
	s_rx_thread_running = 1;
	fprintf(stderr, "RS232 decode: listening on %s @ %d (timing mode %d)\n",
		path, bitrate, Profile.comPortRS232);
	return RS232_SUCCESS;
}

int rs232_disconnect(void)
{
	stop_rx_thread();
	if (s_fd_in >= 0) {
		close(s_fd_in);
		s_fd_in = -1;
	}
	return RS232_SUCCESS;
}

int rs232_transmit_data(unsigned char buffer[], int nBytes)
{
	(void)buffer; (void)nBytes;
	return RS232_NO_CONNECTION;
}

int rs232_get_rx_data(unsigned char buffer[], int nBytes)
{
	(void)buffer; (void)nBytes;
	return RS232_NO_CONNECTION;
}

int OpenComPort(void)
{
	if (s_fd_out >= 0) { close(s_fd_out); s_fd_out = -1; }
	if (s_nports == 0) FindComPorts();
	int idx = Profile.comPort;
	if (idx < 1 || idx > s_nports) {
		fprintf(stderr, "RS232 out: no COM port selected (found %d)\n", s_nports);
		return RS232_NO_DUT;
	}
	/* Prefer a separate FD; if decode already holds this path, reopen RDWR share carefully.
	 * Most USB-serial adapters allow only one open — skip out if same as decode. */
	const char *path = s_port_paths[idx - 1];
	if (s_fd_in >= 0) {
		fprintf(stderr, "RS232 out: decode input owns %s — output disabled on same port\n", path);
		return RS232_NO_DUT;
	}
	int bitrate = Profile.comRS232bitrate > 0 ? Profile.comRS232bitrate : 9600;
	s_fd_out = open_tty(path, bitrate);
	if (s_fd_out < 0) {
		fprintf(stderr, "RS232 out: open %s failed: %s\n", path, strerror(errno));
		return RS232_NO_DUT;
	}
	fprintf(stderr, "RS232 out: opened %s\n", path);
	return RS232_SUCCESS;
}

int WriteComPort(char *szLine)
{
	if (s_fd_out < 0 || !szLine) return 0;
	size_t n = strlen(szLine);
	ssize_t w = write(s_fd_out, szLine, n);
	return (w > 0) ? (int)w : 0;
}

int CloseComPort(void)
{
	if (s_fd_out >= 0) { close(s_fd_out); s_fd_out = -1; }
	return 0;
}

static int add_port(const char *path)
{
	if (s_nports >= 15) return 0;
	if (access(path, R_OK | W_OK) != 0 && access(path, F_OK) != 0) return 0;
	strncpy(s_port_paths[s_nports], path, sizeof(s_port_paths[0]) - 1);
	s_port_paths[s_nports][sizeof(s_port_paths[0]) - 1] = '\0';
	s_comPortsArr[s_nports] = s_nports + 1;
	s_nports++;
	return 1;
}

static void scan_prefix(const char *dir, const char *prefix)
{
	DIR *d = opendir(dir);
	if (!d) return;
	struct dirent *de;
	while ((de = readdir(d)) != NULL) {
		if (strncmp(de->d_name, prefix, strlen(prefix)) != 0) continue;
		char path[128];
		snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
		add_port(path);
	}
	closedir(d);
}

int *FindComPorts(void)
{
	s_nports = 0;
	memset(s_comPortsArr, 0, sizeof(s_comPortsArr));
	scan_prefix("/dev", "ttyUSB");
	scan_prefix("/dev", "ttyACM");
	scan_prefix("/dev", "ttyS");
	s_comPortsArr[s_nports] = 0;
	return s_comPortsArr;
}

const char *GetComPortPath(int one_based_index)
{
	if (s_nports == 0) FindComPorts();
	if (one_based_index < 1 || one_based_index > s_nports) return NULL;
	return s_port_paths[one_based_index - 1];
}

int GetRs232DriverType(void)
{
	if (s_nports == 0) FindComPorts();
	return (s_nports > 0) ? DRIVER_TYPE_RS232 : DRIVER_TYPE_NOT_LOADED;
}

#endif
