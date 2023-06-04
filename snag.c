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
#define DEFAULT_FPS 50
#define DEBUG_INT(x) printf( #x " at line %d; result: %d\n", __LINE__, x)
#define DEBUG_C(x) printf( #x " at line %d; result: %c\n", __LINE__, x)
#define DEBUG_STR(x) printf( #x " at line %d; result: %c\n", __LINE__, x)

//-------------------------------------------------------------------------

volatile bool run = true;

//-------------------------------------------------------------------------

void
printUsage(
	FILE *fp,
	const char *name)
{
	fprintf(fp, "\n");
	fprintf(fp, "Usage: %s <options>\n", name);
	fprintf(fp, "\n");
	fprintf(fp, "    --daemon - start in the background as a daemon\n");
	fprintf(fp, "    --device <device> - framebuffer device");
	fprintf(fp, " (default %s)\n", DEFAULT_DEVICE);
	fprintf(fp, "    --display <number> - Raspberry Pi display number");
	fprintf(fp, " (default %d)\n", DEFAULT_DISPLAY_NUMBER);
	fprintf(fp, "    --fps <fps> - set desired frames per second");
	fprintf(fp, " (default %d frames per second)\n", DEFAULT_FPS);
	fprintf(fp, "    --copyrect - copy only a rectangle the same size as the dest framebuffer\n");
	fprintf(fp, "    --rectx <x> - copy rectangle from source fb at <x> in copyrect mode");
	fprintf(fp, " (default 0)\n");
	fprintf(fp, "    --recty <y> - copy rectangle from source fb at <y> in copyrect mode");
	fprintf(fp, " (default 0)\n");
	fprintf(fp, "    --pidfile <pidfile> - create and lock PID file");
	fprintf(fp, " (if being run as a daemon)\n");
	fprintf(fp, "    --once - copy only one time, then exit\n");
	fprintf(fp, "    --help - print usage and exit\n");
	fprintf(fp, "\n");
}

//-------------------------------------------------------------------------

static void
signalHandler(
	int signalNumber)
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

int
main(
	int argc,
	char *argv[])
{
	const char *program = basename(argv[0]);

	int fps = DEFAULT_FPS;
	suseconds_t frameDuration =  1000000 / fps;
	bool isDaemon = false;
	bool copyRect = false;
	bool once = false;
	uint16_t copyRectX = 0;
	uint16_t copyRectY = 0;
	uint32_t displayNumber = DEFAULT_DISPLAY_NUMBER;
	const char *pidfile = NULL;
	const char *device = DEFAULT_DEVICE;

	//---------------------------------------------------------------------

	static const char *sopts = "df:hn:p:D:";
	static struct option lopts[] = 
	{
		{ "daemon", no_argument, NULL, 'd' },
		{ "fps", required_argument, NULL, 'f' },
		{ "help", no_argument, NULL, 'h' },
		{ "display", required_argument, NULL, 'n' },
		{ "pidfile", required_argument, NULL, 'p' },
		{ "device", required_argument, NULL, 'D' },
		{ "copyrect", no_argument, NULL, 'r' },
		{ "rectx", required_argument, NULL, 'x' },
		{ "recty", required_argument, NULL, 'y' },
		{ "once", no_argument, NULL, 'o' },
		{ NULL, no_argument, NULL, 0 }
	};

	int opt = 0;

	while ((opt = getopt_long(argc, argv, sopts, lopts, NULL)) != -1)
	{
		switch (opt)
		{
		case 'x':
			copyRectX = atoi(optarg);
			break;

		case 'y':
			copyRectY = atoi(optarg);
			break;

		case 'r':
			copyRect = true;
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

	int fbfd = open(device, O_RDWR);

	if (fbfd == -1)
	{
		perrorLog(isDaemon, program, "cannot open framebuffer device");

		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	struct fb_fix_screeninfo finfo;

	if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1)
	{
		perrorLog(isDaemon,
				  program,
				  "cannot get framebuffer fixed information");

		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	struct fb_var_screeninfo vinfo;

	if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1)
	{
		perrorLog(isDaemon,
				  program,
				  "cannot get framebuffer variable information");

		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	//---------------------------------------------------------------------

	if ((vinfo.xres * 2) != finfo.line_length)
	{
		perrorLog(isDaemon,
				  program,
				  "assumption failed ... framebuffer lines are padded");

		//exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	if ((vinfo.xres % 16) != 0)
	{
		perrorLog(isDaemon,
				  program,
				  "framebuffer width must be a multiple of 16");

		//exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	if (vinfo.bits_per_pixel != 16)
	{
		perrorLog(isDaemon,
				  program,
				  "framebuffer is not 16 bits per pixel");

		//exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}


	if (copyRectX > (info.width - vinfo.xres))
	{
		char s[80];
		snprintf(s, 80, "rectx must be between 0 and %d for the configured framebuffers", info.width - vinfo.xres);
		perrorLog(isDaemon,
				  program,
				  s);

		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	if (copyRectY > (info.height - vinfo.yres))
	{
		char s[80];
		snprintf(s, 80, "recty must be between 0 and %d for the configured framebuffers", info.height - vinfo.yres);
		perrorLog(isDaemon,
				  program,
				  s);

		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	//---------------------------------------------------------------------

	DEBUG_INT(finfo.smem_len);
	int chunks = finfo.smem_len/8;
	uint8_t *fbp = mmap(0,
						 chunks,
						 PROT_READ | PROT_WRITE,
						 MAP_SHARED,
						 fbfd,
						 0);

	if (fbp == MAP_FAILED)
	{
		perrorLog(isDaemon, program, "cannot map framebuffer into memory");

		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	memset(fbp, 0, chunks);

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
	
	DEBUG_INT(len);
	DEBUG_INT(finfo.smem_len);

	uint16_t *backCopyP = malloc(len);
	uint16_t *frontCopyP = malloc(len);

	uint32_t line_len = copyRect ? (info.width * 2) : finfo.line_length;

	if ((backCopyP == NULL) || (frontCopyP == NULL))
	{
		perrorLog(isDaemon, program, "cannot allocate offscreen buffers");

		exitAndRemovePidFile(EXIT_FAILURE, pfh);
	}

	memset(backCopyP, 1, finfo.line_length * vinfo.yres);
	
	DEBUG_INT(finfo.line_length*vinfo.yres);

	//---------------------------------------------------------------------

	messageLog(isDaemon,
			   program,
			   LOG_INFO,
			   "raspi2fb normal scaling mode, copying from source fb[%dx%d] to dest fb [%dx%d]",
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
	DEBUG_INT(vinfo.yres);

	while (run)
	{
		gettimeofday(&start_time, NULL);

		//-----------------------------------------------------------------

		vc_dispmanx_snapshot(display, resourceHandle, 0);

		vc_dispmanx_resource_read_data(resourceHandle,
									   &rect,
									   frontCopyP,
									   line_len*2);  // because source is 16 bit 
		
		// normal scaled copy mode
		uint8_t *fbIter = fbp;
		uint16_t *frontCopyIter = frontCopyP;
		uint16_t *backCopyIter = backCopyP;

		uint32_t pixel;
		uint16_t fb_chunk; // 8 bits of fb1 per chunk
		uint8_t fb_chunk_bit;
		
		//char buffer_chunk[16];
		char buffer_chunk[8];
		bool chunk_changed = false;
		
		for (fb_chunk = 0; fb_chunk < 12000; fb_chunk++)
		{
			// copy data chunk _from_ framebuffer 
			memcpy(buffer_chunk, fbp + (fb_chunk * 8), 8);
			
			chunk_changed = false;
			
			for (fb_chunk_bit = 0; fb_chunk_bit < 8; fb_chunk_bit++)
			{	
				if (*frontCopyIter != *backCopyIter)
				{	
					// extract new rgb data for current pixel and convert to grayscale
					uint8_t red = ((*frontCopyIter >> 8) & 0xF8);
					uint8_t green = ((*frontCopyIter >> 3) & 0xFC);
					uint8_t blue = ((*frontCopyIter << 3) & 0xF8);					
					//int grayscale = (int)(red * 0.299 + green * 0.587 + blue * 0.114);
					uint8_t grayscale = (int)((red + green + blue)/3);
					
					// Ensure grayscale value is within the range [0, 255]
					grayscale = grayscale > 255 ? 255 : grayscale; 
					
					// bits to compare
					uint8_t newbit = 1;
					uint8_t oldbit = buffer_chunk[fb_chunk_bit];
					
					if (grayscale < 140)
					{
						newbit = 0;
					}
					
					if (newbit != oldbit)
					{
						memcpy(fbp + (fb_chunk * 8) + fb_chunk_bit, &newbit, 1);
					}
				}
				
				++frontCopyIter;
				++backCopyIter;
				++pixel;
			}
			
			++fbIter;
		}

		uint16_t *tmp = backCopyP;
		backCopyP = frontCopyP;
		frontCopyP = tmp;
			
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

	free(frontCopyP);
	free(backCopyP);

	memset(fbp, 0, chunks);

	munmap(fbp, chunks);
	close(fbfd);

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
