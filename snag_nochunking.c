//-------------------------------------------------------------------------
//
// The MIT License (MIT)
//
// Copyright (c) 2015 Andrew Duncan
// Copyright (c) 2023 TheMediocritist
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


// define bayer dither patterns
const int BAYER2X2[2][2] =	
{	//	2x2 Bayer Dithering Matrix. Color levels: 5
	{  51, 206 },
	{ 153, 102 }
};

const int BAYER3X3[3][3] =	
{	//	3x3 Bayer Dithering Matrix. Color levels: 10
	//{ 181, 231, 131 },
	//{  50,  25, 100 },
	//{ 156,  75, 206 }
	{  75, 150, 225 },
	{  50, 125, 200 },
	{  25, 100, 175 }
};

const int BAYER4X4[4][4] =	
{	//	4x4 Bayer Dithering Matrix. Color levels: 17
	{  15, 195,  60, 240 },
	{ 135,  75, 180, 120 },
	{  45, 225,  30, 210 },
	{ 165, 105, 150,  90 }
};

const int BAYER8X8[8][8] =	
{	//	8x8 Bayer Dithering Matrix. Color levels: 65
	{   0, 128,  32, 160,   8, 136,  40, 168 },
	{ 192,  64, 224,  96, 200,  72, 232, 104 },
	{  48, 176,  16, 144,  56, 184,  24, 152 },
	{ 240, 112, 208,  80, 248, 120, 216,  88 },
	{  12, 140,  44, 172,   4, 132,  36, 164 },
	{ 204,  76, 236, 108, 196,  68, 228, 100 },
	{  60, 188,  28, 156,  52, 180,  20, 148 },
	{ 252, 124, 220,  92, 244, 116, 212,  84 }
};

const int BAYER16X16[16][16] =	
{	//	16x16 Bayer Dithering Matrix.  Color levels: 256
	{   0, 191,  48, 239,  12, 203,  60, 251,   3, 194,  51, 242,  15, 206,  63, 254 }, 
	{ 127,  64, 175, 112, 139,  76, 187, 124, 130,  67, 178, 115, 142,  79, 190, 127 },
	{  32, 223,  16, 207,  44, 235,  28, 219,  35, 226,  19, 210,  47, 238,  31, 222 },
	{ 159,  96, 143,  80, 171, 108, 155,  92, 162,  99, 146,  83, 174, 111, 158,  95 },
	{   8, 199,  56, 247,   4, 195,  52, 243,  11, 202,  59, 250,   7, 198,  55, 246 },
	{ 135,  72, 183, 120, 131,  68, 179, 116, 138,  75, 186, 123, 134,  71, 182, 119 },
	{  40, 231,  24, 215,  36, 227,  20, 211,  43, 234,  27, 218,  39, 230,  23, 214 },
	{ 167, 104, 151,  88, 163, 100, 147,  84, 170, 107, 154,  91, 166, 103, 150,  87 },
	{   2, 193,  50, 241,  14, 205,  62, 253,   1, 192,  49, 240,  13, 204,  61, 252 },
	{ 129,  66, 177, 114, 141,  78, 189, 126, 128,  65, 176, 113, 140,  77, 188, 125 },
	{  34, 225,  18, 209,  46, 237,  30, 221,  33, 224,  17, 208,  45, 236,  29, 220 },
	{ 161,  98, 145,  82, 173, 110, 157,  94, 160,  97, 144,  81, 172, 109, 156,  93 },
	{  10, 201,  58, 249,   6, 197,  54, 245,   9, 200,  57, 248,   5, 196,  53, 244 },
	{ 137,  74, 185, 122, 133,  70, 181, 118, 136,  73, 184, 121, 132,  69, 180, 117 },
	{  42, 233,  26, 217,  38, 229,  22, 213,  41, 232,  25, 216,  37, 228,  21, 212 },
	{ 169, 106, 153,  90, 165, 102, 149,  86, 168, 105, 152,  89, 164, 101, 148,  85 }
};

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
	fprintf(fp, "  --dither <type>       Set dither method (none/2x2/4x4/8x8/16x16) (default %s)\n", DEFAULT_DITHER_METHOD);	
	fprintf(fp, "  --pidfile <pidfile>   Create and lock PID file (if being run as a daemon)\n");	
	fprintf(fp, "  --once                Copy only one time, then exit\n");	
	fprintf(fp, "  --help                Print usage and exit\n");
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


void convertPixel(uint16_t pxl, uint16_t *red, uint16_t *green, uint16_t *blue) {
	// Extract red, green, and blue components
	*red = (pxl >> 11) & 0x1F;
	*green = (pxl >> 5) & 0x3F;
	*blue = pxl & 0x1F;

	// Convert 5-bit and 6-bit values to 8-bit values
	*red = (*red << 3) | (*red >> 2);
	*green = (*green << 2) | (*green >> 4);
	*blue = (*blue << 3) | (*blue >> 2);
	
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
		{ "dither", required_argument, NULL, 'b'},
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
			case 'b':	
				dithermethod = optarg;	
				break;
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

	bcm_host_init();

	DISPMANX_DISPLAY_HANDLE_T display = vc_dispmanx_display_open(displayNumber);

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

	if ((vinfo.xres % 16) != 0)
	{
		perrorLog(isDaemon,
				  program,
				  "framebuffer width must be a multiple of 16");
		//exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	//---------------------------------------------------------------------

	DEBUG_INT(finfo.smem_len);
	int chunks = finfo.smem_len/8;
	uint8_t *fb1_data = mmap(0,
 						chunks,
 						PROT_READ | PROT_WRITE,
 						MAP_SHARED,
 						fb1,
 						0);

	if (fb1_data == MAP_FAILED)
	{
		perrorLog(isDaemon, program, "cannot map framebuffer into memory");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	memset(fb1_data, 0, chunks);

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

	uint32_t len = finfo.smem_len;

	uint16_t *old_data = malloc(len);
	uint16_t *new_data = malloc(len);

	uint32_t line_len = finfo.line_length;

	if ((old_data == NULL) || (new_data == NULL))
	{
		perrorLog(isDaemon, program, "cannot allocate offscreen buffers");
		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	memset(old_data, 1, finfo.line_length * vinfo.yres);

	//---------------------------------------------------------------------

	messageLog(isDaemon,
			   program,
			   LOG_INFO,
			   "snag normal scaling mode, copying from source fb[%dx%d] to dest fb [%dx%d]",
			   info.width,
			   info.height,
			   vinfo.xres,
			   vinfo.yres);

	//---------------------------------------------------------------------

	struct timeval start_time;
	struct timeval end_time;
	struct timeval elapsed_time;

	//---------------------------------------------------------------------

	while (run)
	{
		gettimeofday(&start_time, NULL);

		//-----------------------------------------------------------------
		
		// grab HDMI display data and put it in new_data
		vc_dispmanx_snapshot(display, resourceHandle, 0);
		vc_dispmanx_resource_read_data(resourceHandle,
									   &rect,
									   new_data,
									   line_len * 2);  // * 2 because source is 16 bit 
		
		// get the first 8 pixels of fb1 and the first pixel of new_data & old_data
		uint16_t *new_pixel = new_data;
		uint16_t *old_pixel = old_data;
		
		// loop through pixels, compare new_pixel to old_pixel, update fb1_data if changed
		for (uint32_t pixel = 0; pixel < 96000; pixel++)
		{
			if (*new_pixel != *old_pixel)
			{	
				// extract new rgb data for current pixel and convert to grayscale
				uint16_t red, green, blue;
				convertPixel(*new_pixel, &red, &green, &blue);
				
			
				uint16_t grayscale = (0.299 * red + 0.587 * green + 0.114 * blue);
				
				//grayscale *= 1.5; // because there's a bug somewhere
				
				// Ensure grayscale value is within the range [0, 255]
				grayscale = grayscale > 255 ? 255 : grayscale; 
				
				// DEBUG
				// if (pixel < 8)
				// {
				// 	DEBUG_INT(*new_pixel);
				// 	DEBUG_INT(red);
				// 	DEBUG_INT(green);
				// 	DEBUG_INT(blue);
				// 	DEBUG_INT(grayscale);
				// }
				
				// 1 bit pixels to compare
				uint8_t newbit = 1;
				char oldbit = fb1_data[pixel];
				
				// if (pixel < 8)
				// {
				// 	DEBUG_INT(newbit);
				// 	DEBUG_INT(oldbit);
				// }

				// pixel location
				uint8_t col = pixel % 400;
				uint8_t row = pixel / 400;
				
				if (strcmp(dithermethod, "2x2") == 0){
					newbit = (grayscale < BAYER2X2[row % 2][col % 2] ? 0 : 1);
				}
				else if (strcmp(dithermethod, "3x3") == 0){
					newbit = (grayscale < BAYER3X3[row % 3][col % 3] ? 0 : 1);
				}
				else if (strcmp(dithermethod, "4x4") == 0){
					newbit = (grayscale < BAYER4X4[row % 4][col % 4] ? 0 : 1);
				}
				else if (strcmp(dithermethod, "8x8") == 0){
					newbit = (grayscale < BAYER8X8[row % 8][col % 8] ? 0 : 1);
				}
				else if (strcmp(dithermethod, "16x16") == 0){
					newbit = (grayscale < BAYER16X16[row % 16][col % 16] ? 0 : 1);
				}
				else{
					newbit = (grayscale < 140 ? 0 : 1);
				}
				
				if (newbit != oldbit){
					//fb1_data[pixel] = (char) newbit;
					memcpy(fb1_data + pixel, &newbit, 1);
				}
			}
			
			++new_pixel;
			++old_pixel;
		}

		uint16_t *tmp = old_data;
		old_data = new_data;
		new_data = tmp;
			
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
	memset(fb1_data, 0, 96000);
	munmap(fb1_data, 96000);
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
