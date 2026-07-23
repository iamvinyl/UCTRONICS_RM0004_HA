/******
 * Configurable display for the UCTRONICS RM0013 Raspberry Pi 5 rack.
 *
 * The RM0013 uses the UCTRONICS RM0004 display software and its full-color
 * ST7735 160x80 LCD geometry.
 ******/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "st7735.h"

#define DISPLAY_WIDTH 160
#define DISPLAY_HEIGHT 80
#define LOGO_WIDTH 64
#define LOGO_HEIGHT 64
#define LOGO_X ((DISPLAY_WIDTH - LOGO_WIDTH) / 2)
#define LOGO_Y ((DISPLAY_HEIGHT - LOGO_HEIGHT) / 2)
#define MAX_SCREENS 6
#define DEFAULT_SCREEN_DURATION 5
#define MIN_SCREEN_DURATION 2
#define MAX_SCREEN_DURATION 60
#define ENTITY_TEXT_MAX 64
#define DEFAULT_TRANSITION_STEPS 5
#define DEFAULT_TRANSITION_DELAY_MS 40

typedef enum
{
    SCREEN_HOME_ASSISTANT_LOGO,
    SCREEN_IP_ADDRESS,
    SCREEN_CPU_USAGE,
    SCREEN_RAM_USAGE,
    SCREEN_DISK_SPACE,
    SCREEN_ENTITY_STATE
} screen_type_t;

typedef enum
{
    TRANSITION_NONE,
    TRANSITION_SLIDE,
    TRANSITION_FADE
} transition_type_t;

static screen_type_t enabled_screens[MAX_SCREENS];
static int enabled_screen_count = 0;
static unsigned int screen_duration = DEFAULT_SCREEN_DURATION;

static char host_ip_address[64] = "Unavailable";
static uint8_t host_disk_usage = 0;
static char entity_state_file[128] = "/tmp/uctronics_entity_state";
static char entity_label[ENTITY_TEXT_MAX] = "ENTITY";
static char entity_value[ENTITY_TEXT_MAX] = "Unavailable";
static char entity_unit[ENTITY_TEXT_MAX] = "";
static transition_type_t transition_type = TRANSITION_SLIDE;
static unsigned int transition_steps = DEFAULT_TRANSITION_STEPS;
static unsigned int transition_delay_ms = DEFAULT_TRANSITION_DELAY_MS;

/* RGB565 byte buffer used for one exact 64x64 logo transfer. */
static uint8_t logo_image[LOGO_WIDTH * LOGO_HEIGHT * 2];

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

static void clear_display(uint16_t color)
{
    /*
     * lcd_fill_screen() already sends SYNC_REG through the stock driver.
     * Sending a second immediate sync can stall the RM0013 bridge.
     */
    lcd_fill_screen(color);
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

static void draw_title(const char *title, uint16_t accent_color)
{
    lcd_write_string(
        centered_x(title, Font_8x16.width),
        1,
        (char *)title,
        Font_8x16,
        ST7735_WHITE,
        ST7735_BLACK
    );

    fill_rectangle_synced(
        0,
        20,
        DISPLAY_WIDTH,
        3,
        accent_color
    );
}

static void draw_progress_bar(uint8_t percentage, uint16_t accent_color)
{
    const uint16_t start_x = 15;
    const uint16_t start_y = 65;
    const uint16_t segment_width = 10;
    const uint16_t segment_height = 10;
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
                accent_color
            );
        }
        else
        {
            fill_rectangle_synced(
                x,
                start_y,
                segment_width,
                segment_height,
                ST7735_GRAY
            );
        }
    }
}

static void draw_percentage_value(
    uint8_t percentage,
    uint16_t accent_color
)
{
    char value[8];

    if (percentage > 100)
    {
        percentage = 100;
    }

    snprintf(value, sizeof(value), "%u%%", (unsigned int)percentage);

    lcd_write_string(
        centered_x(value, Font_16x26.width),
        30,
        value,
        Font_16x26,
        ST7735_WHITE,
        ST7735_BLACK
    );

    draw_progress_bar(percentage, accent_color);
}

static void logo_set_pixel(int x, int y, uint16_t color)
{
    size_t offset;

    if (x < 0 || x >= LOGO_WIDTH || y < 0 || y >= LOGO_HEIGHT)
    {
        return;
    }

    offset = (size_t)((y * LOGO_WIDTH) + x) * 2;
    logo_image[offset] = (uint8_t)(color >> 8);
    logo_image[offset + 1] = (uint8_t)(color & 0xFF);
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
        int doubled_error;

        logo_fill_circle(x0, y0, radius, color);

        if (x0 == x1 && y0 == y1)
        {
            break;
        }

        /*
         * Preserve the current error value for both axis decisions.
         * Re-evaluating after changing error can prevent one coordinate from
         * advancing and trap the logo builder in an infinite loop.
         */
        doubled_error = 2 * error;

        if (doubled_error >= dy)
        {
            error += dy;
            x0 += sx;
        }

        if (doubled_error <= dx)
        {
            error += dx;
            y0 += sy;
        }
    }
}

static void logo_fill_polygon(
    const int *x_points,
    const int *y_points,
    int point_count,
    uint16_t color
)
{
    int intersections[16];
    int y;
    int index;

    for (y = 0; y < LOGO_HEIGHT; y++)
    {
        int intersection_count = 0;
        int previous = point_count - 1;

        for (index = 0; index < point_count; index++)
        {
            int y1 = y_points[index];
            int y2 = y_points[previous];

            if (
                (y1 < y && y2 >= y) ||
                (y2 < y && y1 >= y)
            )
            {
                intersections[intersection_count++] =
                    x_points[index] +
                    ((y - y1) * (x_points[previous] - x_points[index])) /
                    (y2 - y1);
            }

            previous = index;
        }

        for (index = 0; index < intersection_count - 1; index++)
        {
            int sort_index;

            for (
                sort_index = index + 1;
                sort_index < intersection_count;
                sort_index++
            )
            {
                if (intersections[sort_index] < intersections[index])
                {
                    int temporary = intersections[index];

                    intersections[index] = intersections[sort_index];
                    intersections[sort_index] = temporary;
                }
            }
        }

        for (index = 0; index + 1 < intersection_count; index += 2)
        {
            int x;

            for (
                x = intersections[index];
                x <= intersections[index + 1];
                x++
            )
            {
                logo_set_pixel(x, y, color);
            }
        }
    }
}

static void build_home_assistant_logo(void)
{
    const uint16_t ha_blue = ST7735_COLOR565(24, 188, 242);
    const uint16_t white = ST7735_WHITE;
    const int house_x[] =
    {
        32, 62, 62, 61, 59, 56, 52,
        12, 8, 5, 3, 2, 2
    };
    const int house_y[] =
    {
        2, 30, 52, 56, 59, 61, 62,
        62, 61, 59, 56, 52, 30
    };

    memset(logo_image, 0, sizeof(logo_image));

    /*
     * Filled Home Assistant house mark based on the current logo:
     * cyan house body with rounded lower corners and a white three-node tree.
     */
    logo_fill_polygon(
        house_x,
        house_y,
        sizeof(house_x) / sizeof(house_x[0]),
        ha_blue
    );

    logo_draw_thick_line(32, 17, 32, 63, 3, white);
    logo_draw_thick_line(32, 49, 48, 34, 3, white);
    logo_draw_thick_line(32, 57, 16, 46, 3, white);

    logo_fill_circle(32, 17, 6, white);
    logo_fill_circle(48, 34, 6, white);
    logo_fill_circle(16, 46, 6, white);
}

/*
 * The stock lcd_draw_image() uses:
 *
 *     (height - y) * (width - x)
 *
 * for the transfer size. That is incorrect for any image not drawn at 0,0.
 * This local replacement always sends exactly width * height RGB565 pixels.
 */
static void draw_image_exact(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    uint8_t *data
)
{
    uint32_t byte_count;

    if (
        x >= DISPLAY_WIDTH ||
        y >= DISPLAY_HEIGHT ||
        width == 0 ||
        height == 0 ||
        x + width > DISPLAY_WIDTH ||
        y + height > DISPLAY_HEIGHT
    )
    {
        fprintf(stderr, "Logo image dimensions are outside the LCD area.\n");
        return;
    }

    byte_count = (uint32_t)width * (uint32_t)height * 2U;

    lcd_set_address_window(
        (uint8_t)x,
        (uint8_t)y,
        (uint8_t)(x + width - 1),
        (uint8_t)(y + height - 1)
    );

    /*
     * i2c_burst_transfer() already ends with SYNC_REG in the stock driver.
     * Do not send an additional sync here.
     */
    i2c_burst_transfer(data, byte_count);
}

static void render_home_assistant_logo(void)
{
    printf("Logo stage 1/5: clearing display\n");
    fflush(stdout);

    clear_display(ST7735_BLACK);

    printf("Logo stage 2/5: display cleared\n");
    fflush(stdout);

    build_home_assistant_logo();

    printf("Logo stage 3/5: bitmap built\n");
    printf(
        "Logo stage 4/5: transferring %ux%u RGB565 image at %u,%u (%u bytes)\n",
        (unsigned int)LOGO_WIDTH,
        (unsigned int)LOGO_HEIGHT,
        (unsigned int)LOGO_X,
        (unsigned int)LOGO_Y,
        (unsigned int)sizeof(logo_image)
    );
    fflush(stdout);

    draw_image_exact(
        LOGO_X,
        LOGO_Y,
        LOGO_WIDTH,
        LOGO_HEIGHT,
        logo_image
    );

    printf("Logo stage 5/5: transfer complete\n");
    fflush(stdout);
}

static void render_ip_address(void)
{
    const uint16_t accent = ST7735_CYAN;

    clear_display(ST7735_BLACK);
    draw_title("HOST IP", accent);

    lcd_write_string(
        centered_x(host_ip_address, Font_11x18.width),
        35,
        host_ip_address,
        Font_11x18,
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
        if (sscanf(line, "MemTotal: %llu kB", &memory_total) == 1)
        {
            continue;
        }

        if (sscanf(line, "MemAvailable: %llu kB", &memory_available) == 1)
        {
            continue;
        }

        if (sscanf(line, "MemFree: %llu kB", &memory_free) == 1)
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
    const uint16_t accent = ST7735_GREEN;

    clear_display(ST7735_BLACK);
    draw_title("CPU USAGE", accent);
    draw_percentage_value(read_cpu_usage(), accent);
    sync_display();
}

static void render_ram_usage(void)
{
    const uint16_t accent = ST7735_YELLOW;

    clear_display(ST7735_BLACK);
    draw_title("RAM USAGE", accent);
    draw_percentage_value(read_ram_usage(), accent);
    sync_display();
}

static void render_disk_space(void)
{
    const uint16_t accent = ST7735_BLUE;

    clear_display(ST7735_BLACK);
    draw_title("DISK SPACE", accent);
    draw_percentage_value(host_disk_usage, accent);
    sync_display();
}

static void trim_line(char *text)
{
    size_t length = strlen(text);
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r'))
    {
        text[--length] = '\0';
    }
}

static void read_entity_state_file(void)
{
    FILE *file = fopen(entity_state_file, "r");
    if (file == NULL)
    {
        snprintf(entity_label, sizeof(entity_label), "%s", "ENTITY");
        snprintf(entity_value, sizeof(entity_value), "%s", "Unavailable");
        entity_unit[0] = '\0';
        return;
    }

    if (fgets(entity_label, sizeof(entity_label), file) == NULL)
        snprintf(entity_label, sizeof(entity_label), "%s", "ENTITY");
    if (fgets(entity_value, sizeof(entity_value), file) == NULL)
        snprintf(entity_value, sizeof(entity_value), "%s", "Unavailable");
    if (fgets(entity_unit, sizeof(entity_unit), file) == NULL)
        entity_unit[0] = '\0';
    fclose(file);
    trim_line(entity_label); trim_line(entity_value); trim_line(entity_unit);
}

static void fit_text(char *destination, size_t destination_size, const char *source, size_t maximum_characters)
{
    size_t length = strlen(source);
    if (length <= maximum_characters)
        snprintf(destination, destination_size, "%s", source);
    else if (maximum_characters > 3)
    {
        snprintf(destination, destination_size, "%.*s...", (int)(maximum_characters - 3), source);
    }
    else
        snprintf(destination, destination_size, "%.*s", (int)maximum_characters, source);
}

static void render_entity_state(void)
{
    const uint16_t accent = ST7735_CYAN;
    char title[21];
    char combined[80];
    char value[20];

    read_entity_state_file();
    fit_text(title, sizeof(title), entity_label, 20);
    if (entity_unit[0] != '\0')
        snprintf(combined, sizeof(combined), "%s %s", entity_value, entity_unit);
    else
        snprintf(combined, sizeof(combined), "%s", entity_value);
    fit_text(value, sizeof(value), combined, 14);

    clear_display(ST7735_BLACK);
    draw_title(title, accent);
    lcd_write_string(centered_x(value, Font_11x18.width), 38, value, Font_11x18, ST7735_WHITE, ST7735_BLACK);
    sync_display();
}

static void draw_fade_mask(unsigned int visible_level, unsigned int levels)
{
    unsigned int x, y;
    if (levels == 0 || visible_level >= levels) return;
    for (y = 0; y < DISPLAY_HEIGHT; y += 4)
    {
        for (x = 0; x < DISPLAY_WIDTH; x += 4)
        {
            unsigned int pattern = ((x / 4) + (y / 4) * 3) % levels;
            if (pattern >= visible_level)
                lcd_fill_rectangle((uint16_t)x, (uint16_t)y, 4, 4, ST7735_BLACK);
        }
    }
    sync_display();
}

static void fade_out(void)
{
    unsigned int step;
    for (step = transition_steps; step > 0; step--)
    {
        draw_fade_mask(step - 1, transition_steps);
        usleep(transition_delay_ms * 1000U);
    }
    clear_display(ST7735_BLACK);
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
        case SCREEN_ENTITY_STATE:
            return "entity state";
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
        case SCREEN_ENTITY_STATE:
            render_entity_state();
            break;
        default:
            render_home_assistant_logo();
            break;
    }
}

static void render_with_transition(screen_type_t screen, int is_first_screen)
{
    unsigned int step;

    if (!is_first_screen && transition_type == TRANSITION_FADE)
        fade_out();

    if (transition_type == TRANSITION_FADE)
    {
        for (step = 1; step <= transition_steps; step++)
        {
            render_screen(screen);
            draw_fade_mask(step, transition_steps);
            usleep(transition_delay_ms * 1000U);
        }
    }
    else
    {
        /* The controller's normal full-frame transfer produces the slide reveal. */
        render_screen(screen);
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
        else if (strcmp(argv[index], "--entity") == 0)
        {
            add_screen(SCREEN_ENTITY_STATE);
        }
        else if (strcmp(argv[index], "--entity-file") == 0 && index + 1 < argc)
        {
            snprintf(entity_state_file, sizeof(entity_state_file), "%s", argv[++index]);
        }
        else if (strcmp(argv[index], "--transition") == 0 && index + 1 < argc)
        {
            const char *requested = argv[++index];
            if (strcmp(requested, "none") == 0) transition_type = TRANSITION_NONE;
            else if (strcmp(requested, "fade") == 0) transition_type = TRANSITION_FADE;
            else transition_type = TRANSITION_SLIDE;
        }
        else if (strcmp(argv[index], "--transition-steps") == 0 && index + 1 < argc)
        {
            long value = strtol(argv[++index], NULL, 10);
            if (value < 2) value = 2;
            if (value > 8) value = 8;
            transition_steps = (unsigned int)value;
        }
        else if (strcmp(argv[index], "--transition-delay-ms") == 0 && index + 1 < argc)
        {
            long value = strtol(argv[++index], NULL, 10);
            if (value < 20) value = 20;
            if (value > 500) value = 500;
            transition_delay_ms = (unsigned int)value;
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
    int is_first_screen = 1;

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
        render_with_transition(enabled_screens[current_screen], is_first_screen);
        is_first_screen = 0;
        sleep(screen_duration);

        current_screen++;

        if (current_screen >= enabled_screen_count)
        {
            current_screen = 0;
        }
    }

    return 0;
}
