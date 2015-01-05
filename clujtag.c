/*
 *  Lib(X)SVF  -  A library for implementing SVF and XSVF JTAG players
 *
 *  Copyright (C) 2009  RIEGL Research ForschungsGmbH
 *  Copyright (C) 2009  Clifford Wolf <clifford@clifford.at>
 *  Copyright (C) 2014  Cluster <clusterrr@clusterrr.com>
 *  
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */
#ifdef __MINGW32__
#define WINDOWS
#endif

#include "libxsvf.h"
#include "jtag_commands.h"

#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <stdint.h>
#include <fcntl.h>
#ifdef WINDOWS
#include <windows.h>
#else
#include <termios.h>
#endif

#ifdef WINDOWS
static void write_port(HANDLE portHandle, uint8_t data)
{
	DWORD writed = 0;
	if (WriteFile(portHandle, &data, sizeof(data), &writed, NULL) && (writed == 1)) return;
	int error = GetLastError();
	fprintf(stderr, "Write error: %d\r\n", error);
	exit(1);
}
#else
static void write_port(int portHandle, uint8_t data)
{
	int res;
	do
	{
	    res  = write(portHandle, &data, 1);
	    if (res == -1) usleep(10);
	} while (res == -1 && errno == EAGAIN);
	if (res == -1)
	{
	    perror("Write error");
	    exit(1);
	}
}
#endif

#ifdef WINDOWS
static uint8_t read_port(HANDLE portHandle)
{
	uint8_t buffer;
	DWORD read = 0;
	if (ReadFile(portHandle, &buffer, 1, &read, NULL) && (read == 1)) return buffer;
	fprintf(stderr, "Read error: %d\r\n", (int)GetLastError());
	exit(1);
}
#else
static uint8_t read_port(int portHandle)
{
	uint8_t data;
	int res, t = 0;
	const int timeout = 5000;
	do
	{
		res = read(portHandle, &data, 1);
		t++;
		if (res == 0) usleep(10);
	}
	while ((res == 0) && (t < timeout));
	if (t == timeout)
	{
		printf("Read timeout\n");
		exit(1);
	}
	if (res == 1) return data;
	perror("Read error");
	exit(1);
}
#endif

struct udata_s {
	char comPort[512];
#ifdef WINDOWS
	HANDLE portHandle;
#else
	int portHandle;
#endif
	FILE *f;
	int verbose;
};

static int h_setup(struct libxsvf_host *h)
{
	struct udata_s *u = h->user_data;
	if (u->verbose >= 2) {
		fprintf(stderr, "[SETUP]\n");
		fflush(stderr);
	}

#ifdef WINDOWS

	char devicePath[50];
	sprintf(devicePath, "\\\\.\\%s", u->comPort);

	HANDLE mHandle = CreateFile(devicePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,/* FILE_FLAG_OVERLAPPED*/0, NULL);
	if (mHandle == INVALID_HANDLE_VALUE)
	{
		fprintf(stderr, "Can't open serial port %s\r\n", u->comPort);
		return -1;
	}

	DCB dcb;

	FillMemory(&dcb, sizeof(dcb), 0);
	if (!GetCommState(mHandle, &dcb))     // get current DCB
	{
	fprintf(stderr, "Can't get DCB\r\n");
	return -1;
	}
	
	dcb.BaudRate = CBR_256000;
			
	if (!SetCommState(mHandle, &dcb))
	{
		fprintf(stderr, "Can't set serial port settings\r\n");
		return -1;
	}

	u->portHandle = mHandle;

#else

	int fd;
	struct termios options;
	fd = open(u->comPort, O_RDWR | O_NOCTTY);
	if (fd == -1)
	{
		perror("Can't open serial port");
		return -1;
	}

	fcntl(fd, F_SETFL, FNDELAY);

	bzero(&options, sizeof(options));
	options.c_cflag = B230400 | CS8 | CLOCAL | CREAD;
	options.c_iflag = IGNPAR;
	options.c_oflag = 0;
	cfsetispeed(&options, B230400);
	cfsetospeed(&options, B230400);
	tcsetattr(fd, TCSANOW, &options);
	tcflush(fd, TCIFLUSH);
	u->portHandle = fd;
	
#endif

	// Reset device
	write_port(u->portHandle, 0xFF);
	// Setup JTAG port
	write_port(u->portHandle,JTAG_SETUP);

  return 0;
}

static int h_shutdown(struct libxsvf_host *h)
{
	struct udata_s *u = h->user_data;
	if (u->verbose >= 2) {
		fprintf(stderr, "[SHUTDOWN]\n");
		fflush(stderr);
	}
	
	if (u->portHandle)
	{
		write_port(u->portHandle, JTAG_SHUTDOWN);	
#ifdef WINDOWS
		CloseHandle(u->portHandle);
#else
		close(u->portHandle);
#endif
	}

	return 0;
}

static void h_udelay(struct libxsvf_host *h, long usecs, int tms, long num_tck)
{
	struct udata_s *u = h->user_data;
	if (u->verbose >= 3) {
		fprintf(stderr, "[DELAY:%ld, TMS:%d, NUM_TCK:%ld]\n", usecs, tms, num_tck);
		fflush(stderr);
	}
	if (num_tck > 0) {
		struct timeval tv1, tv2;
		gettimeofday(&tv1, NULL);
		write_port(u->portHandle, JTAG_PULSE_TCK_DELAY);
		write_port(u->portHandle, tms);
		write_port(u->portHandle, num_tck);

		gettimeofday(&tv2, NULL);
		if (tv2.tv_sec > tv1.tv_sec) {
			usecs -= (1000000 - tv1.tv_usec) + (tv2.tv_sec - tv1.tv_sec - 1) * 1000000;
			tv1.tv_usec = 0;
		}
		usecs -= tv2.tv_usec - tv1.tv_usec;
		if (u->verbose >= 3) {
			fprintf(stderr, "[DELAY_AFTER_TCK:%ld]\n", usecs > 0 ? usecs : 0);
			fflush(stderr);
		}
	}
	if (usecs > 0) {
		usleep(usecs);
	}
}

static int h_getbyte(struct libxsvf_host *h)
{
	struct udata_s *u = h->user_data;
	return fgetc(u->f);
}

static int h_pulse_tck(struct libxsvf_host *h, int tms, int tdi, int tdo, int rmask, int sync)
{
	struct udata_s *u = h->user_data;
	uint8_t data = 0;
	if (tms) data |= 1;
	if (tdi) data |= (1<<1);
	write_port(u->portHandle, JTAG_PULSE_TCK);
	write_port(u->portHandle, data);
	int line_tdo = read_port(u->portHandle);
	int rc = line_tdo >= 0 ? line_tdo : 0;

	if (tdo >= 0 && line_tdo >= 0) {
		if (tdo != line_tdo)
			rc = -1;
	}

	if (u->verbose >= 4) {
		fprintf(stderr, "[TMS:%d, TDI:%d, TDO_ARG:%d, TDO_LINE:%d, RMASK:%d, RC:%d]\n", tms, tdi, tdo, line_tdo, rmask, rc);
	}

	return rc;
}


static int h_pulse_tck_multi(struct libxsvf_host *h, unsigned char* data, unsigned char count)
{
	struct udata_s *u = h->user_data;
	write_port(u->portHandle, JTAG_PULSE_TCK_MULTI);
	write_port(u->portHandle, count);
	int i;
	for (i = 0; i < count; i++)
	{
		write_port(u->portHandle, data[i]);
	}
	if (u->verbose >= 4) {
		fprintf(stderr, "[MULTI TCK: %d bits]\n", count);
		fprintf(stderr, "[TMS:%d, TDI:%d, TDO:%d]\n", data[i]&1, (data[i]>>1)&1 ? (data[i]>>2)&1 : -1, (data[i]>>3)&1 ? (data[i]>>4)&1 : -1);
	}
	int result = read_port(u->portHandle);
	if (!result) return -1;
	return 0;
}

static void h_pulse_sck(struct libxsvf_host *h)
{
	struct udata_s *u = h->user_data;
	if (u->verbose >= 4) {
		fprintf(stderr, "[SCK]\n");
	}
	write_port(u->portHandle, JTAG_PULSE_SCK);
}

static void h_set_trst(struct libxsvf_host *h, int v)
{
	// Maybe I'll support it later
	struct udata_s *u = h->user_data;
	if (u->verbose >= 4) {
		fprintf(stderr, "[TRST:%d]\n", v);
	}
}

static int h_set_frequency(struct libxsvf_host *h, int v)
{
	fprintf(stderr, "WARNING: Setting JTAG clock frequency to %d ignored!\n", v);
	return 0;
}

static void h_report_tapstate(struct libxsvf_host *h)
{
	struct udata_s *u = h->user_data;
	if (u->verbose >= 3) {
		fprintf(stderr, "[%s]\n", libxsvf_state2str(h->tap_state));
	}
}

static void h_report_device(struct libxsvf_host *h, unsigned long idcode)
{
	printf("idcode=0x%08lx, revision=0x%01lx, part=0x%04lx, manufactor=0x%03lx\n", idcode,
			(idcode >> 28) & 0xf, (idcode >> 12) & 0xffff, (idcode >> 1) & 0x7ff);
}

static void h_report_status(struct libxsvf_host *h, const char *message)
{
	struct udata_s *u = h->user_data;
	if (u->verbose >= 2) {
		fprintf(stderr, "[STATUS] %s\n", message);
	}
}

static void h_report_error(struct libxsvf_host *h, const char *file, int line, const char *message)
{
	fprintf(stderr, "[%s:%d] %s\n", file, line, message);
}

static void *h_realloc(struct libxsvf_host *h, void *ptr, int size, enum libxsvf_mem which)
{
	struct udata_s *u = h->user_data;
	if (u->verbose >= 3) {
		fprintf(stderr, "[REALLOC:%s:%d]\n", libxsvf_mem2str(which), size);
	}
	return realloc(ptr, size);
}

static struct udata_s u;

static struct libxsvf_host h = {
	.udelay = h_udelay,
	.setup = h_setup,
	.shutdown = h_shutdown,
	.getbyte = h_getbyte,
	.pulse_tck = h_pulse_tck,
	.pulse_tck_multi = h_pulse_tck_multi,
	.pulse_sck = h_pulse_sck,
	.set_trst = h_set_trst,
	.set_frequency = h_set_frequency,
	.report_tapstate = h_report_tapstate,
	.report_device = h_report_device,
	.report_status = h_report_status,
	.report_error = h_report_error,
	.realloc = h_realloc,
	.user_data = &u
};

const char *progname;

static void copyleft()
{
	static int already_printed = 0;
	if (already_printed)
		return;
	fprintf(stderr, "xsvftool-clujtag\n");
	fprintf(stderr, "Copyright (C) 2009  RIEGL Research ForschungsGmbH\n");
	fprintf(stderr, "Copyright (C) 2009  Clifford Wolf <clifford@clifford.at>\n");
	fprintf(stderr, "Copyright (C) 2014  Cluster <clusterrr@clusterrr.com>\n");
	fprintf(stderr, "Lib(X)SVF is free software licensed under the ISC license.\n");
	already_printed = 1;
}

static void help()
{
	copyleft();
	fprintf(stderr, "\n");
	fprintf(stderr, "Usage: %s [ -v ... ] -p portname { -s svf-file | -x xsvf-file | -c }\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "   -p portname\n");
	fprintf(stderr, "          Use then specified COM port (default is COM14)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "   -v, -vv, -vvv, -vvvv\n");
	fprintf(stderr, "          Verbose, more verbose and even more verbose\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "   -s svf-file\n");
	fprintf(stderr, "          Play the specified SVF file\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "   -x xsvf-file\n");
	fprintf(stderr, "          Play the specified XSVF file\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "   -c\n");
	fprintf(stderr, "          List devices in JTAG chain\n");
	fprintf(stderr, "\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int rc = 0;
	int gotaction = 0;
	int opt;

	strcpy(u.comPort, "COM14");
	u.portHandle = 0;

	progname = argc >= 1 ? argv[0] : "xvsftool";
	while ((opt = getopt(argc, argv, "vp:x:s:c")) != -1)
	{
		switch (opt)
		{
		case 'v':
			copyleft();
			u.verbose++;
			break;
		case 'p':
			strncpy(u.comPort, optarg, sizeof(u.comPort)-1);
			u.comPort[sizeof(u.comPort)-1] = 0;
			break;
		case 'x':
		case 's':
			gotaction = 1;
			if (u.verbose)
				fprintf(stderr, "Playing %s file `%s'.\n", opt == 's' ? "SVF" : "XSVF", optarg);
			if (!strcmp(optarg, "-"))
				u.f = stdin;
			else
				u.f = fopen(optarg, "rb");
			if (u.f == NULL) {
				fprintf(stderr, "Can't open %s file `%s': %s\n", opt == 's' ? "SVF" : "XSVF", optarg, strerror(errno));
				rc = 1;
				break;
			}
			if (libxsvf_play(&h, opt == 's' ? LIBXSVF_MODE_SVF : LIBXSVF_MODE_XSVF) < 0) {
				fprintf(stderr, "Error while playing %s file `%s'.\n", opt == 's' ? "SVF" : "XSVF", optarg);
				rc = 1;
			}
			if (strcmp(optarg, "-"))
				fclose(u.f);
			break;
		case 'c':
			gotaction = 1;
			if (libxsvf_play(&h, LIBXSVF_MODE_SCAN) < 0) {
				fprintf(stderr, "Error while scanning JTAG chain.\n");
				rc = 1;
			}
			break;
		default:
			help();
			break;
		}
	}

	if (!gotaction)
		help();

	if (u.verbose) {
		if (rc == 0) {
			fprintf(stderr, "Done!\n");
		} else {
			fprintf(stderr, "Finished with errors!\n");
		}
	}


	exit(rc);
}

