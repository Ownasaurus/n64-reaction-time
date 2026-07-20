#include "n64.h"

#define VI_REG(index)  (*(volatile unsigned int *)(0xA4400000u + ((unsigned int)(index) * 4u)))
#define MI_REG(index)  (*(volatile unsigned int *)(0xA4300000u + ((unsigned int)(index) * 4u)))
#define DPC_REG(index) (*(volatile unsigned int *)(0xA4100000u + ((unsigned int)(index) * 4u)))
#define SI_REG(index)  (*(volatile unsigned int *)(0xA4800000u + ((unsigned int)(index) * 4u)))

#define FB0_PHYS 0x00200000u
#define FB1_PHYS 0x00280000u
#define RDP_LIST_PHYS 0x00120000u
#define RDP_LIST_MAX_WORDS 8192u
#define UNCACHED_ADDR(physical) (0xA0000000u | (physical))

#define COUNT_TICKS_PER_MS 46875u
#define SI_TIMEOUT_TICKS (COUNT_TICKS_PER_MS * 20u)
#define RDP_TIMEOUT_TICKS (COUNT_TICKS_PER_MS * 100u)
#define VBLANK_TIMEOUT_TICKS (COUNT_TICKS_PER_MS * 50u)
#define PIF_BUFFER_PHYS 0x00100000u

/* Conventional raw RDP command bytes.  GLideN64 masks the low six bits of
   these bytes, matching the hardware command decoder. */
#define RDP_CMD_PIPE_SYNC       0xE7000000u
#define RDP_CMD_FULL_SYNC       0xE9000000u
#define RDP_CMD_SET_SCISSOR     0xED000000u
#define RDP_CMD_SET_OTHER_MODES 0xEF000000u
#define RDP_CMD_FILL_RECT       0xF6000000u
#define RDP_CMD_SET_FILL_COLOR  0xF7000000u
#define RDP_CMD_SET_COLOR_IMAGE 0xFF000000u

static unsigned int g_draw_buffer_phys;
static unsigned int g_display_buffer_phys;
static volatile unsigned int *g_rdp_words =
    (volatile unsigned int *)UNCACHED_ADDR(RDP_LIST_PHYS);
static unsigned int g_rdp_word_count;
static unsigned int g_rdp_fill_word;

/* 5x7 uppercase font. Each byte is one row; low five bits are pixels. */
static const unsigned char FONT[26][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F},
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0F,0x10,0x10,0x13,0x11,0x11,0x0F},
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F},
    {0x01,0x01,0x01,0x01,0x11,0x11,0x0E},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}
};

static const unsigned char DIGITS[10][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E},
    {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E},
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}
};

static unsigned char glyph_row(char ch, int row)
{
    if (ch >= 'A' && ch <= 'Z') return FONT[(int)(ch - 'A')][row];
    if (ch >= '0' && ch <= '9') return DIGITS[(int)(ch - '0')][row];
    if (ch == '!') return (row < 5) ? 0x04 : ((row == 6) ? 0x04 : 0x00);
    if (ch == '-') return (row == 3) ? 0x0E : 0x00;
    return 0x00;
}

unsigned int timer_read(void)
{
    unsigned int value;
    __asm__ volatile("mfc0 %0, $9" : "=r"(value));
    return value;
}

void timer_wait_ms(unsigned int milliseconds)
{
    const unsigned int start = timer_read();
    const unsigned int ticks = milliseconds * COUNT_TICKS_PER_MS;
    while ((unsigned int)(timer_read() - start) < ticks) { }
}

static void rdp_emit(unsigned int word0, unsigned int word1)
{
    if (g_rdp_word_count + 2u > RDP_LIST_MAX_WORDS) return;
    g_rdp_words[g_rdp_word_count++] = word0;
    g_rdp_words[g_rdp_word_count++] = word1;
}

static void rdp_set_fill_color(unsigned short color)
{
    const unsigned int packed = ((unsigned int)color << 16) | (unsigned int)color;
    if (packed == g_rdp_fill_word) return;
    rdp_emit(RDP_CMD_SET_FILL_COLOR, packed);
    g_rdp_fill_word = packed;
}

static void rdp_begin(unsigned int framebuffer_phys, unsigned short background)
{
    g_rdp_word_count = 0u;
    g_rdp_fill_word = 0xFFFFFFFFu;

    rdp_emit(RDP_CMD_PIPE_SYNC, 0u);
    /* Fill cycle: cycleType is bits 20..21 of the high other-mode word. */
    rdp_emit(RDP_CMD_SET_OTHER_MODES | 0x00300000u, 0u);
    /* RGBA, 16-bit, width 320. */
    rdp_emit(RDP_CMD_SET_COLOR_IMAGE | (2u << 19) | (SCREEN_W - 1u), framebuffer_phys);
    /* Full 320x240 scissor, encoded as unsigned 10.2 coordinates. */
    rdp_emit(RDP_CMD_SET_SCISSOR,
             ((unsigned int)(SCREEN_W * 4) << 12) | (unsigned int)(SCREEN_H * 4));

    rdp_set_fill_color(background);
    rdp_emit(RDP_CMD_FILL_RECT |
             ((unsigned int)(SCREEN_W - 1) << 14) |
             ((unsigned int)(SCREEN_H - 1) << 2),
             0u);
}

static int rdp_submit(void)
{
    const unsigned int end_phys = RDP_LIST_PHYS + g_rdp_word_count * 4u;
    const unsigned int start_time = timer_read();

    rdp_emit(RDP_CMD_FULL_SYNC, 0u);

    /* Clear XBUS, freeze and flush, plus stale busy counters. */
    DPC_REG(3) = 0x000003D5u;
    DPC_REG(0) = RDP_LIST_PHYS;
    DPC_REG(1) = RDP_LIST_PHYS + g_rdp_word_count * 4u;

    while ((DPC_REG(2) & 0x00FFFFF8u) !=
           ((RDP_LIST_PHYS + g_rdp_word_count * 4u) & 0x00FFFFF8u)) {
        if ((unsigned int)(timer_read() - start_time) >= RDP_TIMEOUT_TICKS) {
            return 0;
        }
    }

    /* A FullSync raises DP interrupt; this program polls completion instead. */
    MI_REG(0) = 0x00000800u;
    (void)end_phys;
    return 1;
}

static void wait_for_vblank_start(void)
{
    const unsigned int threshold = 480u;
    unsigned int start = timer_read();

    /* If already in blanking, first leave it so this waits for a fresh edge. */
    while ((VI_REG(4) & 0x3FFu) >= threshold) {
        if ((unsigned int)(timer_read() - start) >= VBLANK_TIMEOUT_TICKS) return;
    }

    start = timer_read();
    while ((VI_REG(4) & 0x3FFu) < threshold) {
        if ((unsigned int)(timer_read() - start) >= VBLANK_TIMEOUT_TICKS) return;
    }
}

static void rdp_clear_target(unsigned int framebuffer_phys, unsigned short color)
{
    rdp_begin(framebuffer_phys, color);
    (void)rdp_submit();
}

void video_init(void)
{
    /* Keep scanout disabled while both RDP render targets are initialized. */
    VI_REG(0) = 0u;
    DPC_REG(3) = 0x000003D5u;

    rdp_clear_target(FB0_PHYS, 0u);
    rdp_clear_target(FB1_PHYS, 0u);

    /* Conventional NTSC 320x240, non-interlaced, RGBA5551. */
    VI_REG(1)  = FB0_PHYS;
    VI_REG(2)  = SCREEN_W;
    VI_REG(3)  = 2u;
    VI_REG(5)  = 0x03E52239u;
    VI_REG(6)  = 0x0000020Du;
    VI_REG(7)  = 0x00000C15u;
    VI_REG(8)  = 0x0C150C15u;
    VI_REG(9)  = 0x006C02ECu;
    VI_REG(10) = 0x002501FFu;
    VI_REG(11) = 0x000E0204u;
    VI_REG(12) = 0x00000200u;
    VI_REG(13) = 0x00000400u;
    VI_REG(0)  = 0x0000320Eu;

    g_display_buffer_phys = FB0_PHYS;
    g_draw_buffer_phys = FB1_PHYS;
}

void video_begin_frame(unsigned short background)
{
    rdp_begin(g_draw_buffer_phys, background);
}

void video_present(void)
{
    if (!rdp_submit()) return;

    /* Change VI_ORIGIN only during vertical blank. */
    wait_for_vblank_start();
    VI_REG(1) = g_draw_buffer_phys;

    g_display_buffer_phys = g_draw_buffer_phys;
    g_draw_buffer_phys = (g_display_buffer_phys == FB0_PHYS) ? FB1_PHYS : FB0_PHYS;
}

void draw_rect(int x, int y, int w, int h, unsigned short color)
{
    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    if (y1 > SCREEN_H) y1 = SCREEN_H;
    if (x0 >= x1 || y0 >= y1) return;

    rdp_set_fill_color(color);
    rdp_emit(RDP_CMD_FILL_RECT |
             ((unsigned int)(x1 - 1) << 14) |
             ((unsigned int)(y1 - 1) << 2),
             ((unsigned int)x0 << 14) |
             ((unsigned int)y0 << 2));
}

static int integer_sqrt(int value)
{
    int low = 0;
    int high = 256;
    while (low <= high) {
        int mid = (low + high) >> 1;
        int square = mid * mid;
        if (square <= value) low = mid + 1;
        else high = mid - 1;
    }
    return high;
}

void draw_circle(int center_x, int center_y, int radius, unsigned short color)
{
    int y;
    const int radius_squared = radius * radius;
    for (y = -radius; y <= radius; ++y) {
        const int x_extent = integer_sqrt(radius_squared - y * y);
        draw_rect(center_x - x_extent, center_y + y, x_extent * 2 + 1, 1, color);
    }
}

void draw_text(int x, int y, const char *text, int scale, unsigned short color)
{
    int cursor = x;
    while (*text) {
        int row;
        const char ch = *text++;
        for (row = 0; row < 7; ++row) {
            const unsigned char bits = glyph_row(ch, row);
            int col = 0;
            while (col < 5) {
                int run_start;
                while (col < 5 && !(bits & (1u << (4 - col)))) ++col;
                run_start = col;
                while (col < 5 && (bits & (1u << (4 - col)))) ++col;
                if (run_start < col) {
                    draw_rect(cursor + run_start * scale,
                              y + row * scale,
                              (col - run_start) * scale,
                              scale,
                              color);
                }
            }
        }
        cursor += 6 * scale;
    }
}

int text_width(const char *text, int scale)
{
    int length = 0;
    while (*text++) ++length;
    return length ? (length * 6 - 1) * scale : 0;
}

static int si_wait_idle(void)
{
    const unsigned int start = timer_read();
    while (SI_REG(6) & 0x0003u) {
        if ((unsigned int)(timer_read() - start) >= SI_TIMEOUT_TICKS) return 0;
    }
    return 1;
}

unsigned int controller_read(void)
{
    const unsigned int physical = PIF_BUFFER_PHYS;
    volatile unsigned char *buffer = (volatile unsigned char *)UNCACHED_ADDR(PIF_BUFFER_PHYS);
    unsigned int i;
    unsigned int buttons;

    if (!si_wait_idle()) return 0u;
    SI_REG(6) = 0u;
    for (i = 0; i < 64u; ++i) buffer[i] = 0u;

    buffer[0] = 0x01u;
    buffer[1] = 0x04u;
    buffer[2] = 0x01u;
    buffer[7] = 0xFEu;
    buffer[63] = 0x01u;

    SI_REG(0) = physical;
    SI_REG(4) = 0x1FC007C0u;
    if (!si_wait_idle()) return 0u;

    SI_REG(0) = physical;
    SI_REG(1) = 0x1FC007C0u;
    if (!si_wait_idle()) return 0u;
    SI_REG(6) = 0u;

    if (buffer[1] & 0xC0u) return 0u;
    buttons = ((unsigned int)buffer[3] << 8) | (unsigned int)buffer[4];
    return buttons & PAD_ANY;
}
