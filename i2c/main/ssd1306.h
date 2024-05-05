#include <stdio.h>


void oled_init(void);
void oled_clear(void);
// x - 0~127, y - 0~7, size: 1 - 6*8, 2 - 8*16
void oled_show_char(uint8_t x, uint8_t y, uint8_t ch, uint8_t size);
// x - 0~127, y - 0~7, size: 1 - 6*8, 2 - 8*16
void oled_show_string(uint8_t x, uint8_t y, char *string, uint8_t size);
// x - 0~127, y - 0~7, size: 16*16
void oled_show_chinese(uint8_t x, uint8_t y, uint8_t* index, uint8_t num);
// x - 0~127, y - 0~63, value: 0 - off, 1 - on
void oled_show_point(uint8_t x, uint8_t y, uint8_t value);
// x - 0~127, y - 0~63, x1 <= x2, value: 0 - off, 1 - on
void oled_show_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t value);
