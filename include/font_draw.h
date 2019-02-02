uint32_t getRGB(int r, int g, int b);
void font_setFontColor(uint32_t color);
void font_setBackFontColor(uint32_t color);
void font_drawCharacter(int character, int x, int y, int scale);
void font_drawString(int x, int y, int scale, const char *str);
