/******
 * Configurable display for the UCTRONICS RM0013 Raspberry Pi 5 rack.
 *
 * The RM0013 panel is treated as a 128x64 monochrome display. The underlying
 * UCTRONICS I2C bridge and existing st7735 driver are retained because the
 * stock RM0013 documentation points to the SKU_RM0004 software.
 ******/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "st7735.h"

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
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

static char host_ip_address[64] = "Unavailable";
static uint8_t host_disk_usage = 0;

/* Monochrome canvas used to render the Home Assistant logo. */
static uint8_t logo_canvas[DISPLAY_WIDTH * DISPLAY_HEIGHT];

static void sync_display(void)
{
    i2c_write_command(SYNC_REG, 0x00, 0x01);
}

static void fill_rectangle_synced(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    uint16_t color
)
{
    lcd_fill_rectangle(x, y, width, height, color);
    sync_display();
}

static void clear_display(void)
{
    lcd_fill_screen(ST7735_BLACK);
    sync_display();
}

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

    if (pixel_width >= DISPLAY_WIDTH)
    {
        return 0;
    }

    return (uint16_t)((DISPLAY_WIDTH - pixel_width) / 2);
}

static void draw_title(const char *title)
{
    lcd_write_string(
        centered_x(title, Font_7x10.width),
        1,
        (char *)title,
        Font_7x10,
        ST7735_WHITE,
        ST7735_BLACK
    );

    fill_rectangle_synced(0, 13, DISPLAY_WIDTH, 2, ST7735_WHITE);
}

static void draw_progress_bar(uint8_t percentage)
{
    const uint16_t start_x = 10;
    const uint16_t start_y = 52;
    const uint16_t segment_width = 8;
    const uint16_t segment_height = 8;
    const uint16_t gap = 3;
    uint8_t filled_segments;
    uint8_t index;

    if (percentage > 100)
    {
        percentage = 100;
    }

    filled_segments = percentage == 0
        ? 0
        : (uint8_t)((percentage + 9) / 10);

    for (index = 0; index < 10; index++)
    {
        uint16_t x = start_x + (index * (segment_width + gap));

        if (index < filled_segments)
        {
            fill_rectangle_synced(
                x,
                start_y,
                segment_width,
                segment_height,
                ST7735_WHITE
            );
        }
        else
        {
            /* Draw an outline for unused segments. */
            fill_rectangle_synced(
                x,
                start_y,
                segment_width,
                1,
                ST7735_WHITE
            );
            fill_rectangle_synced(
                x,
                start_y + segment_height - 1,
                segment_width,
                1,
                ST7735_WHITE
            );
            fill_rectangle_synced(
                x,
                start_y,
                1,
                segment_height,
                ST7735_WHITE
            );
            fill_rectangle_synced(
                x + segment_width - 1,
                start_y,
                1,
                segment_height,
                ST7735_WHITE
            );
        }
    }
}

static void draw_percentage_value(uint8_t percentage)
{
    char value[8];

    if (percentage > 100)
    {
        percentage = 100;
    }

    snprintf(value, sizeof(value), "%u%%", (unsigned int)percentage);

    lcd_write_string(
        centered_x(value, Font_16x26.width),
        20,
        value,
        Font_16x26,
        ST7735_WHITE,
        ST7735_BLACK
    );

    draw_progress_bar(percentage);
}

static void logo_set_pixel(int x, int y)
{
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT)
    {
        return;
    }

    logo_canvas[(y * DISPLAY_WIDTH) + x] = 1;
}

static void logo_fill_circle(int center_x, int center_y, int radius)
{
    int x;
    int y;

    for (y = -radius; y <= radius; y++)
    {
        for (x = -radius; x <= radius; x++)
        {
            if ((x * x) + (y * y) <= (radius * radius))
            {
                logo_set_pixel(center_x + x, center_y + y);
            }
        }
    }
}

static void logo_draw_thick_line(
    int x0,
    int y0,
    int x1,
    int y1,
    int radius
)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (1)
    {
        logo_fill_circle(x0, y0, radius);

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
    memset(logo_canvas, 0, sizeof(logo_canvas));

    /*
     * White-only Home Assistant mark for the RM0013 monochrome panel.
     * The mark is centered inside the 128x64 visible area.
     */
    logo_draw_thick_line(40, 29, 64, 5, 3);
    logo_draw_thick_line(64, 5, 88, 29, 3);
    logo_draw_thick_line(40, 29, 40, 45, 3);
    logo_draw_thick_line(88, 29, 88, 45, 3);
    logo_draw_thick_line(40, 45, 55, 59, 3);
    logo_draw_thick_line(88, 45, 73, 59, 3);

    /* Connected nodes inside the house. */
    logo_draw_thick_line(64, 20, 64, 45, 1);
    logo_draw_thick_line(64, 30, 52, 38, 1);
    logo_draw_thick_line(64, 30, 76, 38, 1);
    logo_draw_thick_line(64, 43, 57, 51, 1);
    logo_draw_thick_line(64, 43, 71, 51, 1);

    logo_fill_circle(64, 20, 3);
    logo_fill_circle(52, 38, 3);
    logo_fill_circle(76, 38, 3);
    logo_fill_circle(57, 51, 3);
    logo_fill_circle(71, 51, 3);
}

static void render_home_assistant_logo(void)
{
    int y;

    clear_display();
    build_home_assistant_logo();

    /*
     * Render each white run as a short rectangle and explicitly synchronize
     * after each run. The missing synchronization was the reason the prior
     * custom logo could leave the screen black.
     */
    for (y = 0; y < DISPLAY_HEIGHT; y++)
    {
        int x = 0;

        while (x < DISPLAY_WIDTH)
        {
            int start;

            while (
                x < DISPLAY_WIDTH &&
                logo_canvas[(y * DISPLAY_WIDTH) + x] == 0
            )
            {
                x++;
            }

            start = x;

            while (
                x < DISPLAY_WIDTH &&
                logo_canvas[(y * DISPLAY_WIDTH) + x] != 0
            )
            {
                x++;
            }

            if (x > start)
            {
                fill_rectangle_synced(
                    (uint16_t)start,
                    (uint16_t)y,
                    (uint16_t)(x - start),
                    1,
                    ST7735_WHITE
                );
            }
        }
    }

    sync_display();
}

static void render_ip_address(void)
{
    clear_display();
    draw_title("HOST IP");

    lcd_write_string(
        centered_x(host_ip_address, Font_8x16.width),
        27,
        host_ip_address,
        Font_8x16,
        ST7735_WHITE,
        ST7735_BLACK
    );

    sync_display();
}

static int read_cpu_snapshot(
    unsigned long long *total,
    unsigned long long *idle_total
)
{
    FILE *file;
    char line[256];
    unsigned long long user = 0;
    unsigned long long nice = 0;
    unsigned long long system = 0;
    unsigned long long idle = 0;
    unsigned long long iowait = 0;
    unsigned long long irq = 0;
    unsigned long long softirq = 0;
    unsigned long long steal = 0;
    unsigned long long guest = 0;
    unsigned long long guest_nice = 0;
    int values_read;

    file = fopen("/proc/stat", "r");

    if (file == NULL)
    {
        return 0;
    }

    if (fgets(line, sizeof(line), file) == NULL)
    {
        fclose(file);
        return 0;
    }

    fclose(file);

    values_read = sscanf(
        line,
        "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
        &user,
        &nice,
        &system,
        &idle,
        &iowait,
        &irq,
        &softirq,
        &steal,
        &guest,
        &guest_nice
    );

    if (values_read < 4)
    {
        return 0;
    }

    /*
     * guest and guest_nice are already included in user and nice, so they are
     * not added separately.
     */
    *idle_total = idle + iowait;
    *total = user + nice + system + idle + iowait + irq + softirq + steal;

    return 1;
}

static uint8_t read_cpu_usage(void)
{
    unsigned long long first_total;
    unsigned long long first_idle;
    unsigned long long second_total;
    unsigned long long second_idle;
    unsigned long long total_delta;
    unsigned long long idle_delta;
    unsigned long long busy_delta;
    unsigned int percentage;

    if (!read_cpu_snapshot(&first_total, &first_idle))
    {
        return 0;
    }

    usleep(250000);

    if (!read_cpu_snapshot(&second_total, &second_idle))
    {
        return 0;
    }

    total_delta = second_total - first_total;
    idle_delta = second_idle - first_idle;

    if (total_delta == 0 || idle_delta > total_delta)
    {
        return 0;
    }

    busy_delta = total_delta - idle_delta;
    percentage = (unsigned int)((busy_delta * 100U) / total_delta);

    if (percentage > 100U)
    {
        percentage = 100U;
    }

    return (uint8_t)percentage;
}

static uint8_t read_ram_usage(void)
{
    FILE *file;
    char line[256];
    unsigned long long memory_total = 0;
    unsigned long long memory_available = 0;
    unsigned long long memory_free = 0;
    unsigned long long available;
    unsigned long long used;
    unsigned int percentage;

    file = fopen("/proc/meminfo", "r");

    if (file == NULL)
    {
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        if (
            sscanf(line, "MemTotal: %llu kB", &memory_total) == 1
        )
        {
            continue;
        }

        if (
            sscanf(line, "MemAvailable: %llu kB", &memory_available) == 1
        )
        {
            continue;
        }

        if (
            sscanf(line, "MemFree: %llu kB", &memory_free) == 1
        )
        {
            continue;
        }
    }

    fclose(file);

    if (memory_total == 0)
    {
        return 0;
    }

    available = memory_available > 0
        ? memory_available
        : memory_free;

    if (available > memory_total)
    {
        available = memory_total;
    }

    used = memory_total - available;
    percentage = (unsigned int)((used * 100U) / memory_total);

    if (percentage > 100U)
    {
        percentage = 100U;
    }

    return (uint8_t)percentage;
}

static void render_cpu_usage(void)
{
    clear_display();
    draw_title("CPU USAGE");
    draw_percentage_value(read_cpu_usage());
    sync_display();
}

static void render_ram_usage(void)
{
    clear_display();
    draw_title("RAM USAGE");
    draw_percentage_value(read_ram_usage());
    sync_display();
}

static void render_disk_space(void)
{
    clear_display();
    draw_title("DISK SPACE");
    draw_percentage_value(host_disk_usage);
    sync_display();
}

static const char *screen_name(screen_type_t screen)
{
    switch (screen)
    {
        case SCREEN_HOME_ASSISTANT_LOGO:
            return "Home Assistant logo";
        case SCREEN_IP_ADDRESS:
            return "host IP address";
        case SCREEN_CPU_USAGE:
            return "CPU usage";
        case SCREEN_RAM_USAGE:
            return "RAM usage";
        case SCREEN_DISK_SPACE:
            return "host disk space";
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
        else if (
            strcmp(argv[index], "--host-ip") == 0 &&
            index + 1 < argc
        )
        {
            snprintf(
                host_ip_address,
                sizeof(host_ip_address),
                "%s",
                argv[++index]
            );
        }
        else if (
            strcmp(argv[index], "--disk-percent") == 0 &&
            index + 1 < argc
        )
        {
            long requested_percentage = strtol(argv[++index], NULL, 10);

            if (requested_percentage < 0)
            {
                requested_percentage = 0;
            }
            else if (requested_percentage > 100)
            {
                requested_percentage = 100;
            }

            host_disk_usage = (uint8_t)requested_percentage;
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
    printf("Supervisor host IP: %s\n", host_ip_address);
    printf("Supervisor host disk usage: %u%%\n", host_disk_usage);
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
