#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdbool.h>
#include <assert.h>
#include <windows.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// TODO: look into using libvips for better performance
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define IMG_PIXELSIZE 3
#define MAX_MON_COUNT 16

typedef struct {
	int w, h;
	unsigned char *data;
} img_t;

typedef struct {
	RECT pos;
	int w, h;
} mon_t;

typedef struct {
	int count;
	mon_t mons[MAX_MON_COUNT];
} mondata_t;

typedef struct {
	int offsetX, offsetY, w, h;
	unsigned char *data;
} genimg_t;

typedef struct {
	const char *path;
	const mon_t *mon;
	genimg_t *target;
} mon_thread_data_t;

static BOOL displayEnumProc(HMONITOR monitor, HDC hdc, LPRECT rect, LPARAM userdata)
{
	(void)monitor; (void)hdc; // unused

	mondata_t *mondata = (mondata_t*)userdata;
	mon_t *mon = &mondata->mons[mondata->count];

	memcpy(&mon->pos, rect, sizeof(RECT));
	mon->w = mon->pos.right - mon->pos.left;
	mon->h = mon->pos.bottom - mon->pos.top;

	mondata->count++;

	return TRUE;
}

static DWORD WINAPI threadedLoadBlitImg(LPVOID param)
{
	mon_thread_data_t *data = (mon_thread_data_t*)param;

	// Load
	int imgW, imgH;
	unsigned char *imgData = stbi_load(data->path, &imgW, &imgH, NULL, IMG_PIXELSIZE);
	if (!imgData) {
		fprintf(stderr, "Failed to load image '%s'\n", data->path);
		exit(1);
	}

	// Crop to aspect ratio if needed
	// TODO: crop to center rather than top-left
	const double monAspect = (double)data->mon->w / data->mon->h;
	const double imgAspect = (double)imgW / imgH;
	if (imgAspect != monAspect) {
		if (imgAspect > monAspect) {
			// For reducing width we also need to move the image data
			const int newImgW = imgH * monAspect;
			for (int y = 0; y < imgH; y++) {
				memmove(imgData + y * newImgW * IMG_PIXELSIZE,
						imgData + y * imgW * IMG_PIXELSIZE,
						newImgW * IMG_PIXELSIZE);
			}
			imgW = newImgW;
		} else {
			imgH = imgW / monAspect;
		}
	}

	// Resize if needed
	if (imgW != data->mon->w || imgH != data->mon->h) {
		unsigned char *resized = calloc(data->mon->w * data->mon->h, IMG_PIXELSIZE);
		assert(resized);
		assert(stbir_resize_uint8(imgData, imgW, imgH, 0,
				resized, data->mon->w, data->mon->h, 0, IMG_PIXELSIZE));
		free(imgData);
		imgData = resized;
		imgW = data->mon->w;
		imgH = data->mon->h;
	}

	// Blit into target image
	for (int y = 0; y < data->mon->h; y++) {
		memcpy(data->target->data + ((data->mon->pos.top + data->target->offsetY + y) * data->target->w
						+ data->mon->pos.left + data->target->offsetX) * IMG_PIXELSIZE,
				imgData + y * data->mon->w * IMG_PIXELSIZE,
				data->mon->w * IMG_PIXELSIZE);
	}

	free(imgData);
	return 0;
}

static void help(int exitcode)
{
	fprintf(stderr,
		"USAGE:\n"
		"\tSetting wallpapers:\n"
		"\t\tsetwall.exe [OPTIONS] <wallpaper path(s)...>\n"
		"\tGetting info:\n"
		"\t\tsetwall.exe <OPTIONS...>\n"
		"OPTIONS:\n"
		"\t-h               Get help and exit\n"
		"\t-i               Get display info and exit\n"
		"\t-o <file.png>    Specify wallpaper location (defaults to a temporary file)\n"
		"\t-d               Only generate output, don't set it as wallpaper\n"
	);
	exit(exitcode);
}

int main(int argc, char **argv)
{
	// Get displays
	static mondata_t mondata = { 0 };
	assert(EnumDisplayMonitors(NULL, NULL, displayEnumProc, (LPARAM)&mondata));

	// Options
	char *outputFile = NULL;
	bool setWall = true;

	// Handle arguments
	char c;
	while ((c = getopt(argc, argv, "o:hdi")) != -1) {
		switch (c) {
		case 'o':
			outputFile = optarg;
			break;
		case 'h':
			help(0);
			break;
		case 'd':
			setWall = false;
			break;
		case 'i':
			printf("Display count: %d\n", mondata.count);
			for (int i = 0; i < mondata.count; i++) {
				const mon_t mon = mondata.mons[i];
				printf("Display %d:\n", i + 1);
				printf("\tx: %ld\n\ty: %ld\n\tw: %d\n\th: %d\n",
						mon.pos.left, mon.pos.top, mon.w, mon.h);
			}
			return 0;
		case '?':
			help(1);
			break;
		}
	}
	if (argc - optind != mondata.count) {
		fprintf(stderr, "Expected %d wallpaper paths (one per monitor), got %d!\n", mondata.count, argc - optind);
		return 1;
	}

	// Output path
	if (outputFile == NULL) {
		const char defaultName[] = "setwall-wallpaper.png";
		static char tmpFileName[MAX_PATH];
		memcpy(tmpFileName + GetTempPathA(MAX_PATH, tmpFileName), defaultName, sizeof(defaultName));
		outputFile = tmpFileName;
	}
	char outputAbsFile[MAX_PATH];
	GetFullPathNameA(outputFile, MAX_PATH, outputAbsFile, NULL);

	// Find bounding box for all displays
	RECT boundingBox = { 0 };
	for (int i = 0; i < mondata.count; i++) {
		boundingBox.left = min(boundingBox.left, mondata.mons[i].pos.left);
		boundingBox.top = min(boundingBox.top, mondata.mons[i].pos.top);
		boundingBox.right = max(boundingBox.right, mondata.mons[i].pos.right);
		boundingBox.bottom = max(boundingBox.bottom, mondata.mons[i].pos.bottom);
	}

	// Generated image
	genimg_t genimg = {
		.w = boundingBox.right - boundingBox.left,
		.h = boundingBox.bottom - boundingBox.top,
		.offsetX = -boundingBox.left,
		.offsetY = -boundingBox.top,
	};
	genimg.data = calloc(genimg.w * genimg.h, IMG_PIXELSIZE);
	assert(genimg.data);

	// Load images in thread
	static mon_thread_data_t threadDatas[MAX_MON_COUNT] = { 0 };
	static HANDLE threads[MAX_MON_COUNT] = { 0 };
	for (int i = 0; i < mondata.count; i++) {
		threadDatas[i].path = argv[optind + i];
		threadDatas[i].mon = &mondata.mons[i];
		threadDatas[i].target = &genimg;
		threads[i] = CreateThread(NULL, 0, threadedLoadBlitImg, &threadDatas[i], 0, NULL);
	}
	WaitForMultipleObjects(mondata.count, threads, TRUE, INFINITE);
	for (int i = 0; i < mondata.count; i++)
		CloseHandle(threads[i]);

	// Write out
	if (!stbi_write_png(outputAbsFile, genimg.w, genimg.h, IMG_PIXELSIZE, genimg.data, 0)) {
		fprintf(stderr, "failed to write output image '%s'\n", outputAbsFile);
		return 1;
	}

	if (setWall) {
		// Set wall properties in registry
		HKEY regKey;
		if (RegOpenKeyA(HKEY_CURRENT_USER, "Control Panel\\Desktop", &regKey)) {
			fprintf(stderr, "failed to open desktop regkey, make sure you have access\n");
			return 1;
		}
		const int tmp = 1;
		assert(!RegSetValueExA(regKey, "WallpaperStyle", 0, REG_DWORD, (LPBYTE)&tmp, sizeof(DWORD)));
		assert(!RegSetValueExA(regKey, "TileWallpaper", 0, REG_DWORD, (LPBYTE)&tmp, sizeof(DWORD)));
		RegCloseKey(regKey);

		// Set wall
		SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, outputAbsFile, SPIF_SENDCHANGE | SPIF_UPDATEINIFILE);
	}

	free(genimg.data);
	return 0;
}
