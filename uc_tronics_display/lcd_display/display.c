/******
 * Static Home Assistant logo display for the UCTRONICS RM0004 160x80 LCD.
 *
 * This version intentionally avoids lcd_draw_image(). Some RM0004 controller
 * revisions do not reliably accept a complete 25.6 KB frame in one operation.
 * Instead, it renders the logo as short horizontal rectangles using the same
 * drawing path as the original CPU/RAM/temperature screens.
 ******/
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include "st7735.h"

#define LCD_WIDTH 160
#define LCD_HEIGHT 80

static uint16_t canvas[LCD_WIDTH * LCD_HEIGHT];

static void set_pixel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT)
    {
        return;
    }

    canvas[(y * LCD_WIDTH) + x] = color;
}

static void fill_circle(int center_x, int center_y, int radius, uint16_t color)
{
    int x;
    int y;

    for (y = -radius; y <= radius; y++)
    {
        for (x = -radius; x <= radius; x++)
        {
            if ((x * x) + (y * y) <= (radius * radius))
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

static void build_home_assistant_logo(void)
{
    const uint16_t blue = ST7735_COLOR565(24, 188, 242);
    const uint16_t white = ST7735_WHITE;
    int index;

    for (index = 0; index < LCD_WIDTH * LCD_HEIGHT; index++)
    {
        canvas[index] = ST7735_BLACK;
    }

    /*
     * Centered Home Assistant house mark.
     * The outline occupies roughly 64x72 pixels on the 160x80 screen.
     */
    draw_thick_line(51, 36, 80, 7, 4, blue);
    draw_thick_line(80, 7, 109, 36, 4, blue);
    draw_thick_line(51, 36, 51, 59, 4, blue);
    draw_thick_line(109, 36, 109, 59, 4, blue);
    draw_thick_line(51, 59, 70, 72, 4, blue);
    draw_thick_line(109, 59, 90, 72, 4, blue);

    /* Connected smart-home nodes inside the house. */
    draw_thick_line(80, 26, 80, 56, 2, white);
    draw_thick_line(80, 38, 65, 46, 2, white);
    draw_thick_line(80, 38, 95, 46, 2, white);
    draw_thick_line(80, 52, 71, 62, 2, white);
    draw_thick_line(80, 52, 89, 62, 2, white);

    fill_circle(80, 26, 4, white);
    fill_circle(65, 46, 4, white);
    fill_circle(95, 46, 4, white);
    fill_circle(71, 62, 4, white);
    fill_circle(89, 62, 4, white);
}

static void render_canvas(void)
{
    int y;

    /*
     * Render each row as runs of identical colors. This keeps I2C writes small
     * and uses lcd_fill_rectangle(), which is known to work on the RM0004.
     */
    for (y = 0; y < LCD_HEIGHT; y++)
    {
        int x = 0;

        while (x < LCD_WIDTH)
        {
            uint16_t color = canvas[(y * LCD_WIDTH) + x];
            int start = x;

            while (
                x < LCD_WIDTH &&
                canvas[(y * LCD_WIDTH) + x] == color
            )
            {
                x++;
            }

            /*
             * The screen was already cleared to black, so skip black runs and
             * draw only the blue and white portions of the logo.
             */
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

int main(void)
{
    if (lcd_begin())
    {
        return 1;
    }

    sleep(1);

    lcd_fill_screen(ST7735_BLACK);
    build_home_assistant_logo();
    render_canvas();

    /*
     * Keep the add-on alive without rotating through IP, CPU, RAM,
     * temperature, or disk status screens.
     */
    while (1)
    {
        sleep(3600);
    }

    return 0;
}
