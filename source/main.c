#include "n64.h"

#define COLOR_BACKGROUND RGB5551(10, 12, 18)
#define COLOR_PANEL       RGB5551(25, 29, 40)
#define COLOR_WHITE       RGB5551(245, 245, 245)
#define COLOR_MUTED       RGB5551(160, 170, 185)
#define COLOR_RED         RGB5551(235, 45, 55)
#define COLOR_RED_DARK    RGB5551(110, 18, 25)
#define COLOR_GREEN       RGB5551(45, 220, 90)
#define COLOR_GREEN_DARK  RGB5551(12, 95, 36)
#define COLOR_YELLOW      RGB5551(255, 205, 50)

#define COUNT_TICKS_PER_MS 46875u
#define MIN_WAIT_MS 2000u
#define WAIT_RANGE_MS 3001u
#define RESULT_HOLD_MS 2500u
#define EARLY_HOLD_MS 1300u

static unsigned int g_rng_state = 0x8B5AD4CEu;

static unsigned int random_next(void)
{
    unsigned int x = g_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng_state = x ? x : 1u;
    return g_rng_state;
}

static void center_text(int y, const char *text, int scale, unsigned short color)
{
    draw_text((SCREEN_W - text_width(text, scale)) / 2, y, text, scale, color);
}

static void number_to_text(unsigned int value, char *out)
{
    char reversed[11];
    int count = 0;
    int i;

    if (value == 0u) reversed[count++] = '0';
    while (value != 0u && count < 10) {
        reversed[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    for (i = 0; i < count; ++i) out[i] = reversed[count - 1 - i];
    out[count] = '\0';
}

static void draw_common_frame(void)
{
    video_begin_frame(COLOR_BACKGROUND);
    draw_rect(10, 10, 300, 220, COLOR_PANEL);
    center_text(20, "REACTION TEST", 2, COLOR_WHITE);
}

static void show_wait_screen(void)
{
    draw_common_frame();
    draw_circle(160, 112, 44, COLOR_RED_DARK);
    draw_circle(160, 108, 39, COLOR_RED);
    center_text(186, "WAIT FOR GREEN", 2, COLOR_WHITE);
    center_text(208, "PRESS NOTHING", 1, COLOR_MUTED);
    video_present();
}

static void show_go_screen(void)
{
    draw_common_frame();
    draw_circle(160, 112, 44, COLOR_GREEN_DARK);
    draw_circle(160, 108, 39, COLOR_GREEN);
    center_text(185, "PRESS ANY BUTTON", 2, COLOR_WHITE);
    video_present();
}

static void show_early_screen(void)
{
    draw_common_frame();
    draw_circle(160, 112, 44, COLOR_RED_DARK);
    draw_circle(160, 108, 39, COLOR_RED);
    center_text(181, "TOO EARLY!", 3, COLOR_YELLOW);
    center_text(211, "RELEASE BUTTONS", 1, COLOR_MUTED);
    video_present();
}

static void show_result_screen(unsigned int milliseconds)
{
    char number[11];
    int number_scale;

    number_to_text(milliseconds, number);
    number_scale = (milliseconds < 1000u) ? 5 : ((milliseconds < 10000u) ? 4 : 3);

    draw_common_frame();
    center_text(65, "RESULT", 2, COLOR_MUTED);
    center_text(98, number, number_scale, COLOR_GREEN);
    center_text(148, "MS", 3, COLOR_WHITE);
    center_text(198, "NEXT ROUND SOON", 1, COLOR_MUTED);
    video_present();
}

static void wait_for_button_release(void)
{
    const unsigned int start = timer_read();
    while (controller_read() != 0u) {
        /* Keep SI traffic bounded and never let a malformed packet lock startup. */
        if ((unsigned int)(timer_read() - start) >= COUNT_TICKS_PER_MS * 1000u) break;
        timer_wait_ms(1u);
    }
}

int main(void)
{
    video_init();
    g_rng_state ^= timer_read();

    /* Put a valid frame on screen before touching SI/PIF. */
    show_wait_screen();
    timer_wait_ms(100u);
    wait_for_button_release();

    for (;;) {
        unsigned int wait_ms;
        unsigned int wait_start;
        int false_start = 0;

        show_wait_screen();
        wait_ms = MIN_WAIT_MS + (random_next() % WAIT_RANGE_MS);
        wait_start = timer_read();

        while ((unsigned int)(timer_read() - wait_start) < wait_ms * COUNT_TICKS_PER_MS) {
            if (controller_read() != 0u) {
                false_start = 1;
                break;
            }
            /* About 1 kHz polling: millisecond resolution without flooding SI. */
            timer_wait_ms(1u);
        }

        if (false_start) {
            show_early_screen();
            timer_wait_ms(EARLY_HOLD_MS);
            wait_for_button_release();
            continue;
        }

        show_go_screen();
        {
            const unsigned int green_time = timer_read();
            unsigned int reaction_ticks;
            unsigned int reaction_ms;

            while (controller_read() == 0u) {
                timer_wait_ms(1u);
            }
            reaction_ticks = timer_read() - green_time;
            reaction_ms = (reaction_ticks + (COUNT_TICKS_PER_MS / 2u)) / COUNT_TICKS_PER_MS;

            show_result_screen(reaction_ms);
            timer_wait_ms(RESULT_HOLD_MS);
            wait_for_button_release();
        }
    }
}
