//-------------------------------------------------------------------------
//
// The MIT License (MIT)
//
// Copyright (c) 2015 Andrew Duncan
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//-------------------------------------------------------------------------

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <bsd/libutil.h>

#include <linux/fb.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#include "bcm_host.h"
#pragma GCC diagnostic pop

#include "syslogUtilities.h"

//-------------------------------------------------------------------------

#define DEFAULT_DEVICE "/dev/fb1"
#define DEFAULT_DISPLAY_NUMBER 0
#define DEFAULT_FPS 30
#define DEFAULT_DITHER_METHOD "4x4"
#define DEBUG_INT(x) printf( #x " at line %d; result: %d\n", __LINE__, x)
#define DEBUG_C(x) printf( #x " at line %d; result: %c\n", __LINE__, x)
#define DEBUG_STR(x) printf( #x " at line %d; result: %c\n", __LINE__, x)

//-------------------------------------------------------------------------

volatile bool run = true;

//-------------------------------------------------------------------------

void printUsage( FILE *fp, const char *name)
{
	fprintf(fp, "\n");
	fprintf(fp, "Usage: %s <options>\n", name);
	fprintf(fp, "\n");
	fprintf(fp, "Options:\n");
	fprintf(fp, "  --daemon              Start in the background as a daemon\n");
	fprintf(fp, "  --device <device>     Framebuffer device (default %s)\n", DEFAULT_DEVICE);
	fprintf(fp, "  --display <number>    Raspberry Pi display number (default %d)\n", DEFAULT_DISPLAY_NUMBER);
	fprintf(fp, "  --fps <fps>           Set desired frames per second (default %d)\n", DEFAULT_FPS);
	fprintf(fp, "  --dithertype <type>   Set dither method (2x2/4x4/8x8/16x16) (default %s)\n", DEFAULT_DITHER_METHOD);
	fprintf(fp, "  --pidfile <pidfile>   Create and lock PID file (if being run as a daemon)\n");
	fprintf(fp, "  --once                Copy only one time, then exit\n");
	fprintf(fp, "  --help                Print usage and exit\n");
	fprintf(fp, "\n");
}

//-------------------------------------------------------------------------

static void signalHandler(int signalNumber)
{
	switch (signalNumber)
	{
	case SIGINT:
	case SIGTERM:

		run = false;
		break;
	};
}

//-------------------------------------------------------------------------

int main(int argc, char *argv[])
{
	const char *program = basename(argv[0]);

	int fps = DEFAULT_FPS;
	suseconds_t frameDuration =  1000000 / fps;
	bool isDaemon = false;
	bool once = false;
	uint32_t displayNumber = DEFAULT_DISPLAY_NUMBER;
	char *dithermethod = DEFAULT_DITHER_METHOD;
	const char *pidfile = NULL;
	const char *device = DEFAULT_DEVICE;


	//---------------------------------------------------------------------

	static const char *sopts = "df:hn:b:p:D:o";
	static struct option lopts[] = 
	{
		{ "daemon", no_argument, NULL, 'd' },
		{ "fps", required_argument, NULL, 'f' },
		{ "help", no_argument, NULL, 'h' },
		{ "display", required_argument, NULL, 'n' },
		{ "dithertype", required_argument, NULL, 'b'},
		{ "pidfile", required_argument, NULL, 'p' },
		{ "device", required_argument, NULL, 'D' },
		{ "once", no_argument, NULL, 'o' },
		{ NULL, no_argument, NULL, 0 }
	};

	int opt = 0;

	while ((opt = getopt_long(argc, argv, sopts, lopts, NULL)) != -1)
	{
		switch (opt)
		{
			case 'd':
				isDaemon = true;
				break;
			case 'f':
				fps = atoi(optarg);
				if (fps > 0)
				{
					frameDuration = 1000000 / fps;
				}
				else
				{
					fps = 1000000 / frameDuration;
				}
				break;
			case 'h':
				printUsage(stdout, program);
				exit(EXIT_SUCCESS);
				break;
			case 'b':
				dithermethod = optarg;
				break;
			case 'n':
				displayNumber = atoi(optarg);
				break;
			case 'p':
				pidfile = optarg;
				break;
			case 'o':
				once = true;
				break;
			case 'D':
				device = optarg;
				break;
			default:
				printUsage(stderr, program);
				exit(EXIT_FAILURE);
				break;
		}
	}

	//---------------------------------------------------------------------

	struct pidfh *pfh = NULL;

	if (isDaemon)
	{
		if (pidfile != NULL)
		{
			pid_t otherpid;
			pfh = pidfile_open(pidfile, 0600, &otherpid);

			if (pfh == NULL)
			{
				fprintf(stderr,
						"%s is already running %jd\n",
						program,
						(intmax_t)otherpid);
				exit(EXIT_FAILURE);
			}
		}

		if (daemon(0, 0) == -1)
		{
			fprintf(stderr, "Cannot daemonize\n");
			exitAndRemovePidFile(EXIT_FAILURE, pfh);
		}

		if (pfh)
		{
			pidfile_write(pfh);
		}

		openlog(program, LOG_PID, LOG_USER);
	}

	//---------------------------------------------------------------------

	if (signal(SIGINT, signalHandler) == SIG_ERR)
	{
		perrorLog(isDaemon, program, "installing SIGINT signal handler");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	//---------------------------------------------------------------------

	if (signal(SIGTERM, signalHandler) == SIG_ERR)
	{
		perrorLog(isDaemon, program, "installing SIGTERM signal handler");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}
	
	//---------------------------------------------------------------------
	
	const int BAYER2X2[2][2] = { // dithers 4 different patterns plus white
		{1, 3},
		{4, 2}
	};
	
	const int BAYER4X4[4][4] = { // dithers 16 different patterns plus white
		{ 1,  9,  4, 12},
		{13,  5, 16,  8},
		{ 3, 11,  2, 10},
		{15,  7, 14,  6},
	};
	
	const int BAYER8X8[8][8] = { // dithers 64 different patterns plus white
		{1,  33,  9, 41,  3, 35, 11, 43},
		{49, 17, 57, 25, 51, 19, 59, 27},
		{13, 45,  5, 37, 15, 47,  7, 39},
		{61, 29, 53, 21, 63, 31, 55, 23},
		{ 4, 36, 12, 44,  2, 34, 10, 42},
		{52, 20, 60, 28, 50, 18, 58, 26},
		{16, 48,  8, 40, 14, 46,  6, 38},
		{64, 32, 56, 24, 62, 30, 54, 22}
	};
	
	const int BAYER16X16[16][16] = {   // dithers 256 different patterns plus white
		{  1, 192,  49, 240,  13, 204,  61, 252,   4, 195,  52, 243,  16, 207,  64, 255},
		{128,  65, 176, 113, 140,  77, 188, 125, 131,  68, 179, 116, 143,  80, 191, 128},
		{ 33, 224,  17, 208,  45, 236,  29, 220,  36, 227,  20, 211,  48, 239,  32, 223},
		{160,  97, 144,  81, 172, 109, 156,  93, 163, 100, 147,  84, 175, 112, 159,  96},
		{  9, 200,  57, 248,   5, 196,  53, 244,  12, 203,  60, 251,   8, 199,  56, 247},
		{136,  73, 184, 121, 132,  69, 180, 117, 139,  76, 187, 124, 135,  72, 183, 120},
		{ 41, 232,  25, 216,  37, 228,  21, 212,  44, 235,  28, 219,  40, 231,  24, 215},
		{168, 105, 152,  89, 164, 101, 148,  85, 171, 108, 155,  92, 167, 104, 151,  88},
		{  3, 194,  51, 242,  15, 206,  63, 254,   2, 193,  50, 241,  14, 205,  62, 253},
		{130,  67, 178, 115, 142,  79, 190, 127, 129,  66, 177, 114, 141,  78, 189, 126},
		{ 35, 226,  19, 210,  47, 238,  31, 222,  34, 225,  18, 209,  46, 237,  30, 221},
		{162,  99, 146,  83, 174, 111, 158,  95, 161,  98, 145,  82, 173, 110, 157,  94},
		{ 11, 202,  59, 250,   7, 198,  55, 246,  10, 201,  58, 249,   6, 197,  54, 245},
		{138,  75, 186, 123, 134,  71, 182, 119, 137,  74, 185, 122, 133,  70, 181, 118},
		{ 43, 234,  27, 218,  39, 230,  23, 214,  42, 233,  26, 217,  38, 229,  22, 213},
		{170, 107, 154,  91, 166, 103, 150,  87, 169, 106, 153,  90, 165, 102, 149,  86}
	};
	
	//---------------------------------------------------------------------

	bcm_host_init();

	DISPMANX_DISPLAY_HANDLE_T display
		= vc_dispmanx_display_open(displayNumber);

	if (display == 0)
	{
		messageLog(isDaemon, program, LOG_ERR, "cannot open display");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	DISPMANX_MODEINFO_T info;

	if (vc_dispmanx_display_get_info(display, &info) != 0)
	{
		messageLog(isDaemon,
		   program,
		   LOG_ERR,
		   "cannot get display dimensions");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	//---------------------------------------------------------------------

	int fb1 = open(device, O_RDWR);
	if (fb1 == -1)
	{
		perrorLog(isDaemon, program, "cannot open framebuffer device");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	struct fb_fix_screeninfo finfo;
	if (ioctl(fb1, FBIOGET_FSCREENINFO, &finfo) == -1)
	{
		perrorLog(isDaemon,
				  program,
				  "cannot get framebuffer fixed information");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	struct fb_var_screeninfo vinfo;
	if (ioctl(fb1, FBIOGET_VSCREENINFO, &vinfo) == -1)
	{
		perrorLog(isDaemon,
				  program,
				  "cannot get framebuffer variable information");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	//---------------------------------------------------------------------

	uint8_t *fb1_data = mmap(0,
		 finfo.smem_len,
		 PROT_READ | PROT_WRITE,
		 MAP_SHARED,
		 fb1,
		 0);

	if (fb1_data == MAP_FAILED)
	{
		perrorLog(isDaemon, program, "cannot map framebuffer into memory");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	memset(fb1_data, 0, finfo.smem_len);

	//---------------------------------------------------------------------

	uint32_t image_ptr;

	DISPMANX_RESOURCE_HANDLE_T resourceHandle;
	VC_RECT_T rect;

	resourceHandle = vc_dispmanx_resource_create(VC_IMAGE_RGB565,
		vinfo.xres,
		vinfo.yres,
		&image_ptr);
	vc_dispmanx_rect_set(&rect, 0, 0, vinfo.xres, vinfo.yres);

	//---------------------------------------------------------------------

	// create offscreen buffers for old & new data storage
	uint32_t len = finfo.smem_len;

	uint8_t *old_data = malloc(len);
	uint16_t *new_data = malloc(len);

	uint32_t line_len = finfo.line_length;

	if ((old_data == NULL) || (new_data == NULL))
	{
		perrorLog(isDaemon, program, "cannot allocate offscreen buffers");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	memset(old_data, 0, finfo.line_length * vinfo.yres);

	//---------------------------------------------------------------------

	messageLog(isDaemon,
		program,
		LOG_INFO,
		"snag is copying from source fb[%dx%d] to dest fb [%dx%d]",
		info.width,
		info.height,
		vinfo.xres,
		vinfo.yres);

	//---------------------------------------------------------------------

	struct timeval start_time;
	struct timeval end_time;
	struct timeval elapsed_time;

	//---------------------------------------------------------------------

	// pixels = count of destination framebuffer pixels
	uint32_t pixels = vinfo.xres * vinfo.yres;
	
	DEBUG_INT(vinfo.xres);
	DEBUG_INT(vinfo.yres);
	DEBUG_INT(vinfo.bits_per_pixel);
	
	DEBUG_INT(finfo.line_length);
	DEBUG_INT(finfo.smem_len);
	DEBUG_INT(pixels);

	while (run)
	{
		gettimeofday(&start_time, NULL);
		
		//-----------------------------------------------------------------
		
		vc_dispmanx_snapshot(display, resourceHandle, 0);
		vc_dispmanx_resource_read_data(resourceHandle,
			&rect,
			new_data,
			line_len*2);  // because source is 16 bit <- but what if this isn't the case?
			
		// load pixel data 
		uint8_t *fb1_pixel = fb1_data;
		uint16_t *new_pixel = new_data;
		uint8_t *old_pixel = old_data;
		
		// just for debug - check min and max grayscale values per frame
		int mingray = 128;
		int maxgray = 128;
		
		uint32_t pixel;
		for (pixel = 0 ; pixel < pixels ; pixel++)
		{   
			uint8_t red = ((*new_pixel >> 8) & 0xF8);
			uint8_t green = ((*new_pixel >> 3) & 0xFC);
			uint8_t blue = ((*new_pixel << 3) & 0xF8);
			//int grayscale = (int)(red * 0.299 + green * 0.587 + blue * 0.114);
			int grayscale = (int)((red + green + blue)/3);
			grayscale = grayscale > 255 ? 255 : grayscale; // Ensure grayscale value is within the range [0, 255]
			
			// just for debug
			mingray = grayscale < mingray ? grayscale : mingray;
			maxgray = grayscale > maxgray ? grayscale : maxgray;
			
			// this is what we write to the framebuffer as either 0 or 255
			uint8_t onebit = 0;
			
			// get row & column values for current pixel
			uint8_t column = pixel % 400;
			uint8_t row = pixel / 400;
			
			// apply the selected dither
			if (strcmp(dithermethod, "2x2") == 0)
			{
				int colMod = column % 2;
				int rowMod = row % 2;
				onebit = ((grayscale * 5 / 255) > BAYER2X2[rowMod][colMod]) ? 255 : onebit;
			}
			else if (strcmp(dithermethod, "4x4") == 0)
			{
				int colMod = column % 4;
				int rowMod = row % 4;
				onebit = ((grayscale * 17 / 255) > BAYER4X4[rowMod][colMod]) ? 255 : onebit;
			}
			else if (strcmp(dithermethod, "8x8") == 0)
			{
				int colMod = column % 8;
				int rowMod = row % 8;
				onebit = ((grayscale * 65 / 255) > BAYER8X8[rowMod][colMod]) ? 255 : onebit;
			}
			else if (strcmp(dithermethod, "16x16") == 0)
			{
				int colMod = column % 16;
				int rowMod = row % 16;
				onebit = ((grayscale * 257 / 255) > BAYER16X16[rowMod][colMod]) ? 255 : onebit;
			}
			else
			{
				onebit = grayscale > 120 ? 255 : onebit;
			}
			
			// update framebuffer if pixel has changed
			if (onebit != *old_pixel)
			{
				*fb1_pixel = onebit;
			}
			
			// Move to the next pixel
			++fb1_pixel;
			++new_pixel;
			++old_pixel;
		}
		
		// DEBUG_INT(mingray);
		// DEBUG_INT(maxgray);
		
		uint8_t *tmp = (uint8_t*)old_data;
		old_data = (uint8_t*)new_data;
		new_data = (uint16_t*)tmp;

		//-----------------------------------------------------------------

		if (once)
		{
			messageLog(isDaemon,
					   program,
					   LOG_INFO,
					   "ran once, exiting now");
			break;
		}

		gettimeofday(&end_time, NULL);
		timersub(&end_time, &start_time, &elapsed_time);

		if (elapsed_time.tv_sec == 0)
		{
			if (elapsed_time.tv_usec < frameDuration)
			{
				usleep(frameDuration -  elapsed_time.tv_usec);
			}
		}
	}

	//---------------------------------------------------------------------

	free(new_data);
	free(old_data);

	memset(fb1_data, 0, finfo.smem_len);
	munmap(fb1_data, finfo.smem_len);

	close(fb1);

	//---------------------------------------------------------------------

	vc_dispmanx_resource_delete(resourceHandle);
	vc_dispmanx_display_close(display);

	//---------------------------------------------------------------------

	messageLog(isDaemon, program, LOG_INFO, "exiting");

	if (isDaemon)
	{
		closelog();
	}

	if (pfh)
	{
		pidfile_remove(pfh);
	}

	//---------------------------------------------------------------------

	return 0 ;
}
