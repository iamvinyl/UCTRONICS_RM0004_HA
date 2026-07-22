/******
 * Static Home Assistant logo display for the UCTRONICS RM0004 160x80 LCD.
 ******/
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include "st7735.h"

#define LCD_WIDTH 160
#define LCD_HEIGHT 80

static uint8_t framebuffer[LCD_WIDTH * LCD_HEIGHT * 2];

static void set_pixel(int x, int y, uint16_t color)
{
    size_t offset;

    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT)
    {
        return;
    }

    offset = (size_t)(y * LCD_WIDTH + x) * 2;
    framebuffer[offset] = (uint8_t)(color >> 8);
    framebuffer[offset + 1] = (uint8_t)(color & 0xFF);
}

static void fill_circle(int center_x, int center_y, int radius, uint16_t color)
{
    int x;
    int y;

    for (y = -radius; y <= radius; y++)
    {
        for (x = -radius; x <= radius; x++)
        {
            if ((x * x) + (y * y) <= radius * radius)
            {
                set_pixel(center_x + x, center_y + y, color);
            }
        }
    }
}

static void draw_thick_line(
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
        fill_circle(x0, y0, radius, color);

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

static void draw_home_assistant_logo(void)
{
    const uint16_t blue = ST7735_COLOR565(24, 188, 242);
    const uint16_t white = ST7735_WHITE;
    size_t index;

    /* Clear the complete 160x80 framebuffer to black. */
    for (index = 0; index < sizeof(framebuffer); index += 2)
    {
        framebuffer[index] = 0;
        framebuffer[index + 1] = 0;
    }

    /* Home Assistant house outline. */
    draw_thick_line(51, 36, 80, 8, 4, blue);
    draw_thick_line(80, 8, 109, 36, 4, blue);
    draw_thick_line(51, 36, 51, 60, 4, blue);
    draw_thick_line(109, 36, 109, 60, 4, blue);
    draw_thick_line(51, 60, 70, 72, 4, blue);
    draw_thick_line(109, 60, 90, 72, 4, blue);

    /* Connected smart-home nodes. */
    draw_thick_line(80, 27, 80, 57, 2, white);
    draw_thick_line(80, 38, 65, 46, 2, white);
    draw_thick_line(80, 38, 95, 46, 2, white);
    draw_thick_line(80, 53, 71, 62, 2, white);
    draw_thick_line(80, 53, 89, 62, 2, white);

    fill_circle(80, 27, 4, white);
    fill_circle(65, 46, 4, white);
    fill_circle(95, 46, 4, white);
    fill_circle(71, 62, 4, white);
    fill_circle(89, 62, 4, white);

    lcd_draw_image(0, 0, LCD_WIDTH, LCD_HEIGHT, framebuffer);
}

int main(void)
{
    if (lcd_begin())
    {
        return 1;
    }

    sleep(1);
    draw_home_assistant_logo();

    /*
     * Keep the add-on alive without redrawing the LCD or rotating through
     * IP, CPU, RAM, temperature, and disk status screens.
     */
    while (1)
    {
        sleep(3600);
    }

    return 0;
}
