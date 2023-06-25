#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico.h"
#include "pico/audio_i2s.h"
#include "pico/float.h"
#include "pico/multicore.h"
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#include "pico/stdlib.h"
#include "pico/sync.h"

#include "../data/audio.h"

#define vga_mode vga_mode_160x120_60
#define VIDEO_W 160
#define VIDEO_H 120
#define VIDEO_W_2 80
#define VIDEO_H_2 60
#define SAMPLES_PER_BUFFER 256

static void frame_prologue();

typedef enum effect_e
{
    EFFECT_START = 0,
    EFFECT_WATER,
    EFFECT_ACID,
    EFFECT_3D_B,
    EFFECT_FIRE_A,
    EFFECT_FRACTAL,
    EFFECT_FIRE_B,
    EFFECT_FIRE_C,
    EFFECT_END,
} effect_et;

static struct audio_buffer_pool *audio_buffer_pool;
static bool frame_prologue_done; /* After frame ended, we prepared state for
                                    next frame. */
/* Current y coordinate in frame. */
static uint32_t y = 0;
/* Frame count since start. */
static uint32_t frame = 0;
/* Frames until end of effect. */
static uint32_t frame_rem = 10;
/* Current played effect. */
static effect_et effect = EFFECT_START;
/* Current color palette. */
static uint8_t palette = 0;
/* A palette with all colors used in the demo. */
static uint16_t palette_list[6][255] = {};
/* The framebuf that will be written to screen. It is a composition of
 * backghround then foreground. */
static uint16_t vid[VIDEO_H][VIDEO_W] = {};
/* Hidden buffer that will be the background. */
static uint8_t bg[VIDEO_H][VIDEO_W] = {};
static uint8_t *const bg_idx = (uint8_t *const)bg;
/* Hidden buffer that will be the foreground. */
static uint8_t fg[VIDEO_H][VIDEO_W] = {};
static uint8_t *const fg_idx = (uint8_t *const)fg;

static int8_t const vertex_cube[][3][3] = {
    {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}}, {{0, 0, 0}, {1, 1, 0}, {1, 0, 0}},
    {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, {{1, 0, 0}, {1, 1, 1}, {1, 0, 1}},
    {{1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {{1, 0, 1}, {0, 1, 1}, {0, 0, 1}},
    {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, {{0, 0, 1}, {0, 1, 0}, {0, 0, 0}},
    {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}}, {{0, 1, 0}, {1, 1, 1}, {1, 1, 0}},
    {{1, 0, 1}, {0, 0, 1}, {0, 0, 0}}, {{1, 0, 1}, {0, 0, 0}, {1, 0, 0}},
};
static uint8_t const vertex_cube_count =
    sizeof(vertex_cube) / sizeof(vertex_cube[0]);
static int16_t vertex_gem[10][3][3];
static uint8_t const vertex_gem_count =
    sizeof(vertex_gem) / sizeof(vertex_gem[0]);

static void vertex_gem_create()
{
    static float const angle = 2.0f * M_PI / 5.0f;
    for (uint8_t point_i = 0; point_i < 5; ++point_i)
    {
        float const a = (float)point_i * angle;
        float const b = (float)(point_i + 1 % 5) * angle;

        float a_sin, a_cos;
        sincosf(a, &a_sin, &a_cos);

        float b_sin, b_cos;
        sincosf(b, &b_sin, &b_cos);

        int16_t const x1 = (int16_t)(a_sin * 128.0f);
        int16_t const x2 = (int16_t)(b_sin * 128.0f);
        int16_t const y1 = (int16_t)(a_cos * 128.0f);
        int16_t const y2 = (int16_t)(b_cos * 128.0f);

        int16_t const vertex_a[3][3] = {{x1, y1, 0}, {x2, y2, 0}, {0, 0, 128}};
        int16_t const vertex_b[3][3] = {{x1, y1, 0}, {x2, y2, 0}, {0, 0, -128}};
        memcpy(vertex_gem[point_i], vertex_a, sizeof(vertex_a));
        memcpy(vertex_gem[point_i + 5], vertex_b, sizeof(vertex_b));
    }
}

static void palette_create()
{
    for (uint8_t palette_i = 0; palette_i < 6; ++palette_i)
    {
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        for (uint8_t color_i = 0; color_i < 255; color_i++)
        {
            switch (palette_i)
            {
            case 0: {
                if (color_i < 126)
                {
                    r += 1;
                }
                else if (color_i < 188)
                {
                    g += 2;
                }
                else if (color_i < 250)
                {
                    b += 2;
                }

                palette_list[0][color_i] =
                    PICO_SCANVIDEO_PIXEL_FROM_RGB8(r * 2, g * 2, b * 2);
                break;
            }
            case 1: {
                g += 2;
                b += 2;
                palette_list[1][color_i] =
                    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0, g, b);
                break;
            }
            case 2: {
                g += 2;
                palette_list[2][color_i] =
                    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0, g * 2, 0);
                break;
            }
            case 3: {
                r += 1;
                b += 1;
                palette_list[3][color_i] =
                    PICO_SCANVIDEO_PIXEL_FROM_RGB8(r + 20, 0, b + 20);
                break;
            }
            case 4: {
                if (color_i < 64)
                {
                    r += 3;
                }
                g += 2;

                palette_list[4][color_i] =
                    PICO_SCANVIDEO_PIXEL_FROM_RGB8(r, g, 40);
                break;
            }
            case 5: {
                g += 2;
                b += 1;

                palette_list[5][color_i] =
                    PICO_SCANVIDEO_PIXEL_FROM_RGB8(0, g, b);
                break;
            }
            default:
                __builtin_unreachable();
            }
        }
    }
}

#define FONT_W 3U
#define FONT_H 5U

/* 3x5 matrix display font. */
static uint16_t const font_alpha[] = {
    0x5BEF, /* A */
    0x7AEF, /* B */
    0x724F, /* C */
    0x3B6B, /* D */
    0x72CF, /* E */
    0x12CF, /* F */
    0x7B4F, /* G */
    0x5BED, /* H */
    0x7497, /* I */
    0x7B26, /* J */
    0x5AED, /* K */
    0x7249, /* L */
    0x5B7D, /* M */
    0x5B6F, /* N */
    0x7B6F, /* O */
    0x13EF, /* P */
    0x4F6F, /* Q */
    0x5AEF, /* R */
    0x79CF, /* S */
    0x2497, /* T */
    0x7B6D, /* U */
    0x176D, /* V */
    0x5F6D, /* W */
    0x5AAD, /* X */
    0x24AD, /* Y */
    0x72A7, /* Z */
};
static uint16_t const font_numeric[] = {
    0x7B6F, /* 0 */
    0x4926, /* 1 */
    0x73E7, /* 2 */
    0x79A7, /* 3 */
    0x49ED, /* 4 */
    0x79CF, /* 5 */
    0x7BCF, /* 6 */
    0x4927, /* 7 */
    0x7BEF, /* 8 */
    0x79EF, /* 9 */
};

static void text_draw(uint16_t const _y, uint16_t const x, uint8_t const scale,
                      char const *const text, uint16_t const len,
                      uint16_t const color)
{
    char ch;
    uint16_t offset_x = 0U;
    uint16_t offset_y = 0U;
    for (uint16_t text_idx = 0; text_idx < len; ++text_idx)
    {
        ch = text[text_idx];

        uint32_t font_glyph;
        if (ch >= '0' && ch <= '9')
        {
            ch -= '0';
            font_glyph = font_numeric[(uint8_t)ch];
        }
        else if (ch >= 'A' && ch <= 'Z')
        {
            ch -= 'A';
            font_glyph = font_alpha[(uint8_t)ch];
        }
        else if (ch == ' ')
        {
            font_glyph = 0x0000;
        }
        else if (ch == '\r')
        {
            offset_x = 0U;
            text_idx += 1U;
            continue;
        }
        else if (ch == '\n')
        {
            offset_y += (FONT_H * (scale + 1U));
            text_idx += 1U;
            continue;
        }
        else if (ch == '-')
        {
            font_glyph = 0x01C0;
        }
        else if (ch == '+')
        {
            font_glyph = 0x05D0;
        }
        else if (ch == ':')
        {
            font_glyph = 0x0410;
        }
        else if (ch == '=')
        {
            font_glyph = 0x0E38;
        }
        else if (ch == '.')
        {
            font_glyph = 0x2000;
        }
        else if (ch == '!')
        {
            font_glyph = 0x2092;
        }
        else
        {
            font_glyph = 0x7B6F;
        }

        for (uint8_t i = 0U; i < 15U; ++i)
        {
            uint8_t const seg = font_glyph & 0x00000001;
            uint8_t const draw = seg ? 255U : 0U;
            font_glyph >>= 1;

            for (uint8_t scale_y = 0U; scale_y < scale; ++scale_y)
            {
                for (uint8_t scale_x = 0U; scale_x < scale; ++scale_x)
                {
                    uint16_t const x_common = x + offset_x + scale_x;
                    uint16_t const y_common = _y + offset_y + scale_y;
                    if (draw)
                    {
                        fg[y_common + ((i / 3U) * scale)]
                          [x_common + ((i % 3U) * scale)] = color;
                    }
                    else
                    {
                        if (draw)
                        {
                            fg[y_common + ((i / 3U) * scale)]
                              [x_common + ((i % 3U) * scale)] = 0;
                        }
                    }
                }
            }
        }

        offset_x += (FONT_W + 2U) * scale;
    }
}

static uint16_t yx_to_idx(int16_t const _y, int16_t const _x)
{
    static uint16_t const idx_last = (VIDEO_H * VIDEO_W) - 1;
    int16_t const idx = (_y * VIDEO_W) + _x;
    if (idx < 0)
    {
        return (VIDEO_W - 1) + _x;
    }
    else if (idx > idx_last)
    {
        return idx_last;
    }
    else
    {
        return idx;
    }
}

static void fractal(uint32_t const frame_rel, uint16_t const _y,
                    uint16_t const _x)
{
    static float const real_c = -0.7f;
    static uint8_t const iteration_max = 32;
    // static float const zoom = 1.2f;

    float const imaginary_c = 0.27f - (0.004f * frame_rel);

    /* Calculate the initial real and imaginary part of z, based on the pixel
     * location and zoom and position values. */
    float real_new =
        1.5f * ((float)_x - (float)VIDEO_W_2) / (96.0 /* zoom * VIDEO_W_2 */);
    float imaginary_new =
        ((float)_y - (float)VIDEO_H_2) / (72.0 /* zoom * VIDEO_H_2 */);

    uint8_t iteration;
    for (iteration = 0; iteration < iteration_max; iteration++)
    {
        // Remember value of previous iteration.
        float const real_old = real_new;
        float const imaginary_old = imaginary_new;
        // The actual iteration, the real and imaginary part are calculated.
        real_new = real_old * real_old - imaginary_old * imaginary_old + real_c;
        imaginary_new = 2.0f * real_old * imaginary_old + imaginary_c;
        // If the point is outside the circle with radius 2: stop.
        if ((real_new * real_new + imaginary_new * imaginary_new) > 1.5f)
        {
            break;
        }
    }

    uint8_t const color = iteration_max - iteration;
    bg[_y][_x] = color * 2;
}

static void energy_transfer(uint16_t const idx_src, uint16_t const idx_dst)
{
    uint8_t const transfer_amount = bg_idx[idx_src] / 2;
    uint16_t const transfer_max = (uint16_t)256 - (uint16_t)bg_idx[idx_dst];
    if (transfer_max > transfer_amount)
    {
        bg_idx[idx_src] -= transfer_amount;
        bg_idx[idx_dst] = bg_idx[idx_dst] + transfer_amount > 255
                              ? 0
                              : bg_idx[idx_dst] + transfer_amount;
    }
    else
    {
        bg_idx[idx_src] -= transfer_max > 255 ? 255 : transfer_max;
        bg_idx[idx_dst] = bg_idx[idx_dst] + transfer_max > 255
                              ? 0
                              : bg_idx[idx_dst] + transfer_max;
    }
}

static void fire(effect_et const _effect, uint16_t const _y, uint16_t const _x)
{
    uint32_t const idx_self = yx_to_idx(_y, _x);

    switch (_effect)
    {
    case EFFECT_FIRE_A:
    case EFFECT_FIRE_B: {
        if (_y == 0 && _x == 0)
        {
            for (uint16_t __x = 0; __x < VIDEO_W; ++__x)
            {
                bg[VIDEO_H - 1][__x] = 255;
            }
        }
        break;
    }
    case EFFECT_ACID:
    case EFFECT_WATER: {
        if (frame % 2 == 0)
        {
            bg[_y][_x] = (bg[_y][_x] + 2) % 255;
        }
        break;
    }
    default:
        __builtin_unreachable();
    }

    /* Cache current value to not have to re-read buffer. */
    uint8_t const val = bg_idx[yx_to_idx(_y, _x)];

    int8_t const moore[] = {-1, -1, -1, 0, 1, 1, 1, 0, -1, -1};
    uint8_t const moore_north = _effect == EFFECT_ACID ? 7 : 1;
    uint8_t neighbor_min = 9;
    uint8_t neighbor_val_min = val;
    uint8_t neighbor_val_tot = 0;

    for (uint8_t neighbor = 0; neighbor < 8; ++neighbor)
    {
        int16_t const neighbor_x = _x + moore[(neighbor + 2) % 9];
        int16_t const neighbor_y = _y + moore[neighbor];

        uint16_t const neighbor_idx = yx_to_idx(neighbor_y, neighbor_x);
        uint8_t const neighbor_val = bg_idx[neighbor_idx];
        neighbor_val_tot += neighbor_val;

        if (neighbor_val < neighbor_val_min)
        {
            neighbor_min = neighbor;
            neighbor_val_min = neighbor_val;
        }
    }

    /* Try to transfer energy. */
    if (neighbor_val_min < val)
    {
        int16_t const neighbor_min_x = _x + moore[(neighbor_min + 1) % 9];
        int16_t const neighbor_min_y = _y + moore[neighbor_min % 9];
        uint16_t const neighbor_min_idx =
            yx_to_idx(neighbor_min_y, neighbor_min_x);

        energy_transfer(idx_self, neighbor_min_idx);
        /* We don't update the `val` value here intentionally for a better
         * effect, even though it changed. */
    }

    // Cool down places that have a cooler neighborhood.
    if (neighbor_val_tot / 8 < val && bg_idx[idx_self] > 0)
    {
        bg_idx[idx_self] -= 1;
    }

    // Transfer up due to convection.
    int16_t const neighbor_north_x = _x + moore[(moore_north + 1) % 9];
    int16_t neighbor_north_y = _y + moore[moore_north];

    if (val > 32)
    {
        uint16_t const neighbor_north_idx =
            yx_to_idx(neighbor_north_y, neighbor_north_x);
        energy_transfer(idx_self, neighbor_north_idx);
    }
    if (val > 128)
    {
        neighbor_north_y -= 1;
        uint16_t const neighbor_north_idx =
            yx_to_idx(neighbor_north_y, neighbor_north_x);
        uint16_t const idx_south = yx_to_idx(_y + 1, _x);
        if (_y + 1 < VIDEO_H)
        {
            energy_transfer(idx_south, neighbor_north_idx);
        }
    }
}

static void matrix_mult(float *const o, float const i[3], float const m[4][4])
{
    float const x =
        (i[0] * m[0][0]) + (i[1] * m[1][0]) + (i[2] * m[2][0] + m[3][0]);
    float const _y =
        (i[0] * m[0][1]) + (i[1] * m[1][1]) + (i[2] * m[2][1] + m[3][1]);
    float const z =
        (i[0] * m[0][2]) + (i[1] * m[1][2]) + (i[2] * m[2][2] + m[3][2]);
    float const w =
        (i[0] * m[0][3] + i[1] * m[1][3] + i[2] * m[2][3] + m[3][3]);
    if (w != 0.0f)
    {
        o[0] = x / w;
        o[1] = _y / w;
        o[2] = z / w;
    }
    else
    {
        o[0] = x;
        o[1] = _y;
        o[2] = z;
    }
}

static void bresenham(uint16_t const color, uint32_t x0, uint32_t y0,
                      uint32_t x1, uint32_t y1)
{
    int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy, e2; /* error value e_xy */

    for (;;)
    {
        uint16_t const idx = yx_to_idx(y0, x0);
        fg_idx[idx] = color;

        if (x0 == x1 && y0 == y1)
        {
            break;
        }
        e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        } /* e_xy+e_x > 0 */
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        } /* e_xy+e_y < 0 */
    }
}

inline static void triangle(uint16_t const tri[3][2], uint16_t const color)
{
    bresenham(color, tri[0][0], tri[0][1], tri[1][0], tri[1][1]);
    bresenham(color, tri[1][0], tri[1][1], tri[2][0], tri[2][1]);
    bresenham(color, tri[2][0], tri[2][1], tri[0][0], tri[0][1]);
}

static void threedee(effect_et const _effect, uint32_t const _frame,
                     uint8_t const shape, uint16_t const color,
                     uint16_t const _y, uint16_t const _x)
{
    if (_y == 0 && _x == 0)
    {
        static float const near_clip = 1.0f;
        static float const far_clip = 100.0f;
        static float const fov_rad = 1.0f;
        static float const aspect_ratio = (float)VIDEO_H / (float)VIDEO_W;
        static float const projection[4][4] = {
            {aspect_ratio * fov_rad, 0, 0, 0},
            {0, fov_rad, 0, 0},
            {0, 0, far_clip / (far_clip - near_clip), 1.0f},
            {0, 0, (-far_clip * near_clip) / (far_clip - near_clip), 0}};

        float rot_z[4][4] = {
            {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
        float rot_x[4][4] = {
            {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};

        float const theta = _frame / 8.0f;

        for (uint16_t __y = 0; __y < VIDEO_H; ++__y)
        {
            for (uint16_t __x = 0; __x < VIDEO_W; ++__x)
            {
                fg[__y][__x] /= 2;
            }
        }

        float theta_sin, theta_cos;
        sincosf(theta, &theta_sin, &theta_cos);

        float theta_half_sin, theta_half_cos;
        sincosf(theta * 0.5f, &theta_half_sin, &theta_half_cos);

        rot_z[0][0] = theta_cos;
        rot_z[0][1] = theta_sin;
        rot_z[1][0] = -theta_sin;
        rot_z[1][1] = theta_cos;
        rot_z[2][2] = 1;
        rot_z[3][3] = 1;
        if (_effect == EFFECT_3D_B || _effect == EFFECT_FIRE_C)
        {
            rot_z[0][1] = -theta_half_sin;
        }

        rot_x[0][0] = 1;
        rot_x[1][1] = theta_half_cos;
        rot_x[1][2] = theta_half_sin;
        rot_x[2][1] = -theta_half_sin;
        rot_x[2][2] = theta_half_cos;
        rot_x[3][3] = 1;

        for (uint8_t tri_i = 0;
             tri_i < (shape == 0 ? vertex_cube_count : vertex_gem_count);
             ++tri_i)
        {
            float tri[3][3];
            for (uint8_t i = 0; i < 3; ++i)
            {
                for (uint8_t j = 0; j < 3; ++j)
                {
                    switch (shape)
                    {
                    case 0:
                        tri[i][j] = (float)vertex_cube[tri_i][i][j];
                        break;
                    case 1:
                        tri[i][j] = (float)vertex_gem[tri_i][i][j] / 128.0f;
                        break;
                    default:
                        __builtin_unreachable();
                    }
                }
            }

            float tri_project[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
            float tri_translate[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
            float tri_rotate_z[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
            float tri_rotate_zx[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

            // Rotate in Z-Axis
            matrix_mult(tri_rotate_z[0], tri[0], rot_z);
            matrix_mult(tri_rotate_z[1], tri[1], rot_z);
            matrix_mult(tri_rotate_z[2], tri[2], rot_z);

            // Rotate in X-Axis
            matrix_mult(tri_rotate_zx[0], tri_rotate_z[0], rot_x);
            matrix_mult(tri_rotate_zx[1], tri_rotate_z[1], rot_x);
            matrix_mult(tri_rotate_zx[2], tri_rotate_z[2], rot_x);

            // Offset into the screen
            memcpy(tri_translate, tri_rotate_zx, sizeof(tri_translate));
            tri_translate[0][2] = tri_rotate_zx[0][2] + 3.0f;
            tri_translate[1][2] = tri_rotate_zx[1][2] + 3.0f;
            tri_translate[2][2] = tri_rotate_zx[2][2] + 3.0f;

            // Project triangles from 3D --> 2D
            matrix_mult(tri_project[0], tri_translate[0], projection);
            matrix_mult(tri_project[1], tri_translate[1], projection);
            matrix_mult(tri_project[2], tri_translate[2], projection);

            // Scale into view
            float const video_w_half = (float)VIDEO_W_2;
            float const video_h_half = (float)VIDEO_H_2;
            tri_project[0][0] += 1;
            tri_project[0][1] += 1;
            tri_project[1][0] += 1;
            tri_project[1][1] += 1;
            tri_project[2][0] += 1;
            tri_project[2][1] += 1;
            tri_project[0][0] *= video_w_half;
            tri_project[0][1] *= video_h_half;
            tri_project[1][0] *= video_w_half;
            tri_project[1][1] *= video_h_half;
            tri_project[2][0] *= video_w_half;
            tri_project[2][1] *= video_h_half;

            uint16_t tri_draw[3][2] = {{0, 0}, {0, 0}, {0, 0}};
            float theta_1p4_sin, theta_1p4_cos;
            sincosf(theta * 1.4f, &theta_1p4_sin, &theta_1p4_cos);
            float const move_x = theta_sin * 35.0f;
            float const move_y = theta_1p4_cos * 20.0f;
            for (uint8_t tri_row_i = 0; tri_row_i < 3; ++tri_row_i)
            {
                tri_draw[tri_row_i][0] =
                    (uint16_t)(tri_project[tri_row_i][0] + move_x);
                tri_draw[tri_row_i][1] =
                    (uint16_t)(tri_project[tri_row_i][1] + move_y);
            }
            triangle(tri_draw, color);
        }
    }
}

static void chess(uint32_t const _frame, uint16_t const _y, uint16_t const _x)
{
    const uint32_t delta = _frame / 2;
    bg[_y][_x] = (((_x + delta)) ^ (_y + delta)) - 1;
}

static void plasma(uint32_t const _frame, uint16_t const _y, uint16_t const _x)
{
    float const time = _frame / 64.0f;
    float const dy = (float)_y / VIDEO_H;
    float const dx = (float)_x / VIDEO_W;
    float v = sinf(dx * 10.0f + time);
    float time3_sin, time3_cos;
    sincosf(time / 3.0f, &time3_sin, &time3_cos);
    float const cx = dx + time3_sin;
    float const cy = dy + time3_cos;
    v += sinf(sqrtf(50.0f * (cx * cx + cy * cy) + 1.0f + time));
    v += cosf(sqrtf(dx * dx + dy * dy) - time);
    float vpi_sin, vpi_cos;
    sincosf(v * M_PI, &vpi_sin, &vpi_cos);
    uint8_t const r = (uint8_t)(vpi_sin * 64.0f);
    uint8_t const b = (uint8_t)(vpi_cos * 64.0f);
    uint8_t const color = (r + b) % 255;

    for (uint8_t i = 0; i < 4; ++i)
    {
        for (uint8_t j = 0; j < 4; ++j)
        {
            bg[_y + i][_x + j] = color;
        }
    }
}

static void draw(effect_et const _effect, uint32_t const _frame,
                 uint16_t const _y, uint16_t const _x)
{
    switch (_effect)
    {
    case EFFECT_START:
        break;
    case EFFECT_FIRE_A:
        fire(_effect, _y, _x);
        threedee(_effect, _frame, 0, 210, _y, _x);
        break;
    case EFFECT_FIRE_C:
    case EFFECT_FIRE_B:
        if (_y % 4 == 0 && _x % 4 == 0)
        {
            plasma(_frame, _y, _x);
        }
        threedee(_effect, _frame, 1, 210, _y, _x);
        break;
    case EFFECT_WATER:
        fire(_effect, _y, _x);
        if (_y == 0 && _x == 0)
        {
            char const txt[] = "EGO SUM PICO";
            if (_frame % 4 == 0 && _frame >= 120 && _frame <= 144)
            {
                uint8_t const txt_idx = (_frame - 120) / 4;
                text_draw(50, 25 + txt_idx * 9, 2, &txt[txt_idx], 1, 250);
            }
            if (_frame == 160)
            {
                text_draw(50, 97, 2, &txt[8], 4, 250);
            }
        }
        break;
    case EFFECT_ACID:
        fire(_effect, _y, _x);
        break;
    case EFFECT_FRACTAL:
        if (_y % 2 == 0 && _x % 2 == 0)
        {
            /* The offset here is the sum of frame durations from start till
             * this effect. */
            fractal((_frame - 1136) + 110, _y, _x);
            uint16_t const color = bg[_y][_x];
            bg[_y][_x + 1] = color;
            bg[_y + 1][_x] = color;
            bg[_y + 1][_x + 1] = color;
        }
        break;
    case EFFECT_3D_B:
        chess(_frame, _y, _x);
        if (_frame % 4 == 0)
        {
            threedee(_effect, _frame / 4, 1, 254, _y, _x);
        }
        break;
    case EFFECT_END:
        if (_y % 4 == 0 && _x % 4 == 0)
        {
            plasma(_frame, _y, _x);
        }
        if (_y == 0 && _x == 0)
        {
            text_draw(36, 46, 1, "DECRUNCH  2023", 14, 128);
            text_draw(44, 46, 1, "     WILD     ", 14, 128);
            text_draw(52, 46, 1, "   RPI PICO   ", 14, 128);

            text_draw(64, 46, 1, " CODE: 1935711", 14, 128);
            text_draw(72, 46, 1, "MUSIC: EIGHTBM", 14, 128);
        }
        break;
    }
}

static void scanline(effect_et const _effect, uint8_t const _palette,
                     uint32_t const _frame, uint16_t const _y)
{
    for (int x = 0; x < VIDEO_W; ++x)
    {
        draw(_effect, _frame, (_y + 3) % VIDEO_H, x);
        vid[_y][x] = palette_list[_palette][bg[_y][x]];
        vid[_y][x] |= palette_list[_palette][fg[_y][x]];
    }
}

/* "Worker thread" for each core. */
// void __time_critical_func(render_loop)()
static void render_loop()
{
    while (true)
    {
        if (y == VIDEO_H)
        {
            frame_prologue_done = false;
            frame_prologue(); // Start next frame.
        }
        uint16_t const _y = y++;
        uint32_t const _frame = frame;
        effect_et const _effect = effect;

        scanline(_effect, palette, _frame, _y);
    }
}

static void fill_scanline_buffer(struct scanvideo_scanline_buffer *const buffer)
{
    static uint32_t postamble[] = {0x0000U | (COMPOSABLE_EOL_ALIGN << 16)};

    buffer->data[0] = 4;
    buffer->data[1] = host_safe_hw_ptr(buffer->data + 8);
    buffer->data[2] =
        (VIDEO_W - 4) / 2; /* First four pixels are handled separately. */
    volatile uint16_t *const pixels =
        &vid[scanvideo_scanline_number(buffer->scanline_id)][0];
    buffer->data[3] = host_safe_hw_ptr(pixels + 4);
    buffer->data[4] = count_of(postamble);
    buffer->data[5] = host_safe_hw_ptr(postamble);
    buffer->data[6] = 0;
    buffer->data[7] = 0;
    buffer->data_used = 8;

    // 3 pixel run followed by main run, consuming the first 4 pixels.
    buffer->data[8] = (pixels[0] << 16U) | COMPOSABLE_RAW_RUN;
    buffer->data[9] = (pixels[1] << 16U) | 0;
    buffer->data[10] = (COMPOSABLE_RAW_RUN << 16U) | pixels[2];
    buffer->data[11] =
        (((VIDEO_W - 3) + 1 - 3) << 16U) |
        pixels[3]; // Note we add one for the black pixel at the end.
}

int64_t timer_callback(alarm_id_t const alarm_id, void *const user_data)
{
    struct scanvideo_scanline_buffer *buffer =
        scanvideo_begin_scanline_generation(false);
    while (buffer)
    {
        fill_scanline_buffer(buffer);
        scanvideo_end_scanline_generation(buffer);
        buffer = scanvideo_begin_scanline_generation(false);
    }
    return 100;
}

static void audio_init()
{
    static audio_format_t audio_format = {
        .format = AUDIO_BUFFER_FORMAT_PCM_S16,
        .sample_freq = 8000,
        .channel_count = 1,
    };

    static struct audio_buffer_format producer_format = {
        .format = &audio_format,
        .sample_stride = 2,
    };

    audio_buffer_pool =
        audio_new_producer_pool(&producer_format, 3,
                                SAMPLES_PER_BUFFER); // todo correct size

    bool __unused ok;
    const struct audio_format *output_format;
    struct audio_i2s_config config = {
        .data_pin = PICO_AUDIO_I2S_DATA_PIN,
        .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
        .dma_channel = PICO_AUDIO_I2S_DMA_IRQ,
        .pio_sm = PICO_AUDIO_I2S_PIO,
    };

    output_format = audio_i2s_setup(&audio_format, &config);
    if (!output_format)
    {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    ok = audio_i2s_connect(audio_buffer_pool);
    assert(ok);
    audio_i2s_set_enabled(true);
}

static void core1_func()
{
    audio_init();
    uint32_t step = 1;
    uint32_t pos = 0;
    uint32_t pos_max = __audio_bin_len - 1;

    while (true)
    {
        struct audio_buffer *buffer =
            take_audio_buffer(audio_buffer_pool, true);
        int16_t *samples = (int16_t *)buffer->buffer->bytes;
        for (uint i = 0; i < buffer->max_sample_count; i++)
        {
            samples[i] = __audio_bin[pos] << 8;
            // samples[i] = (vol * sine_wave_table[pos >> 16u]) >> 8u;
            pos += step;
            if (pos >= pos_max)
            {
                pos = pos_max;
            }
        }
        buffer->sample_count = buffer->max_sample_count;
        give_audio_buffer(audio_buffer_pool, buffer);
    }
}

static int vga_main(void)
{
    palette_create();
    vertex_gem_create();

    multicore_launch_core1(core1_func);

    hard_assert(VIDEO_W + 4 <= PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS * 2);
    scanvideo_setup(&vga_mode);
    scanvideo_timing_enable(true);

    frame_prologue();

    add_alarm_in_us(100, timer_callback, NULL, true);
    render_loop();
    return 0;
}

// void __time_critical_func(frame_prologue)()
static void frame_prologue()
{

    /* Lookup table for durations of each effect. */
    static uint32_t const effect_duration[EFFECT_END + 1] = {
        10, 215, 131, 500, 280, 132, 360, 246, UINT32_MAX};
    /* Lookup table for the palette that will be used by each effect. */
    static uint8_t const effect_palette[EFFECT_END + 1] = {0, 1, 2, 1, 0,
                                                           3, 4, 5, 3};

    if (!frame_prologue_done)
    {
        if (frame_rem == 0)
        {
            ++effect;
            frame_rem = effect_duration[effect];
            palette = effect_palette[effect];

            // Prepare buffers for next effect.
            for (uint16_t _y = 0; _y < VIDEO_H; ++_y)
            {
                for (uint16_t _x = 0; _x < VIDEO_W; ++_x)
                {
                    switch (effect)
                    {
                    case EFFECT_WATER:
                        bg[_y][_x] /= 4;
                        break;
                    case EFFECT_ACID:
                        bg[_y][_x] /= 2;
                        break;
                    default:
                        bg[_y][_x] = 0;
                        break;
                    }
                    fg[_y][_x] = 0;
                }
            }
        }

        ++frame;
        --frame_rem;
        y = 0;

        frame_prologue_done = true;
    }
}

int main(void)
{
    uint32_t const base_freq = 50000;
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(10);

    // 300 MHz @ 1.3v.
    set_sys_clock_khz(base_freq * 6, true);

    // 150 MHz @ 1.1v.
    // set_sys_clock_khz(base_freq * 3, true
    return vga_main();
}
