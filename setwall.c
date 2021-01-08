#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdbool.h>
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

static void fatal(char *str)
{
	fprintf(stderr, "FATAL ERROR: %s\n", str);
	exit(1);
}

BOOL displayEnumProc(HMONITOR monitor, HDC hdc, LPRECT rect, LPARAM userdata)
{
	(void)monitor; // unused
	(void)hdc; // unused

	mondata_t *mondata = (mondata_t*)userdata;
	mon_t *mon = &mondata->mons[mondata->count];

	memcpy(&mon->pos, rect, sizeof(RECT));
	mon->w = mon->pos.right - mon->pos.left;
	mon->h = mon->pos.bottom - mon->pos.top;

	mondata->count++;

	return TRUE;
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
	if (!EnumDisplayMonitors(NULL, NULL, displayEnumProc, (LPARAM)&mondata))
		fatal("failed to enumerate displays");

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
		char tmpDir[MAX_PATH];
		GetTempPathA(MAX_PATH, tmpDir);
		static char tmpFileName[MAX_PATH];
		GetTempFileNameA(tmpDir, "wallpaper", 0, tmpFileName);
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
	const int genimgW = boundingBox.right - boundingBox.left;
	const int genimgH = boundingBox.bottom - boundingBox.top;
	unsigned char *genimgData = calloc(genimgW * genimgH, IMG_PIXELSIZE);
	if (!genimgData)
		fatal("failed to alloc result image");

	// Load images
	for (int i = 0; i < mondata.count; i++) {
		const char *path = argv[optind + i];
		mon_t *mon = &mondata.mons[i];

		// Load
		unsigned char *imgData;
		int imgW, imgH;
		imgData = stbi_load(path, &imgW, &imgH, NULL, IMG_PIXELSIZE);
		if (!imgData) {
			fprintf(stderr, "Failed to load image '%s'\n", path);
			return 1;
		}

		// Resize
		if (imgW != mon->w || imgH != mon->h) {
			unsigned char *resized = calloc(mon->w * mon->h, IMG_PIXELSIZE);
			if (!resized)
				fatal("failed to alloc resize space");
			if (!stbir_resize_uint8(imgData, imgW, imgH, 0,
					resized, mon->w, mon->h, 0, IMG_PIXELSIZE))
				fatal("failed to resize image");
			stbi_image_free(imgData);
			imgData = resized;
			imgW = mon->w;
			imgH = mon->h;
		}

		// Place in genimg
		for (int y = 0; y < mon->h; y++) {
			memcpy(genimgData + ((mon->pos.top - boundingBox.top + y) * genimgW
							+ mon->pos.left - boundingBox.left) * IMG_PIXELSIZE,
					imgData + y * mon->w * IMG_PIXELSIZE,
					mon->w * IMG_PIXELSIZE);
		}

		stbi_image_free(imgData);
	}

	// Write out
	if (!stbi_write_png(outputAbsFile, genimgW, genimgH, IMG_PIXELSIZE, genimgData, 0))
		fatal("failed to write output image");

	if (setWall) {
		// Set wall properties in registry
		HKEY regKey;
		if (RegOpenKeyA(HKEY_CURRENT_USER, "Control Panel\\Desktop", &regKey))
			fatal("failed to open desktop regkey, make sure you have access");
		int tmp = 1;
		if (RegSetValueExA(regKey, "WallpaperStyle", 0, REG_DWORD, (LPBYTE)&tmp, sizeof(DWORD)))
			fatal("failed to set wallpaper style");
		if (RegSetValueExA(regKey, "TileWallpaper", 0, REG_DWORD, (LPBYTE)&tmp, sizeof(DWORD)))
			fatal("failed to set wallpaper tiling");
		RegCloseKey(regKey);

		// Set wall
		SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, outputAbsFile,
				SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE);
	}

	return 0;
}
