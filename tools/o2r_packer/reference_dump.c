/* Dumps the decomp's own texture decoder's (tools/mktex's libpdtex) raw
 * decoded LOD 0 pixels for one texture, for reference_compare.py.
 * Built against the decomp's libpdtex sources at test time, not copied.
 *
 * Output: five little-endian int32 (format, width, height, compression,
 * numcolours), then the pixel bytes, then -- for paletted formats only --
 * numcolours 16-bit big-endian palette entries. */
#include <stdio.h>
#include <stdlib.h>
#include "libpdtex/pdtex.h"

extern int g_TexFormatBitsPerPixel[];

int main(int argc, char **argv)
{
	struct pd_tex *tex;
	struct pd_image *image;
	int paletted;
	long numbytes;
	int header[5];
	FILE *out;

	if (argc != 3) {
		return 1;
	}

	tex = pdtex_allocate();

	if (pdtex_read(tex, argv[1]) != 0) {
		return 2;
	}

	image = &tex->images[0];
	paletted = image->format >= PDFORMAT_RGBA16_CI8;

	/* I4/IA4 are held one byte per pixel; everything else at its native
	 * width. */
	if (paletted || image->format == PDFORMAT_I4 || image->format == PDFORMAT_IA4) {
		numbytes = (long)image->width * image->height;
	} else if (image->format == PDFORMAT_RGB15) {
		numbytes = (long)image->width * image->height * 2;
	} else {
		numbytes = (long)image->width * image->height * g_TexFormatBitsPerPixel[image->format] / 8;
	}

	header[0] = image->format;
	header[1] = image->width;
	header[2] = image->height;
	header[3] = image->compression;
	header[4] = tex->numcolours;

	out = fopen(argv[2], "wb");
	fwrite(header, 4, 5, out);
	fwrite(image->pixels, 1, numbytes, out);

	if (paletted) {
		fwrite(tex->palette, 1, tex->numcolours * 2, out);
	}

	fclose(out);
	return 0;
}
