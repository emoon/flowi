#include <stdint.h>
#include <stdio.h>
#include <freetype/freetype.h>

static void draw_bitmap(uint32_t* dest, FT_Bitmap* bitmap, FT_Int x, FT_Int y, int width, int height) {
	FT_Int  i, j, p, q;
	FT_Int  x_max = x + bitmap->width;
	FT_Int  y_max = y + bitmap->rows;

	// for simplicity, we assume that `bitmap->pixel_mode'
	// is `FT_PIXEL_MODE_GRAY' (i.e., not a bitmap font)

	for (i = x, p = 0; i < x_max; i++, p++) {
		for (j = y, q = 0; j < y_max; j++, q++) {
			if (i < 0 || j < 0 || i >= width || j >= height)
				continue;

			uint32_t v = bitmap->buffer[q * bitmap->width + p];
			dest[(j * width) + i] = (v << 16) | (v << 8) | v;
		}
	}
}

void fli_render_font(uint32_t* dest, int width, int height) {
	printf("test %p - %d %d\n", dest, width, height);

	FT_Library library;
	FT_Face face;

	int error = FT_Init_FreeType(&library);
	if (error) {
		printf("init error\n");
		return;
	}

	//error = FT_New_Face(library, "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf", 0, &face);
	error = FT_New_Face(library, "/usr/share/fonts/truetype/freefont/FreeSans.ttf", 0, &face);
	if (error) {
		printf("new face error %d\n", error);
		return;
	}

	error = FT_Set_Pixel_Sizes(face, 0, 256);

	if (error) {
		printf("unable to set pixel sizes");
		return;
	}

	FT_GlyphSlot slot = face->glyph;

	char* text = "hello!";
	int num_chars = 6;

	int pen_x = 50;
	int pen_y = 200;

	for (int n = 0; n < num_chars; n++) {
		// load glyph image into the slot (erase previous one)
		error = FT_Load_Char(face, text[n], FT_LOAD_RENDER);

		if (error)
			continue;

		/* now, draw to our target surface */
		draw_bitmap(dest, &slot->bitmap, pen_x + slot->bitmap_left, pen_y - slot->bitmap_top, width, height);

		// increment pen position
		pen_x += slot->advance.x >> 6;
	}

	printf("done\n");
}
