#ifndef REACTION_N64_H
#define REACTION_N64_H

#define SCREEN_W 320
#define SCREEN_H 240

#define RGB5551(r,g,b) ((unsigned short)((((unsigned int)(r) >> 3) << 11) | (((unsigned int)(g) >> 3) << 6) | (((unsigned int)(b) >> 3) << 1) | 1u))

#define PAD_A       0x8000u
#define PAD_B       0x4000u
#define PAD_Z       0x2000u
#define PAD_START   0x1000u
#define PAD_UP      0x0800u
#define PAD_DOWN    0x0400u
#define PAD_LEFT    0x0200u
#define PAD_RIGHT   0x0100u
#define PAD_L       0x0020u
#define PAD_R       0x0010u
#define PAD_C_UP    0x0008u
#define PAD_C_DOWN  0x0004u
#define PAD_C_LEFT  0x0002u
#define PAD_C_RIGHT 0x0001u
#define PAD_ANY     0xFF3Fu

void video_init(void);
void video_begin_frame(unsigned short background);
void video_present(void);
void draw_rect(int x, int y, int w, int h, unsigned short color);
void draw_circle(int center_x, int center_y, int radius, unsigned short color);
void draw_text(int x, int y, const char *text, int scale, unsigned short color);
int text_width(const char *text, int scale);

unsigned int controller_read(void);
unsigned int timer_read(void);
void timer_wait_ms(unsigned int milliseconds);

#endif
