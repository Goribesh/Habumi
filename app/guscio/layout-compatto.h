/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HABUMI_LAYOUT_COMPATTO_H
#define HABUMI_LAYOUT_COMPATTO_H

#include <stdint.h>

typedef struct {
    int view_width;
    int view_height;
    int rail_x;
    int rail_width;
} CompatLayout;

typedef struct {
    int width;
    int height;
} CompatSize;

static inline CompatLayout compat_layout(int client_w, int client_h,
                                         int rail_px)
{
    CompatLayout result;
    int width = client_w > 0 ? client_w : 0;
    int height = client_h > 0 ? client_h : 0;
    int rail = rail_px > 0 ? rail_px : 0;

    if (rail > width) {
        rail = width;
    }
    result.view_width = width - rail;
    result.view_height = height;
    result.rail_x = result.view_width;
    result.rail_width = rail;
    return result;
}

static inline CompatSize compat_fit(int guest_w, int guest_h,
                                    int max_client_w, int max_client_h,
                                    int rail_px)
{
    CompatSize result = {0, 0};
    int max_area_w;
    int rail;
    int64_t width_limited;
    int64_t height_limited;

    if (guest_w <= 0 || guest_h <= 0 || max_client_w <= 0 ||
        max_client_h <= 0 || rail_px < 0) {
        return result;
    }
    rail = rail_px < max_client_w ? rail_px : max_client_w;
    max_area_w = max_client_w - rail;
    if (max_area_w <= 0) {
        return result;
    }

    width_limited = (int64_t)max_area_w * guest_h;
    height_limited = (int64_t)max_client_h * guest_w;
    if (width_limited <= height_limited) {
        result.width = max_area_w;
        result.height = (int)((int64_t)max_area_w * guest_h / guest_w);
    } else {
        result.height = max_client_h;
        result.width = (int)((int64_t)max_client_h * guest_w / guest_h);
    }
    if (result.width < 1) {
        result.width = 1;
    }
    if (result.height < 1) {
        result.height = 1;
    }
    result.width += rail;
    return result;
}

#endif
