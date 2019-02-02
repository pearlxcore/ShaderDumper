#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debugnet.h>

#include <kernel.h>
#include <systemservice.h>
#include <orbis2d.h>
#include <sys/fcntl.h>
#include <ps4link.h>

#include "font.h"
#include "font_draw.h"


uint32_t font_color=0x80000000;
uint32_t backfont_color=0x80FFFFFF;

uint32_t getRGB(int r, int g, int b) {
	r=r%256;
	g=g%256;
	b=b%256;
	return 0x80000000|r<<16|g<<8|b;
}

void font_setFontColor(uint32_t color) {
	font_color = color;
}

void font_setBackFontColor(uint32_t color) {
	backfont_color = color;
}

void font_drawCharacter(int character, int x, int y, int scale) {
    for (int yy = 0; yy < 10 ; yy++) {
        uint8_t charPos = font[character * 10 + yy];
        int off = 8;
        for (int xx = 0; xx < 8; xx++) {           // font color : background color
			uint32_t clr = ((charPos >> xx) & 1) ? font_color : backfont_color;  // 0x00000000

			for (int sy = 0; sy < scale; sy++) {
				for (int sx = 0; sx < scale; sx++) {
					orbis2dDrawPixelColor(x + (off * scale) - sx, y + (yy * scale) + sy, clr);
				}
			}

			off--;
        }
    }
}

void font_drawString(int x, int y, int scale, const char *str) {
    for (size_t i = 0; i < strlen(str); i++)
        font_drawCharacter(str[i], x + (i * 8 * scale), y, scale);
}