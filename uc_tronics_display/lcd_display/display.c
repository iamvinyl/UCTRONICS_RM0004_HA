/******
 * Configurable display for the UCTRONICS RM0004 160x80 LCD.
 *
 * Enabled screens are selected by run.sh from the Home Assistant add-on
 * configuration page and passed as command-line flags.
 ******/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "st7735.h"
#include "rpiInfo.h"

#define LCD_WIDTH 160
#define LCD_HEIGHT 80
#define MAX_SCREENS 5
#define DEFAULT_SCREEN_DURATION 5
#define MIN_SCREEN_DURATION 2
#define MAX_SCREEN_DURATION 60

typedef enum
{
    SCREEN_HOME_ASSISTANT_LOGO,
    SCREEN_IP_ADDRESS,
    SCREEN_CPU_USAGE,
    SCREEN_RAM_USAGE,
    SCREEN_DISK_SPACE
} screen_type_t;

static screen_type_t enabled_screens[MAX_SCREENS];
static int enabled_screen_count = 0;
static unsigned int screen_duration = DEFAULT_SCREEN_DURATION;

/* Canvas used only for the Home Assistant logo. */
static uint16_t logo_canvas[LCD_WIDTH * LCD_HEIGHT];

static void add_screen(screen_type_t screen)
{
    if (enabled_screen_count < MAX_SCREENS)
    {
        enabled_screens[enabled_screen_count++] = screen;
    }
}

static uint16_t centered_x(const char *text, uint16_t character_width)
{
    size_t pixel_width;

    if (text == NULL)
    {
        return 0;
    }

    pixel_width = strlen(text) * character_width;

    if (pixel_width >= LCD_WIDTH)
    {
        return 0;
    }

    return (uint16_t)((LCD_WIDTH - pixel_width) / 2);
}

static void draw_title(const char *title, uint16_t color)
{
    lcd_write_string(
        centered_x(title, Font_8x16.width),
        2,
        (char *)title,
        Font_8x16,
        color,
        ST7735_BLACK
    );

    lcd_fill_rectangle(0, 21, LCD_WIDTH, 3, color);
}

static void draw_percentage_value(uint8_t percentage, uint16_t color)
{
    char value[8];

    if (percentage > 100)
    {
        percentage = 100;
    }

    snprintf(value, sizeof(value), "%u%%", (unsigned int)percentage);

    lcd_write_string(
        centered_x(value, Font_11x18.width),
        32,
        value,
        Font_11x18,
        ST7735_WHITE,
        ST7735_BLACK
    );

    lcd_display_percentage(percentage, color);
}

static void logo_set_pixel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT)
    {
        return;
    }

    logo_canvas[(y * LCD_WIDTH) + x] = color;
}

static void logo_fill_circle(
    int center_x,
    int center_y,
    int radius,
    uint16_t color
)
{
    int x;
    int y;

    for (y = -radius; y <= radius; y++)
    {
        for (x = -radius; x <= radius; x++)
        {
            if ((x * x) + (y * y) <= (radius * radius))
            {
                logo_set_pixel(center_x + x, center_y + y, color);
            }
        }
    }
}

static void logo_draw_thick_line(
    int x0,
    int y0,
    int x1,
    int y1,
    int radius,
    uint16_t color
)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (1)
    {
        logo_fill_circle(x0, y0, radius, color);

        if (x0 == x1 && y0 == y1)
        {
            break;
        }

        if ((2 * error) >= dy)
        {
            error += dy;
            x0 += sx;
        }

        if ((2 * error) <= dx)
        {
            error += dx;
            y0 += sy;
        }
    }
}

static void build_home_assistant_logo(void)
{
    const uint16_t blue = ST7735_COLOR565(24, 188, 242);
    const uint16_t white = ST7735_WHITE;
    int index;

    for (index = 0; index < LCD_WIDTH * LCD_HEIGHT; index++)
    {
        logo_canvas[index] = ST7735_BLACK;
    }

    /* Centered Home Assistant house outline. */
    logo_draw_thick_line(51, 36, 80, 7, 4, blue);
    logo_draw_thick_line(80, 7, 109, 36, 4, blue);
    logo_draw_thick_line(51, 36, 51, 59, 4, blue);
    logo_draw_thick_line(109, 36, 109, 59, 4, blue);
    logo_draw_thick_line(51, 59, 70, 72, 4, blue);
    logo_draw_thick_line(109, 59, 90, 72, 4, blue);

    /* Connected smart-home nodes inside the house. */
    logo_draw_thick_line(80, 26, 80, 56, 2, white);
    logo_draw_thick_line(80, 38, 65, 46, 2, white);
    logo_draw_thick_line(80, 38, 95, 46, 2, white);
    logo_draw_thick_line(80, 52, 71, 62, 2, white);
    logo_draw_thick_line(80, 52, 89, 62, 2, white);

    logo_fill_circle(80, 26, 4, white);
    logo_fill_circle(65, 46, 4, white);
    logo_fill_circle(95, 46, 4, white);
    logo_fill_circle(71, 62, 4, white);
    logo_fill_circle(89, 62, 4, white);
}

static void render_home_assistant_logo(void)
{
    int y;

    lcd_fill_screen(ST7735_BLACK);
    build_home_assistant_logo();

    /*
     * Draw short horizontal runs through lcd_fill_rectangle(). This uses the
     * same reliable I2C drawing path as the original UCTRONICS metric pages.
     */
    for (y = 0; y < LCD_HEIGHT; y++)
    {
        int x = 0;

        while (x < LCD_WIDTH)
        {
            uint16_t color = logo_canvas[(y * LCD_WIDTH) + x];
            int start = x;

            while (
                x < LCD_WIDTH &&
                logo_canvas[(y * LCD_WIDTH) + x] == color
            )
            {
                x++;
            }

            if (color != ST7735_BLACK)
            {
                lcd_fill_rectangle(
                    (uint16_t)start,
                    (uint16_t)y,
                    (uint16_t)(x - start),
                    1,
                    color
                );
            }
        }
    }
}

static void render_ip_address(void)
{
    char *ip_address = get_ip_address();

    lcd_fill_screen(ST7735_BLACK);
    draw_title("IP ADDRESS", ST7735_CYAN);

    if (
        ip_address == NULL ||
        ip_address[0] == '\0'
    )
    {
        ip_address = "Unavailable";
    }

    lcd_write_string(
        centered_x(ip_address, Font_8x16.width),
        37,
        ip_address,
        Font_8x16,
        ST7735_WHITE,
        ST7735_BLACK
    );
}

static void render_cpu_usage(void)
{
    uint8_t cpu_usage = get_cpu_message();

    if (cpu_usage > 100)
    {
        cpu_usage = 100;
    }

    lcd_fill_screen(ST7735_BLACK);
    draw_title("CPU USAGE", ST7735_GREEN);
    draw_percentage_value(cpu_usage, ST7735_GREEN);
}

static void render_ram_usage(void)
{
    float total_ram = 0.0f;
    float free_ram = 0.0f;
    float used_percentage = 0.0f;
    uint8_t ram_usage = 0;

    get_cpu_memory(&total_ram, &free_ram);

    if (total_ram > 0.0f)
    {
        used_percentage = ((total_ram - free_ram) / total_ram) * 100.0f;

        if (used_percentage < 0.0f)
        {
            used_percentage = 0.0f;
        }
        else if (used_percentage > 100.0f)
        {
            used_percentage = 100.0f;
        }

        ram_usage = (uint8_t)used_percentage;
    }

    lcd_fill_screen(ST7735_BLACK);
    draw_title("RAM USAGE", ST7735_YELLOW);
    draw_percentage_value(ram_usage, ST7735_YELLOW);
}

static void render_disk_space(void)
{
    uint32_t sd_total = 0;
    uint32_t sd_used = 0;
    uint16_t disk_total = 0;
    uint16_t disk_used = 0;
    uint64_t total_space;
    uint64_t used_space;
    uint8_t disk_usage = 0;

    get_sd_memory(&sd_total, &sd_used);
    get_hard_disk_memory(&disk_total, &disk_used);

    total_space = (uint64_t)sd_total + (uint64_t)disk_total;
    used_space = (uint64_t)sd_used + (uint64_t)disk_used;

    if (total_space > 0)
    {
        if (used_space > total_space)
        {
            used_space = total_space;
        }

        disk_usage = (uint8_t)((used_space * 100U) / total_space);
    }

    lcd_fill_screen(ST7735_BLACK);
    draw_title("DISK SPACE", ST7735_BLUE);
    draw_percentage_value(disk_usage, ST7735_BLUE);
}

static const char *screen_name(screen_type_t screen)
{
    switch (screen)
    {
        case SCREEN_HOME_ASSISTANT_LOGO:
            return "Home Assistant logo";
        case SCREEN_IP_ADDRESS:
            return "IP address";
        case SCREEN_CPU_USAGE:
            return "CPU usage";
        case SCREEN_RAM_USAGE:
            return "RAM usage";
        case SCREEN_DISK_SPACE:
            return "disk space";
        default:
            return "unknown";
    }
}

static void render_screen(screen_type_t screen)
{
    printf("Displaying %s\n", screen_name(screen));
    fflush(stdout);

    switch (screen)
    {
        case SCREEN_HOME_ASSISTANT_LOGO:
            render_home_assistant_logo();
            break;
        case SCREEN_IP_ADDRESS:
            render_ip_address();
            break;
        case SCREEN_CPU_USAGE:
            render_cpu_usage();
            break;
        case SCREEN_RAM_USAGE:
            render_ram_usage();
            break;
        case SCREEN_DISK_SPACE:
            render_disk_space();
            break;
        default:
            render_home_assistant_logo();
            break;
    }
}

static void parse_arguments(int argc, char *argv[])
{
    int index;

    for (index = 1; index < argc; index++)
    {
        if (strcmp(argv[index], "--logo") == 0)
        {
            add_screen(SCREEN_HOME_ASSISTANT_LOGO);
        }
        else if (strcmp(argv[index], "--ip") == 0)
        {
            add_screen(SCREEN_IP_ADDRESS);
        }
        else if (strcmp(argv[index], "--cpu") == 0)
        {
            add_screen(SCREEN_CPU_USAGE);
        }
        else if (strcmp(argv[index], "--ram") == 0)
        {
            add_screen(SCREEN_RAM_USAGE);
        }
        else if (strcmp(argv[index], "--disk") == 0)
        {
            add_screen(SCREEN_DISK_SPACE);
        }
        else if (
            strcmp(argv[index], "--duration") == 0 &&
            index + 1 < argc
        )
        {
            long requested_duration = strtol(argv[++index], NULL, 10);

            if (requested_duration < MIN_SCREEN_DURATION)
            {
                requested_duration = MIN_SCREEN_DURATION;
            }
            else if (requested_duration > MAX_SCREEN_DURATION)
            {
                requested_duration = MAX_SCREEN_DURATION;
            }

            screen_duration = (unsigned int)requested_duration;
        }
    }

    if (enabled_screen_count == 0)
    {
        fprintf(
            stderr,
            "No screens were enabled; falling back to the Home Assistant logo.\n"
        );
        add_screen(SCREEN_HOME_ASSISTANT_LOGO);
    }
}

int main(int argc, char *argv[])
{
    int current_screen = 0;

    parse_arguments(argc, argv);

    if (lcd_begin())
    {
        fprintf(stderr, "Unable to initialize the UCTRONICS LCD.\n");
        return 1;
    }

    sleep(1);

    printf(
        "%d screen(s) enabled; rotating every %u seconds.\n",
        enabled_screen_count,
        screen_duration
    );
    fflush(stdout);

    while (1)
    {
        render_screen(enabled_screens[current_screen]);
        sleep(screen_duration);

        current_screen++;

        if (current_screen >= enabled_screen_count)
        {
            current_screen = 0;
        }
    }

    return 0;
}

