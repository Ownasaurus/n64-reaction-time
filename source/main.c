#include "n64.h"

#define COLOR_BACKGROUND RGB5551(10, 12, 18)
#define COLOR_PANEL       RGB5551(25, 29, 40)
#define COLOR_STATS       RGB5551(16, 19, 28)
#define COLOR_WHITE       RGB5551(245, 245, 245)
#define COLOR_MUTED       RGB5551(160, 170, 185)
#define COLOR_RED         RGB5551(235, 45, 55)
#define COLOR_RED_DARK    RGB5551(110, 18, 25)
#define COLOR_GREEN       RGB5551(45, 220, 90)
#define COLOR_GREEN_DARK  RGB5551(12, 95, 36)
#define COLOR_YELLOW      RGB5551(255, 205, 50)
#define COLOR_BLUE        RGB5551(75, 160, 255)

#define COUNT_TICKS_PER_MS 46875u
#define MIN_WAIT_MS 2000u
#define WAIT_RANGE_MS 3001u
#define RESULT_HOLD_MS 2500u
#define EARLY_HOLD_MS 1300u
#define RESET_HOLD_MS 900u
#define REACTION_BUTTONS (PAD_ANY & ~PAD_START)

static unsigned int g_rng_state = 0x8B5AD4CEu;
static unsigned int g_stat_count;
static unsigned int g_stat_min;
static unsigned int g_stat_max;
static unsigned int g_stat_sum;

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

static int append_text(char *out, int position, const char *text)
{
    while (*text) out[position++] = *text++;
    out[position] = '\0';
    return position;
}

static int append_number(char *out, int position, unsigned int value)
{
    char number[11];
    number_to_text(value, number);
    return append_text(out, position, number);
}

static void reset_statistics(void)
{
    g_stat_count = 0u;
    g_stat_min = 0u;
    g_stat_max = 0u;
    g_stat_sum = 0u;
}

static void record_reaction(unsigned int milliseconds)
{
    if (g_stat_count == 0u) {
        g_stat_min = milliseconds;
        g_stat_max = milliseconds;
    } else {
        if (milliseconds < g_stat_min) g_stat_min = milliseconds;
        if (milliseconds > g_stat_max) g_stat_max = milliseconds;
    }

    /* More than 49 days of accumulated milliseconds would be needed to wrap. */
    if (0xFFFFFFFFu - g_stat_sum >= milliseconds) {
        g_stat_sum += milliseconds;
        ++g_stat_count;
    }
}

static unsigned int average_reaction(void)
{
    if (g_stat_count == 0u) return 0u;
    return (g_stat_sum + (g_stat_count / 2u)) / g_stat_count;
}

static void draw_statistics(void)
{
    char line[48];
    int position;

    draw_rect(22, 174, 276, 49, COLOR_STATS);

    position = 0;
    position = append_text(line, position, "MIN ");
    position = append_number(line, position, g_stat_min);
    position = append_text(line, position, "   MAX ");
    position = append_number(line, position, g_stat_max);
    center_text(180, line, 1, COLOR_WHITE);

    position = 0;
    position = append_text(line, position, "AVG ");
    position = append_number(line, position, average_reaction());
    position = append_text(line, position, "   RUNS ");
    position = append_number(line, position, g_stat_count);
    center_text(193, line, 1, COLOR_WHITE);

    center_text(209, "START RESETS STATS", 1, COLOR_MUTED);
}

static void draw_common_frame(void)
{
    video_begin_frame(COLOR_BACKGROUND);
    draw_rect(10, 10, 300, 220, COLOR_PANEL);
    center_text(18, "REACTION TEST", 2, COLOR_WHITE);
}

static void show_wait_screen(void)
{
    draw_common_frame();
    draw_circle(160, 93, 39, COLOR_RED_DARK);
    draw_circle(160, 89, 34, COLOR_RED);
    center_text(137, "WAIT FOR GREEN", 2, COLOR_WHITE);
    center_text(158, "ANY OF 4 PLAYERS", 1, COLOR_MUTED);
    draw_statistics();
    video_present();
}

static void show_go_screen(void)
{
    draw_common_frame();
    draw_circle(160, 93, 39, COLOR_GREEN_DARK);
    draw_circle(160, 89, 34, COLOR_GREEN);
    center_text(137, "PRESS ANY BUTTON", 2, COLOR_WHITE);
    center_text(158, "FIRST PRESS WINS", 1, COLOR_MUTED);
    draw_statistics();
    video_present();
}

static void show_early_screen(void)
{
    draw_common_frame();
    draw_circle(160, 91, 37, COLOR_RED_DARK);
    draw_circle(160, 87, 32, COLOR_RED);
    center_text(130, "TOO EARLY!", 3, COLOR_YELLOW);
    center_text(158, "RELEASE ALL BUTTONS", 1, COLOR_MUTED);
    draw_statistics();
    video_present();
}

static void show_reset_screen(void)
{
    draw_common_frame();
    center_text(72, "STATS RESET", 3, COLOR_BLUE);
    center_text(116, "MIN MAX AVG AND RUNS", 1, COLOR_WHITE);
    center_text(132, "ARE BACK TO 0", 1, COLOR_MUTED);
    draw_statistics();
    video_present();
}

static void show_result_screen(unsigned int milliseconds)
{
    char number[11];
    int number_scale;

    number_to_text(milliseconds, number);
    number_scale = (milliseconds < 1000u) ? 4 : ((milliseconds < 10000u) ? 3 : 2);

    draw_common_frame();
    center_text(50, "RESULT", 2, COLOR_MUTED);
    center_text(77, number, number_scale, COLOR_GREEN);
    center_text(117, "MS", 2, COLOR_WHITE);
    center_text(148, "NEXT ROUND SOON", 1, COLOR_MUTED);
    draw_statistics();
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

static void perform_statistics_reset(void)
{
    reset_statistics();
    show_reset_screen();
    timer_wait_ms(RESET_HOLD_MS);
    wait_for_button_release();
}

static int hold_screen_or_reset(unsigned int milliseconds)
{
    const unsigned int start = timer_read();
    const unsigned int ticks = milliseconds * COUNT_TICKS_PER_MS;

    while ((unsigned int)(timer_read() - start) < ticks) {
        const unsigned int buttons = controller_read();
        if (buttons & PAD_START) {
            perform_statistics_reset();
            return 1;
        }
        timer_wait_ms(1u);
    }
    return 0;
}

int main(void)
{
    video_init();
    reset_statistics();
    g_rng_state ^= timer_read();

    /* Put a valid frame on screen before touching SI/PIF. */
    show_wait_screen();
    timer_wait_ms(100u);
    wait_for_button_release();

    for (;;) {
        unsigned int wait_ms;
        unsigned int wait_start;
        int false_start = 0;
        int reset_requested = 0;

        show_wait_screen();
        wait_ms = MIN_WAIT_MS + (random_next() % WAIT_RANGE_MS);
        wait_start = timer_read();

        while ((unsigned int)(timer_read() - wait_start) < wait_ms * COUNT_TICKS_PER_MS) {
            const unsigned int buttons = controller_read();
            if (buttons & PAD_START) {
                perform_statistics_reset();
                reset_requested = 1;
                break;
            }
            if (buttons & REACTION_BUTTONS) {
                false_start = 1;
                break;
            }
            /* About 1 kHz polling: millisecond resolution without flooding SI. */
            timer_wait_ms(1u);
        }

        if (reset_requested) continue;

        if (false_start) {
            show_early_screen();
            if (hold_screen_or_reset(EARLY_HOLD_MS)) continue;
            wait_for_button_release();
            continue;
        }

        show_go_screen();
        {
            const unsigned int green_time = timer_read();
            unsigned int reaction_ticks = 0u;
            unsigned int reaction_ms = 0u;
            int reaction_received = 0;

            while (!reaction_received) {
                const unsigned int buttons = controller_read();
                if (buttons & PAD_START) {
                    perform_statistics_reset();
                    reset_requested = 1;
                    break;
                }
                if (buttons & REACTION_BUTTONS) {
                    reaction_ticks = timer_read() - green_time;
                    reaction_received = 1;
                    break;
                }
                timer_wait_ms(1u);
            }

            if (reset_requested) continue;

            reaction_ms = (reaction_ticks + (COUNT_TICKS_PER_MS / 2u)) / COUNT_TICKS_PER_MS;
            record_reaction(reaction_ms);
            show_result_screen(reaction_ms);
            if (hold_screen_or_reset(RESULT_HOLD_MS)) continue;
            wait_for_button_release();
        }
    }
}
