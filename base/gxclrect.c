/* Copyright (C) 2001-2012 Artifex Software, Inc.
   All Rights Reserved.

   This software is provided AS-IS with no warranty, either express or
   implied.

   This software is distributed under license and may not be copied,
   modified or distributed except as expressly authorized under the terms
   of the license contained in the file LICENSE in this distribution.

   Refer to licensing information at http://www.artifex.com or contact
   Artifex Software, Inc.,  7 Mt. Lassen Drive - Suite A-134, San Rafael,
   CA  94903, U.S.A., +1(415)492-9861, for further information.
*/


/* Rectangle-oriented command writing for command list */
#include "gx.h"
#include "gserrors.h"
#include "gsutil.h"		/* for gs_next_ids */
#include "gxdevice.h"
#include "gxdevmem.h"		/* must precede gxcldev.h */
#include "gxcldev.h"
#include "gxclpath.h"
#include "gxdevsop.h"

extern dev_proc_dev_spec_op(gdev_prn_forwarding_dev_spec_op);

/* ---------------- Writing utilities ---------------- */

#define cmd_set_rect(rect)\
  ((rect).x = x, (rect).y = y,\
   (rect).width = width, (rect).height = height)

/* Write a rectangle. */
static int
cmd_size_rect(register const gx_cmd_rect * prect)
{
    return
        cmd_sizew(prect->x) + cmd_sizew(prect->y) +
        cmd_sizew(prect->width) + cmd_sizew(prect->height);
}
static byte *
cmd_put_rect(register const gx_cmd_rect * prect, register byte * dp)
{
    cmd_putw(prect->x, dp);
    cmd_putw(prect->y, dp);
    cmd_putw(prect->width, dp);
    cmd_putw(prect->height, dp);
    return dp;
}

int
cmd_write_rect_hl_cmd(gx_device_clist_writer * cldev, gx_clist_state * pcls,
                      int op, int x, int y, int width, int height,
                      bool extended_command)
{
    byte *dp;
    int code;
    int rcsize;

    cmd_set_rect(pcls->rect);
    if (extended_command) {
        rcsize = 2 + cmd_size_rect(&pcls->rect);
        code = set_cmd_put_op(dp, cldev, pcls, cmd_opv_extend, rcsize);
        dp[1] = op;
        dp += 2;
    } else {
        rcsize = 1 + cmd_size_rect(&pcls->rect);
        code = set_cmd_put_op(dp, cldev, pcls, op, rcsize);
        dp += 1;
    }
    if (code < 0)
        return code;
    if_debug5m('L', cldev->memory, "rect hl r%d:%d,%d,%d,%d\n",
               rcsize - 1, pcls->rect.x, pcls->rect.y,
               pcls->rect.width, pcls->rect.height);
    cmd_put_rect(&pcls->rect, dp);
    return 0;
}

int
cmd_write_rect_cmd(gx_device_clist_writer * cldev, gx_clist_state * pcls,
                   int op, int x, int y, int width, int height)
{
    int dx = x - pcls->rect.x;
    int dy = y - pcls->rect.y;
    int dwidth = width - pcls->rect.width;
    int dheight = height - pcls->rect.height;
    byte *dp;
    int code;

#define check_range_xy(rmin, rmax)\
  ((unsigned)(dx - rmin) <= (rmax - rmin) &&\
   (unsigned)(dy - rmin) <= (rmax - rmin))
#define check_range_w(rmin, rmax)\
  ((unsigned)(dwidth - rmin) <= (rmax - rmin))
#define check_ranges(rmin, rmax)\
  (check_range_xy(rmin, rmax) && check_range_w(rmin, rmax) &&\
   (unsigned)(dheight - rmin) <= (rmax - rmin))
    cmd_set_rect(pcls->rect);
    if (dheight == 0 && check_range_w(cmd_min_dw_tiny, cmd_max_dw_tiny) &&
        check_range_xy(cmd_min_dxy_tiny, cmd_max_dxy_tiny)
        ) {
        byte op_tiny = op + 0x20 + dwidth - cmd_min_dw_tiny;

        if (dx == width - dwidth && dy == 0) {
            code = set_cmd_put_op(dp, cldev, pcls, op_tiny + 8, 1);
            if (code < 0)
                return code;
        } else {
            code = set_cmd_put_op(dp, cldev, pcls, op_tiny, 2);
            if (code < 0)
                return code;
            dp[1] = (dx << 4) + dy - (cmd_min_dxy_tiny * 0x11);
        }
    }
#define rmin cmd_min_short
#define rmax cmd_max_short
    else if (check_ranges(rmin, rmax)) {
        int dh = dheight - cmd_min_dxy_tiny;

        if ((unsigned)dh <= cmd_max_dxy_tiny - cmd_min_dxy_tiny &&
            dh != 0 && dy == 0
            ) {
            op += dh;
            code = set_cmd_put_op(dp, cldev, pcls, op + 0x10, 3);
            if (code < 0)
                return code;
            if_debug3m('L', cldev->memory, "    rs2:%d,%d,0,%d\n",
                       dx, dwidth, dheight);
        } else {
            code = set_cmd_put_op(dp, cldev, pcls, op + 0x10, 5);
            if (code < 0)
                return code;
            if_debug4m('L', cldev->memory, "    rs4:%d,%d,%d,%d\n",
                       dx, dwidth, dy, dheight);
            dp[3] = dy - rmin;
            dp[4] = dheight - rmin;
        }
        dp[1] = dx - rmin;
        dp[2] = dwidth - rmin;
    }
#undef rmin
#undef rmax
    else if (dy >= -2 && dy <= 1 && dheight >= -2 && dheight <= 1 &&
             (dy + dheight) != -4
        ) {
        int rcsize = 1 + cmd_sizew(x) + cmd_sizew(width);

        code = set_cmd_put_op(dp, cldev, pcls,
                              op + ((dy + 2) << 2) + dheight + 2, rcsize);
        if (code < 0)
            return code;
        ++dp;
        cmd_put2w(x, width, dp);
    } else {
        int rcsize = 1 + cmd_size_rect(&pcls->rect);

        code = set_cmd_put_op(dp, cldev, pcls, op, rcsize);
        if (code < 0)
            return code;
        if_debug5m('L', cldev->memory, "    r%d:%d,%d,%d,%d\n",
                   rcsize - 1, dx, dwidth, dy, dheight);
        cmd_put_rect(&pcls->rect, dp + 1);
    }
    return 0;
}

/* Put out a fill or tile rectangle command for fillpage. */
int
cmd_write_page_rect_cmd(gx_device_clist_writer * cldev, int op)
{
    /* Since cmd_write_rect_cmd always writes 0x0 rectangle as a tiny one,
       here we write a "big" rectangle with the size 0x0.
       The clist reader must handle this case especially.
     */
    int rcsize = 1 + 4 * cmd_sizew(0);
    byte *dp;
    int x = 0, y = 0, width = 0, height = 0; /* For cmd_set_rect */
    gx_clist_state *pcls1;
    int code;

    if_debug0m('L', cldev->memory, "[L]fillpage beg\n");
    code = set_cmd_put_all_op(dp, cldev, op, rcsize);
    if (code < 0)
        return code;
    for (pcls1 = cldev->states; pcls1 < cldev->states + cldev->nbands; pcls1++)
        cmd_set_rect(pcls1->rect);
    ++dp;
    cmd_putw(0, dp);
    cmd_putw(0, dp);
    cmd_putw(0, dp);
    cmd_putw(0, dp);
    if_debug0m('L', cldev->memory, "[L]fillpage end\n");
    return 0;
}

static inline byte *
cmd_put_frac31_color(gx_device_clist_writer * cldev, const frac31 *c, byte *dp)
{
    int num_components = cldev->clist_color_info.num_components;
    int j;

    for (j = 0; j < num_components; j++)
        dp = cmd_put_frac31(c[j], dp);
    return dp;
}

static inline int
cmd_size_frac31_color(gx_device_clist_writer * cldev, const frac31 *c)
{
    int j, s = 0;
    int num_components = cldev->clist_color_info.num_components;

    for (j = 0; j < num_components; j++)
        s += cmd_size_frac31(c[j]);
    return s;
}

static int
cmd_write_trapezoid_cmd(gx_device_clist_writer * cldev, gx_clist_state * pcls,
                    int op,  const gs_fixed_edge *left, const gs_fixed_edge *right,
                    fixed ybot, fixed ytop, int options,
                    const gs_fill_attributes *fa,
                    const frac31 *c0, const frac31 *c1,
                    const frac31 *c2, const frac31 *c3)
{
    byte *dp;
    int rcsize;
    int code;
    int colors_mask = 0;

    rcsize = 1 + cmd_sizew(left->start.x) + cmd_sizew(left->start.y)
               + cmd_sizew(left->end.x) + cmd_sizew(left->end.y)
               + cmd_sizew(right->start.x) + cmd_sizew(right->start.y)
               + cmd_sizew(right->end.x) + cmd_sizew(right->end.y)
               + cmd_sizew(options);

    if (!(options & 4))
        rcsize += cmd_sizew(ybot) + cmd_sizew(ytop);
    if (options & 2) {
        rcsize += cmd_sizew(fa->clip->p.x) + cmd_sizew(fa->clip->p.y)
                + cmd_sizew(fa->clip->q.x) + cmd_sizew(fa->clip->q.y);
        if (c0 != NULL) {
            colors_mask += 1;
            rcsize += cmd_size_frac31_color(cldev, c0);
        }
        if (c1 != NULL) {
            colors_mask += 2;
            rcsize += cmd_size_frac31_color(cldev, c1);
        }
        if (c2 != NULL) {
            colors_mask += 4;
            rcsize += cmd_size_frac31_color(cldev, c2);
        }
        if (c3 != NULL) {
            colors_mask += 8;
            rcsize += cmd_size_frac31_color(cldev, c3);
        }
        rcsize += cmd_sizew(colors_mask);
    }
    code = set_cmd_put_op(dp, cldev, pcls, op, rcsize);
    if (code < 0)
        return code;
    dp++;
    cmd_putw(left->start.x, dp);
    cmd_putw(left->start.y, dp);
    cmd_putw(left->end.x, dp);
    cmd_putw(left->end.y, dp);
    cmd_putw(right->start.x, dp);
    cmd_putw(right->start.y, dp);
    cmd_putw(right->end.x, dp);
    cmd_putw(right->end.y, dp);
    cmd_putw(options, dp);
    if (!(options & 4)) {
        cmd_putw(ybot, dp);
        cmd_putw(ytop, dp);
    }
    if_debug6m('L', cldev->memory, "    t%d:%ld,%ld,%ld,%ld   %ld\n",
               rcsize - 1, (long int)left->start.x, (long int)left->start.y,
               (long int)left->end.x, (long int)left->end.y, (long int)ybot);
    if_debug6m('L', cldev->memory, "    t%ld,%ld,%ld,%ld   %ld   o=%d\n",
               (long int)right->start.x, (long int)right->start.y,
               (long int)right->end.x, (long int)right->end.y, (long int)ytop,
               options);
    if (options & 2) {
        cmd_putw(fa->clip->p.x, dp);
        cmd_putw(fa->clip->p.y, dp);
        cmd_putw(fa->clip->q.x, dp);
        cmd_putw(fa->clip->q.y, dp);
        cmd_putw(colors_mask, dp);
        if (c0 != NULL)
            dp = cmd_put_frac31_color(cldev, c0, dp);
        if (c1 != NULL)
            dp = cmd_put_frac31_color(cldev, c1, dp);
        if (c2 != NULL)
            dp = cmd_put_frac31_color(cldev, c2, dp);
        if (c3 != NULL)
            cmd_put_frac31_color(cldev, c3, dp);
    }
    return 0;
}

/* ---------------- Driver procedures ---------------- */

int
clist_fillpage(gx_device * dev, gs_imager_state *pis, gx_drawing_color *pdcolor)
{
    gx_device_clist * const cldev = (gx_device_clist *)dev;
    gx_device_clist_writer * const cdev = &(cldev->writer);
    gx_clist_state * pcls;
    int code;

    /* flush previous contents */
    if ((code = clist_close_writer_and_init_reader(cldev) < 0) ||
        (code = clist_finish_page(dev, true)) < 0)
        return code;;

    pcls = cdev->states; /* Use any. */

    do {
        code = cmd_put_drawing_color(cdev, pcls, pdcolor, NULL, devn_not_tile);
        if (code >= 0)
            code = cmd_write_page_rect_cmd(cdev, cmd_op_fill_rect);
    } while (RECT_RECOVER(code));
    return code;
}

int
clist_fill_rectangle(gx_device * dev, int rx, int ry, int rwidth, int rheight,
                     gx_color_index color)
{
    gx_device_clist_writer * const cdev =
        &((gx_device_clist *)dev)->writer;
    int code;
    cmd_rects_enum_t re;
    gx_color_usage_bits color_usage;

    crop_fill(cdev, rx, ry, rwidth, rheight);
    if (rwidth <= 0 || rheight <= 0)
        return 0;
    if (cdev->permanent_error < 0)
      return (cdev->permanent_error);
    /* If needed, update the trans_bbox */
    if (cdev->pdf14_needed) {
        gs_int_rect bbox;

        bbox.p.x = rx;
        bbox.q.x = rx + rwidth - 1;
        bbox.p.y = ry;
        bbox.q.y = ry + rheight - 1;

        clist_update_trans_bbox(cdev, &bbox);
    }
    RECT_ENUM_INIT(re, ry, rheight);
    color_usage = gx_color_index2usage(dev, color);
    do {
        RECT_STEP_INIT(re);
        re.pcls->color_usage.or |= color_usage;
        do {
            code = cmd_disable_lop(cdev, re.pcls);
            if (code >= 0 && color != re.pcls->colors[1]) {
                code = cmd_put_color(cdev, re.pcls, &clist_select_color1,
                                     color, &re.pcls->colors[1]);
            }
            if (code >= 0) {
                code = cmd_write_rect_cmd(cdev, re.pcls, cmd_op_fill_rect, rx, re.y,
                                          rwidth, re.height);
            }
        } while (RECT_RECOVER(code));
        if (code < 0 && SET_BAND_CODE(code))
            goto error_in_rect;
        re.y += re.height;
        continue;
error_in_rect:
        if (!(cdev->error_is_retryable && cdev->driver_call_nesting == 0 &&
                SET_BAND_CODE(clist_VMerror_recover_flush(cdev, re.band_code)) >= 0))
            return re.band_code;
    } while (re.y < re.yend);
    return 0;
}

/* This is used in fills from devn color types */
int
clist_fill_rectangle_hl_color(gx_device *dev, const gs_fixed_rect *rect,
    const gs_imager_state *pis, const gx_drawing_color *pdcolor,
    const gx_clip_path *pcpath)
{
    gx_device_clist_writer * const cdev =
        &((gx_device_clist *)dev)->writer;
    int code;
    int rx, ry, rwidth, rheight;
    cmd_rects_enum_t re;
    gx_color_usage_bits color_usage = cmd_drawing_color_usage(cdev, pdcolor);

    rx = fixed2int(rect->p.x);
    ry = fixed2int(rect->p.y);
    rwidth = fixed2int(rect->q.x) - rx;
    rheight = fixed2int(rect->q.y) - ry;

    crop_fill(cdev, rx, ry, rwidth, rheight);
    if (rwidth <= 0 || rheight <= 0)
        return 0;
    if (cdev->permanent_error < 0)
      return (cdev->permanent_error);
    /* If needed, update the trans_bbox */
    if (cdev->pdf14_needed) {
        gs_int_rect bbox;

        bbox.p.x = rx;
        bbox.q.x = rx + rwidth - 1;
        bbox.p.y = ry;
        bbox.q.y = ry + rheight - 1;

        clist_update_trans_bbox(cdev, &bbox);
    }
    RECT_ENUM_INIT(re, ry, rheight);
    do {
        RECT_STEP_INIT(re);
        re.pcls->color_usage.or |= color_usage;
        do {
            code = cmd_disable_lop(cdev, re.pcls);
            code = cmd_put_drawing_color(cdev, re.pcls, pdcolor, &re,
                                         devn_not_tile);
            if (code >= 0) {
                code = cmd_write_rect_hl_cmd(cdev, re.pcls, cmd_op_fill_rect_hl,
                                             rx, re.y, rwidth, re.height, false);
            }
        } while (RECT_RECOVER(code));
        if (code < 0 && SET_BAND_CODE(code))
            goto error_in_rect;
        re.y += re.height;
        continue;
error_in_rect:
        if (!(cdev->error_is_retryable && cdev->driver_call_nesting == 0 &&
                SET_BAND_CODE(clist_VMerror_recover_flush(cdev, re.band_code)) >= 0))
            return re.band_code;
    } while (re.y < re.yend);
    return 0;
}

static void update_color_use_frac_array(int num_colors, const frac31 *color,
    cmd_rects_enum_t *re)
{
    int k;

    if (color == NULL)
        return;

    for (k = 0; k < num_colors; k++){
        if (color[k] != 0) {
            re->pcls->color_usage.or |= (1<<k);
        }
    }
}

static inline int
clist_write_fill_trapezoid(gx_device * dev,
    const gs_fixed_edge *left, const gs_fixed_edge *right,
    fixed ybot, fixed ytop, int options,
    const gx_drawing_color *pdcolor, gs_logical_operation_t lop,
    const gs_fill_attributes *fa,
    const frac31 *c0, const frac31 *c1,
    const frac31 *c2, const frac31 *c3)
{
    gx_device_clist_writer * const cdev =
        &((gx_device_clist *)dev)->writer;
    int code;
    cmd_rects_enum_t re;
    int ry, rheight, rx, rxe;
    bool swap_axes = (options & 1);

    if (options & 4) {
        if (swap_axes) {
            rx =  fixed2int(max(min(min(left->start.y, left->end.y), right->start.y), fa->clip->p.y));
            rxe =  fixed2int_ceiling(min(max(max(left->start.y, left->end.y), right->start.y), fa->clip->q.y));
            ry = fixed2int(max(min(min(left->start.x, left->end.x), right->start.x), fa->clip->p.x));
            rheight = fixed2int_ceiling(min(max(max(left->start.x, left->end.x), right->start.x), fa->clip->q.x)) - ry;
        } else {
            rx = fixed2int(max(min(min(left->start.x, left->end.x), right->start.x), fa->clip->p.x));
            rxe = fixed2int_ceiling(min(max(max(left->start.x, left->end.x), right->start.x), fa->clip->q.x));
            ry = fixed2int(max(min(min(left->start.y, left->end.y), right->start.y), fa->clip->p.y));
            rheight = fixed2int_ceiling(min(max(max(left->start.y, left->end.y), right->start.y), fa->clip->q.y)) - ry;
        }
    } else {
        /* fixme: this may give a significant overestimation,
           so the command will be written to many bands.
           Would like to know a better range by X
           with computing intersections of sides with ybot, ytop. */
        if (swap_axes) {
            rx = fixed2int(ybot);
            rxe = fixed2int_ceiling(ytop);
            ry = fixed2int(min(left->start.x, left->end.x));
            rheight = fixed2int_ceiling(max(right->start.x, right->end.x)) - ry;
        } else {
            rx = fixed2int(min(left->start.x, left->end.x));
            rxe = fixed2int_ceiling(max(right->start.x, right->end.x));
            ry = fixed2int(ybot);
            rheight = fixed2int_ceiling(ytop) - ry;
        }
    }
    crop_fill_y(cdev, ry, rheight);
    if (rheight <= 0)
        return 0;
    if (cdev->permanent_error < 0)
        return (cdev->permanent_error);
    /* If needed, update the trans_bbox */
    if (cdev->pdf14_needed) {
        gs_int_rect bbox;

        bbox.p.x = rx;
        bbox.q.x = rxe;
        bbox.p.y = ry;
        bbox.q.y = ry + rheight;

        clist_update_trans_bbox(cdev, &bbox);
    }
    RECT_ENUM_INIT(re, ry, rheight);
    do {
        RECT_STEP_INIT(re);
        do {
            if (pdcolor != NULL) {
                code = cmd_put_drawing_color(cdev, re.pcls, pdcolor, &re,
                                             devn_not_tile);
                if (code == gs_error_unregistered)
                    return code;
                if (code < 0) {
                    /* Something went wrong, use the default implementation. */
                    return gx_default_fill_trapezoid(dev, left, right, ybot, ytop, swap_axes, pdcolor, lop);
                }
                code = cmd_update_lop(cdev, re.pcls, lop);
            } else {
                /* Even with pdcolor NULL, we may still have colors packed in 
                   c0, c1, c2 or c3 */
                update_color_use_frac_array(dev->color_info.num_components, c0, &re);
                update_color_use_frac_array(dev->color_info.num_components, c1, &re);
                update_color_use_frac_array(dev->color_info.num_components, c2, &re);
                update_color_use_frac_array(dev->color_info.num_components, c3, &re);
                code = 0;
            }
            if (code >= 0) {
                /* Dont't want to shorten the trapezoid by the band boundary,
                   keeping in mind a further optimization with writing same data to all bands. */
                code = cmd_write_trapezoid_cmd(cdev, re.pcls, cmd_opv_fill_trapezoid, left, right,
                                          ybot, ytop, options, fa, c0, c1, c2, c3);
            }
        } while (RECT_RECOVER(code));
        if (code < 0 && SET_BAND_CODE(code))
            goto error_in_rect;
        re.y += re.height;
        continue;
error_in_rect:
        if (!(cdev->error_is_retryable && cdev->driver_call_nesting == 0 &&
                SET_BAND_CODE(clist_VMerror_recover_flush(cdev, re.band_code)) >= 0))
            return re.band_code;
    } while (re.y < re.yend);
    return 0;
}

int
clist_fill_trapezoid(gx_device * dev,
    const gs_fixed_edge *left, const gs_fixed_edge *right,
    fixed ybot, fixed ytop, bool swap_axes,
    const gx_drawing_color *pdcolor, gs_logical_operation_t lop)
{
    return clist_write_fill_trapezoid(dev, left, right,
        ybot, ytop, swap_axes, pdcolor, lop, NULL, NULL, NULL, NULL, NULL);
}

int
clist_fill_linear_color_trapezoid(gx_device * dev, const gs_fill_attributes *fa,
        const gs_fixed_point *p0, const gs_fixed_point *p1,
        const gs_fixed_point *p2, const gs_fixed_point *p3,
        const frac31 *c0, const frac31 *c1,
        const frac31 *c2, const frac31 *c3)
{
    gs_fixed_edge left, right;
    int code;

    left.start = *p0;
    left.end = *p1;
    right.start = *p2;
    right.end = *p3;
    code = clist_write_fill_trapezoid(dev, &left, &right,
        fa->ystart, fa->yend, fa->swap_axes | 2, NULL, fa->lop, fa, c0, c1, c2, c3);
    if (code < 0)
        return code;
    /* NOTE : The return value 0 for the fill_linear_color_trapezoid method means
       that the device requests a further decomposition of the trapezoid.
       Currently we have no interface for checking whether the target device
       can handle a trapezoid without an attempt to fill it.
       Therefore the clist writer device must always return 1,
       and the clist reader must perform a further decomposition
       if the target device requests it.
     */
    return 1;
}

int
clist_fill_linear_color_triangle(gx_device * dev, const gs_fill_attributes *fa,
        const gs_fixed_point *p0, const gs_fixed_point *p1,
        const gs_fixed_point *p2,
        const frac31 *c0, const frac31 *c1, const frac31 *c2)
{
    gs_fixed_edge left, right;
    int code;

    left.start = *p0;
    left.end = *p1;
    right.start = *p2;
    right.end.x = right.end.y = 0; /* unused. */

    code = clist_write_fill_trapezoid(dev, &left, &right,
        0, 0, fa->swap_axes | 2 | 4, NULL, fa->lop, fa, c0, c1, c2, NULL);
    if (code < 0)
        return code;
    /* NOTE : The return value 0 for the fill_linear_color_triangle method means
       that the device requests a further decomposition of the trapezoid.
       Currently we have no interface for checking whether the target device
       can handle a trapezoid without an attempt to fill it.
       Therefore the clist writer device must always return 1,
       and the clist reader must perform a further decomposition
       if the target device requests it.
     */
    return 1;
}

extern dev_proc_open_device(pattern_clist_open_device);

int
clist_dev_spec_op(gx_device *pdev, int dev_spec_op, void *data, int size)
{
    gx_device_clist_common * const cdev = &((gx_device_clist *)pdev)->common;

    if (dev_spec_op == gxdso_pattern_handles_clip_path)
        return 1;
    if (dev_spec_op == gxdso_pattern_shfill_doesnt_need_path)
        return 1;
    if (dev_spec_op == gxdso_supports_devn) {
        cmm_dev_profile_t *dev_profile;
        int code;
        code = dev_proc(cdev, get_profile)((gx_device*) cdev, &dev_profile);
        if (code == 0) {
            return dev_profile->supports_devn;
        } else {
            return 0;
        }
    }
    /* forward to our super class */
    if (cdev->is_printer)
        return gdev_prn_forwarding_dev_spec_op(pdev, dev_spec_op, data, size);
    return gx_default_dev_spec_op(pdev, dev_spec_op, data, size);
}

#define dev_proc_pattern_manage(proc)\
  dev_t_proc_pattern_manage(proc, gx_device)

/* Based heavily off of clist_strip_tile_rectangle */
int
clist_strip_tile_rect_devn(gx_device * dev, const gx_strip_bitmap * tile,
                           int rx, int ry, int rwidth, int rheight,
                           const gx_drawing_color *pdcolor0,
                           const gx_drawing_color *pdcolor1, int px, int py)
{
    gx_device_clist_writer * const cdev =
        &((gx_device_clist *)dev)->writer;
    int depth = 1;
    int code;
    cmd_rects_enum_t re;
    gx_color_usage_bits color_usage = cmd_drawing_color_usage(cdev, pdcolor0);

    crop_fill(cdev, rx, ry, rwidth, rheight);
    if (rwidth <= 0 || rheight <= 0)
        return 0;
    if (cdev->permanent_error < 0)
      return (cdev->permanent_error);
    /* If needed, update the trans_bbox */
    if (cdev->pdf14_needed) {
        gs_int_rect bbox;

        bbox.p.x = rx;
        bbox.q.x = rx + rwidth - 1;
        bbox.p.y = ry;
        bbox.q.y = ry + rheight - 1;

        clist_update_trans_bbox(cdev, &bbox);
    }
    color_usage |= cmd_drawing_color_usage(cdev, pdcolor1);
    RECT_ENUM_INIT(re, ry, rheight);
    do {
        ulong offset_temp;

        RECT_STEP_INIT(re);
        re.pcls->color_usage.or |= color_usage;
        do {
            code = cmd_disable_lop(cdev, re.pcls);
        } while (RECT_RECOVER(code));
        if (code < 0 && SET_BAND_CODE(code))
            goto error_in_rect;
        /* Change the tile if needed */
        if (!cls_has_tile_id(cdev, re.pcls, tile->id, offset_temp)) {
            if (tile->id != gx_no_bitmap_id) {
                do {
                    code = clist_change_tile(cdev, re.pcls, tile, depth);
                } while (RECT_RECOVER(code));
                if (code < 0 && !(code != gs_error_VMerror || !cdev->error_is_retryable) && SET_BAND_CODE(code))
                    goto error_in_rect;
            } else
                code = -1;
            if (code < 0) {
                return_error(gs_error_unregistered);
            }
        }
        do {
            code = 0;
            /* Write out the devn colors */
            code = cmd_put_drawing_color(cdev, re.pcls, pdcolor0, &re,
                                         devn_tile0);
            code = cmd_put_drawing_color(cdev, re.pcls, pdcolor1, &re,
                                         devn_tile1);
            /* Set the tile phase */
            if (px != re.pcls->tile_phase.x || py != re.pcls->tile_phase.y) {
                if (code >= 0)
                    code = cmd_set_tile_phase(cdev, re.pcls, px, py);
            }
            /* Write out the actually command to fill with the devn colors */
            if (code >= 0) {
                code = cmd_write_rect_hl_cmd(cdev, re.pcls,
                                             cmd_opv_ext_tile_rect_hl, rx, re.y,
                                             rwidth, re.height, true);
            }
        } while (RECT_RECOVER(code));
        if (code < 0 && SET_BAND_CODE(code))
            goto error_in_rect;
        re.y += re.height;
        continue;
error_in_rect:
        if (!(cdev->error_is_retryable && cdev->driver_call_nesting == 0 &&
                SET_BAND_CODE(clist_VMerror_recover_flush(cdev, re.band_code)) >= 0))
            return re.band_code;
    } while (re.y < re.yend);
    return 0;
}

int
clist_strip_tile_rectangle(gx_device * dev, const gx_strip_bitmap * tile,
                           int rx, int ry, int rwidth, int rheight,
               gx_color_index color0, gx_color_index color1, int px, int py)
{
    gx_device_clist_writer * const cdev =
        &((gx_device_clist *)dev)->writer;
    int depth =
        (color1 == gx_no_color_index && color0 == gx_no_color_index ?
         cdev->clist_color_info.depth : 1);
    gx_color_usage_bits color_usage =
        (color1 == gx_no_color_index && color0 == gx_no_color_index ?
         /* We can't know what colors will be used: assume the worst. */
         gx_color_usage_all(dev) :
         (color0 == gx_no_color_index ? 0 : gx_color_index2usage(dev, color0)) |
         (color1 == gx_no_color_index ? 0 : gx_color_index2usage(dev, color1)));
    int code;
    cmd_rects_enum_t re;

    crop_fill(cdev, rx, ry, rwidth, rheight);
    if (rwidth <= 0 || rheight <= 0)
        return 0;
    if (cdev->permanent_error < 0)
        return (cdev->permanent_error);
    /* If needed, update the trans_bbox */
    if (cdev->pdf14_needed) {
        gs_int_rect bbox;

        bbox.p.x = rx;
        bbox.q.x = rx + rwidth - 1;
        bbox.p.y = ry;
        bbox.q.y = ry + rheight - 1;

        clist_update_trans_bbox(cdev, &bbox);
    }
    RECT_ENUM_INIT(re, ry, rheight);
    do {
        ulong offset_temp;

        RECT_STEP_INIT(re);
        re.pcls->color_usage.or |= color_usage;
        do {
            code = cmd_disable_lop(cdev, re.pcls);
        } while (RECT_RECOVER(code));
        if (code < 0 && SET_BAND_CODE(code))
            goto error_in_rect;
        if (!cls_has_tile_id(cdev, re.pcls, tile->id, offset_temp)) {
            if (tile->id != gx_no_bitmap_id) {
                do {
                    code = clist_change_tile(cdev, re.pcls, tile, depth);
                } while (RECT_RECOVER(code));
                if (code < 0 && !(code != gs_error_VMerror || !cdev->error_is_retryable) && SET_BAND_CODE(code))
                    goto error_in_rect;
            } else
                code = -1; /* Force the default implementation. Should never happen. */
            if (code < 0) {
                /* ok if gx_default... does retries internally: */
                /* it's self-sufficient */
                code = gx_default_strip_tile_rectangle(dev, tile,
                                                       rx, re.y, rwidth, re.height,
                                                       color0, color1,
                                                       px, py);
                if (code < 0 && SET_BAND_CODE(code))
                    goto error_in_rect;
                goto endr;
            }
        }
        do {
            code = 0;
            if (color0 != re.pcls->tile_colors[0] || color1 != re.pcls->tile_colors[1])
                code = cmd_set_tile_colors(cdev, re.pcls, color0, color1);
            if (px != re.pcls->tile_phase.x || py != re.pcls->tile_phase.y) {
                if (code >= 0)
                    code = cmd_set_tile_phase(cdev, re.pcls, px, py);
            }
            if (code >= 0)
                code = cmd_write_rect_cmd(cdev, re.pcls, cmd_op_tile_rect, rx, re.y,
                                          rwidth, re.height);
        } while (RECT_RECOVER(code));
        if (code < 0 && SET_BAND_CODE(code))
            goto error_in_rect;
endr:;
        re.y += re.height;
        continue;
error_in_rect:
        if (!(cdev->error_is_retryable && cdev->driver_call_nesting == 0 &&
                SET_BAND_CODE(clist_VMerror_recover_flush(cdev, re.band_code)) >= 0))
            return re.band_code;
    } while (re.y < re.yend);
    return 0;
}

int
clist_copy_mono(gx_device * dev,
                const byte * data, int data_x, int raster, gx_bitmap_id id,
                int rx, int ry, int rwidth, int rheight,
                gx_color_index color0, gx_color_index color1)
{
    gx_device_clist_writer * const cdev =
        &((gx_device_clist *)dev)->writer;
    int y0;
    gx_bitmap_id orig_id = id;
    gx_color_usage_bits color_usage =
        (color0 == gx_no_color_index ? 0 : gx_color_index2usage(dev, color0)) |
        (color1 == gx_no_color_index ? 0 : gx_color_index2usage(dev, color1));
    cmd_rects_enum_t re;

    fit_copy(dev, data, data_x, raster, id, rx, ry, rwidth, rheight);
    y0 = ry;
    if (cdev->permanent_error < 0)
      return (cdev->permanent_error);
    /* If needed, update the trans_bbox */
    if (cdev->pdf14_needed) {
        gs_int_rect bbox;

        bbox.p.x = rx;
        bbox.q.x = rx + rwidth - 1;
        bbox.p.y = ry;
        bbox.q.y = ry + rheight - 1;

        clist_update_trans_bbox(cdev, &bbox);
    }
    RECT_ENUM_INIT(re, ry, rheight);
    do {
        int dx = data_x & 7;
        int w1 = dx + rwidth;
        const byte *row = data + (re.y - y0) * raster + (data_x >> 3);
        int code;

        RECT_STEP_INIT(re);
        re.pcls->color_usage.or |= color_usage;
        do {
            code = cmd_disable_lop(cdev, re.pcls);
            if (code >= 0)
                code = cmd_disable_clip(cdev, re.pcls);
            if (color0 != re.pcls->colors[0] && code >= 0)
                code = cmd_set_color0(cdev, re.pcls, color0);
            if (color1 != re.pcls->colors[1] && code >= 0)
                code = cmd_set_color1(cdev, re.pcls, color1);
        } while (RECT_RECOVER(code));
        if (code < 0 && SET_BAND_CODE(code))
            goto error_in_rect;
        /* Don't bother to check for a possible cache hit: */
        /* tile_rectangle and fill_mask handle those cases. */
copy:{
        gx_cmd_rect rect;
        int rsize;
        byte op = (byte) cmd_op_copy_mono_planes;
        byte *dp;
        uint csize;
        uint compress;
        int code;

        rect.x = rx, rect.y = re.y;
        rect.width = w1, rect.height = re.height;
        rsize = (dx ? 3 : 1) + cmd_size_rect(&rect) + cmd_sizew(0); /* planar_height 0 */
        do {
            code = cmd_put_bits(cdev, re.pcls, row, w1, re.height, raster,
                                rsize, (orig_id == gx_no_bitmap_id ?
                                        1 << cmd_compress_rle :
                                        cmd_mask_compress_any),
                                &dp, &csize);
        } while (RECT_RECOVER(code));
        if (code < 0 && !(code == gs_error_limitcheck) && SET_BAND_CODE(code))
            goto error_in_rect;
        compress = (uint)code;
        if (code < 0) {
            /* The bitmap was too large; split up the transfer. */
            if (re.height > 1) {
                /*
                 * Split the transfer by reducing the height.
                 * See the comment above FOR_RECTS in gxcldev.h.
                 */
                re.height >>= 1;
                goto copy;
            } else {
                /* Split a single (very long) row. */
                int w2 = w1 >> 1;

                ++cdev->driver_call_nesting;
                {
                    code = clist_copy_mono(dev, row, dx,
                                           raster, gx_no_bitmap_id, rx, re.y,
                                           w2, 1, color0, color1);
                    if (code >= 0)
                        code = clist_copy_mono(dev, row, dx + w2,
                                               raster, gx_no_bitmap_id,
                                               rx + w2, re.y,
                                               w1 - w2, 1, color0, color1);
                }
                --cdev->driver_call_nesting;
                if (code < 0 && SET_BAND_CODE(code))
                    goto error_in_rect;
                continue;
            }
        }
        op += compress;
        if (dx) {
            *dp++ = cmd_count_op(cmd_opv_set_misc, 2, dev->memory);
            *dp++ = cmd_set_misc_data_x + dx;
        }
        *dp++ = cmd_count_op(op, csize, dev->memory);
        /* Store the plane_height */
        cmd_putw(0, dp);
        cmd_put2w(rx, re.y, dp);
        cmd_put2w(w1, re.height, dp);
        re.pcls->rect = rect;
        }
        continue;
error_in_rect:
        if (!(cdev->error_is_retryable && cdev->driver_call_nesting == 0 &&
                SET_BAND_CODE(clist_VMerror_recover_flush(cdev, re.band_code)) >= 0))
            return re.band_code;
        re.y -= re.height;
    } while ((re.y += re.height) < re.yend);
    return 0;
}

/* The code duplication between this and clist_copy_mono needs to be removed */
int
clist_copy_planes(gx_device * dev,
                  const byte * data, int data_x, int raster, gx_bitmap_id id,
                  int rx, int ry, int rwidth, int rheight, int plane_height)
{
    gx_device_clist_writer * const cdev =
        &((gx_device_clist *)dev)->writer;
    int y0;
    gx_bitmap_id orig_id = id;
    cmd_rects_enum_t re;
    int bpc = dev->color_info.depth / dev->color_info.num_components;

    if (rwidth <= 0 || rheight <= 0)
        return 0;

    fit_copy(dev, data, data_x, raster, id, rx, ry, rwidth, rheight);

    y0 = ry;
    if (cdev->permanent_error < 0)
      return (cdev->permanent_error);

#ifdef DEBUG
    if (plane_height == 0) {
        dprintf("clist_copy_planes called with plane_height == 0.\n");
    }
#endif

    /* If needed, update the trans_bbox */
    if (cdev->pdf14_needed) {
        gs_int_rect bbox;

        bbox.p.x = rx;
        bbox.q.x = rx + rwidth - 1;
        bbox.p.y = ry;
        bbox.q.y = ry + rheight - 1;

        clist_update_trans_bbox(cdev, &bbox);
    }

    RECT_ENUM_INIT(re, ry, rheight);
    do {
        int code;
        gx_cmd_rect rect;
        int rsize;
        byte *dp, *dp2;
        uint csize;
        byte op = (byte) cmd_op_copy_mono_planes;
        int dx = data_x % (8/bpc);
        int w1 = dx + rwidth;
        const byte *row = data + (re.y - y0) * raster + (data_x / (8/bpc));
        int bytes_row = ((w1*bpc+7)/8 + 7) & ~7;
        int maxheight = data_bits_size / bytes_row / cdev->color_info.num_components;

        RECT_STEP_INIT(re);
        do {
            code = cmd_disable_lop(cdev, re.pcls);
            if (code >= 0)
                code = cmd_disable_clip(cdev, re.pcls);
        } while (RECT_RECOVER(code));
        if (code < 0 && SET_BAND_CODE(code))
            goto error_in_rect;
        /* Don't bother to check for a possible cache hit: */
        /* tile_rectangle and fill_mask handle those cases. */

        /* We require that in the copy_planes case all the planes fit into
         * the data_bits area (for the copy_planes call by the clist reader) */
        if (re.height > maxheight)
            re.height = maxheight;

        if (re.height == 0) {
            /* Not even a single row fits. Revert to a single row and split it in two recursively */
            int w2 = w1 >> 1;

            re.height = 1;
            ++cdev->driver_call_nesting;
            code = clist_copy_planes(dev, row, dx, raster,
                                     gx_no_bitmap_id, rx, re.y,
                                     w2, 1, plane_height);
            if (code >= 0)
                code = clist_copy_planes(dev, row, dx + w2,
                                         raster, gx_no_bitmap_id,
                                         rx + w2, re.y,
                                         w1 - w2, 1, plane_height);
            --cdev->driver_call_nesting;
            if (code < 0 && SET_BAND_CODE(code))
                goto error_in_rect;
            continue;
        }

        /* 0x100 fudge is arbitrary, but the BufferSpace is large w.r.t. cbuf size so it doesn't matter */
        if ((cdev->cend - cdev->cnext) < 0x100 + (re.height * bytes_row * cdev->color_info.num_components))
            cmd_write_buffer(cdev, cmd_opv_end_run);	/* Insure that all planes fit in the bufferspace */

        rect.x = rx, rect.y = re.y;
        rect.width = w1, rect.height = re.height;
        rsize = (dx ? 3 : 1) + cmd_size_rect(&rect) + cmd_sizew(plane_height);
        do {
            int plane;
            /* Copy the 0th plane - this is the one the op goes in. */
            code = cmd_put_bits(cdev, re.pcls, row, w1*bpc, re.height, raster,
                                rsize, (bpc == 1 ? (orig_id == gx_no_bitmap_id ?
                                                    1 << cmd_compress_rle :
                                                    cmd_mask_compress_any) : 0),
                                &dp, &csize);
            if (code < 0)
                continue;
            /* Write the command header out now, in case the following
             * cmd_put_bits fill the buffer up. */
            dp2 = dp;
            if (dx) {
                *dp2++ = cmd_count_op(cmd_opv_set_misc, 2, cdev->memory);
                *dp2++ = cmd_set_misc_data_x + dx;
            }
            *dp2++ = cmd_count_op(op + code, csize, cdev->memory);
            cmd_putw(plane_height, dp2);
            cmd_put2w(rx, re.y, dp2);
            cmd_put2w(w1, re.height, dp2);
            for (plane = 1; plane < cdev->color_info.num_components && (code >= 0); plane++)
            {
                byte *dummy_dp;
                uint csize2;
                /* Copy subsequent planes - 1 byte header used to send the
                 * compression type. */
                code = cmd_put_bits(cdev, re.pcls,
                                    row + plane_height * raster * plane,
                                    w1*bpc, re.height, raster, 1,
                                    (bpc == 1 ?
                                     (orig_id == gx_no_bitmap_id ?
                                      1 << cmd_compress_rle :
                                      cmd_mask_compress_any) : 0),
                                    &dummy_dp, &csize2);
                if (code >= 0)
                    *dummy_dp = code;

                csize += csize2;
            }
        } while (RECT_RECOVER(code));
        if (code < 0 && !(code == gs_error_limitcheck) && SET_BAND_CODE(code))
            goto error_in_rect;

        re.pcls->rect = rect;
        continue;
error_in_rect:
        if (!(cdev->error_is_retryable && cdev->driver_call_nesting == 0 &&
                SET_BAND_CODE(clist_VMerror_recover_flush(cdev, re.band_code)) >= 0))
            return re.band_code;
        re.y -= re.height;
    } while ((re.y += re.height) < re.yend);
    return 0;
}

int
clist_copy_color(gx_device * dev,
                 const byte * data, int data_x, int raster, gx_bitmap_id id,
                 int rx, int ry, int rwidth, int rheight)
{
    gx_device_clist_writer * const cdev =
        &((gx_device_clist *)dev)->writer;
    int depth = cdev->clist_color_info.depth; /* Could be different than target
                                                 if 1.4 device */
    int y0;
    int data_x_bit;
    /* We can't know what colors will be used: assume the worst. */
    gx_color_usage_bits all = gx_color_usage_all(cdev);
    cmd_rects_enum_t re;

    fit_copy(dev, data, data_x, raster, id, rx, ry, rwidth, rheight);
    y0 = ry;
    data_x_bit = data_x * depth;
    if (cdev->permanent_error < 0)
      return (cdev->permanent_error);
    /* If needed, update the trans_bbox */
    if (cdev->pdf14_needed) {
        gs_int_rect bbox;

        bbox.p.x = rx;
        bbox.q.x = rx + rwidth - 1;
        bbox.p.y = ry;
        bbox.q.y = ry + rheight - 1;

        clist_update_trans_bbox(cdev, &bbox);
    }
    RECT_ENUM_INIT(re, ry, rheight);
    do {
        int dx = (data_x_bit & 7) / depth;
        int w1 = dx + rwidth;
        const byte *row = data + (re.y - y0) * raster + (data_x_bit >> 3);
        int code;

        RECT_STEP_INIT(re);
        re.pcls->color_usage.or |= all;

        do {
            code = cmd_disable_lop(cdev, re.pcls);
            if (code >= 0)
                code = cmd_disable_clip(cdev, re.pcls);
        } while (RECT_RECOVER(code));
        if (code < 0 && SET_BAND_CODE(code))
            goto error_in_rect;
        if (re.pcls->color_is_alpha) {
            byte *dp;

            do {
                code =
                    set_cmd_put_op(dp, cdev, re.pcls, cmd_opv_set_copy_color, 1);
            } while (RECT_RECOVER(code));
            if (code < 0 && SET_BAND_CODE(code))
                goto error_in_rect;
            re.pcls->color_is_alpha = 0;
        }
copy:{
            gx_cmd_rect rect;
            int rsize;
            byte op = (byte) cmd_op_copy_color_alpha;
            byte *dp;
            uint csize;
            uint compress;

            rect.x = rx, rect.y = re.y;
            rect.width = w1, rect.height = re.height;
            rsize = (dx ? 3 : 1) + cmd_size_rect(&rect);
            do {
                code = cmd_put_bits(cdev, re.pcls, row, w1 * depth,
                                    re.height, raster, rsize,
                                    1 << cmd_compress_rle, &dp, &csize);
            } while (RECT_RECOVER(code));
            if (code < 0 && !(code == gs_error_limitcheck) && SET_BAND_CODE(code))
                goto error_in_rect;
            compress = (uint)code;
            if (code < 0) {
                /* The bitmap was too large; split up the transfer. */
                if (re.height > 1) {
                    /* Split the transfer by reducing the height.
                     * See the comment above FOR_RECTS in gxcldev.h.
                     */
                    re.height >>= 1;
                    goto copy;
                } else {
                    /* Split a single (very long) row. */
                    int w2 = w1 >> 1;

                    ++cdev->driver_call_nesting;
                    {
                        code = clist_copy_color(dev, row, dx,
                                                raster, gx_no_bitmap_id,
                                                rx, re.y, w2, 1);
                        if (code >= 0)
                            code = clist_copy_color(dev, row, dx + w2,
                                                    raster, gx_no_bitmap_id,
                                                    rx + w2, re.y, w1 - w2, 1);
                    }
                    --cdev->driver_call_nesting;
                    if (code < 0 && SET_BAND_CODE(code))
                        goto error_in_rect;
                    continue;
                }
            }
            op += compress;
            if (dx) {
                *dp++ = cmd_count_op(cmd_opv_set_misc, 2, dev->memory);
                *dp++ = cmd_set_misc_data_x + dx;
            }
            *dp++ = cmd_count_op(op, csize, dev->memory);
            cmd_put2w(rx, re.y, dp);
            cmd_put2w(w1, re.height, dp);
            re.pcls->rect = rect;
        }
        continue;
error_in_rect:
        if (!(cdev->error_is_retryable && cdev->driver_call_nesting == 0 &&
                SET_BAND_CODE(clist_VMerror_recover_flush(cdev, re.band_code)) >= 0))
            return re.band_code;
        re.y -= re.height;
    } while ((re.y += re.height) < re.yend);
    return 0;
}

/* Patterned after clist_copy_alpha and sharing a command with
   cmd_op_copy_color_alpha.  This was done due to avoid difficult
   to follow code in the read back logic in gxclrast.c.  Due to the
   recursive nature of this call, it would be a bit painful to do much
   sharing between clist_copy_alpha_hl_color and clist_copy_alpha */
int
clist_copy_alpha_hl_color(gx_device * dev, const byte * data, int data_x,
           int raster, gx_bitmap_id id, int rx, int ry, int rwidth, int rheight,
                 const gx_drawing_color *pdcolor, int depth)
{
    gx_device_clist_writer * const cdev =
        &((gx_device_clist *)dev)->writer;

    int log2_depth = ilog2(depth);
    int y0;
    int data_x_bit;
    cmd_rects_enum_t re;
    gx_color_usage_bits all = gx_color_usage_all(cdev);

    /* If the target can't perform copy_alpha, exit now */
    if (depth > 1 && (cdev->disable_mask & clist_disable_copy_alpha) != 0)
        return_error(gs_error_unknownerror);

    fit_copy(dev, data, data_x, raster, id, rx, ry, rwidth, rheight);
    y0 = ry;
    data_x_bit = data_x << log2_depth;
    if (cdev->permanent_error < 0)
      return (cdev->permanent_error);
    /* If needed, update the trans_bbox */
    if (cdev->pdf14_needed) {
        gs_int_rect bbox;

        bbox.p.x = rx;
        bbox.q.x = rx + rwidth - 1;
        bbox.p.y = ry;
        bbox.q.y = ry + rheight - 1;

        clist_update_trans_bbox(cdev, &bbox);
    }
    RECT_ENUM_INIT(re, ry, rheight);
    do {
        int dx = (data_x_bit & 7) >> log2_depth;
        int w1 = dx + rwidth;
        const byte *row = data + (re.y - y0) * raster + (data_x_bit >> 3);
        int code;

        RECT_STEP_INIT(re);
        re.pcls->color_usage.or = all;
        do {
            code = cmd_disable_lop(cdev, re.pcls);
            if (code >= 0)
                code = cmd_disable_clip(cdev, re.pcls);
        } while (RECT_RECOVER(code));
        if (code < 0 && SET_BAND_CODE(code))
            goto error_in_rect;
        if (!re.pcls->color_is_alpha) {
            byte *dp;

            do {
                code =
                    set_cmd_put_op(dp, cdev, re.pcls, cmd_opv_set_copy_alpha, 1);
            } while (RECT_RECOVER(code));
            if (code < 0 && SET_BAND_CODE(code))
                goto error_in_rect;
            re.pcls->color_is_alpha = 1;
        }
        /* Set extended command for overload of copy_color_alpha with devn type */
        if (!re.pcls->color_is_devn) {
            byte *dp;

            do {
                code = set_cmd_put_op(dp, cdev, re.pcls, cmd_opv_extend, 2);
                dp[1] = cmd_opv_ext_set_color_is_devn;
                dp += 2;
            } while (RECT_RECOVER(code));
            if (code < 0 && SET_BAND_CODE(code))
                goto error_in_rect;
            re.pcls->color_is_alpha = 1;
        }
        /* Set the color */
        do {
            code = cmd_put_drawing_color(cdev, re.pcls, pdcolor, &re,
                                         devn_not_tile);
        } while (RECT_RECOVER(code));
copy:{
            gx_cmd_rect rect;
            int rsize;
            byte op = (byte) cmd_op_copy_color_alpha;
            byte *dp;
            uint csize;
            uint compress;

            rect.x = rx, rect.y = re.y;
            rect.width = w1, rect.height = re.height;
            rsize = (dx ? 4 : 2) + cmd_size_rect(&rect);
            do {
                code = cmd_put_bits(cdev, re.pcls, row, w1 << log2_depth,
                                    re.height, raster, rsize,
                                    1 << cmd_compress_rle, &dp, &csize);
            } while (RECT_RECOVER(code));
            if (code < 0 && !(code == gs_error_limitcheck) && SET_BAND_CODE(code))
                goto error_in_rect;
            compress = (uint)code;
            if (code < 0) {
                /* The bitmap was too large; split up the transfer. */
                if (re.height > 1) {
                    /* Split the transfer by reducing the height.
                     * See the comment above FOR_RECTS in gxcldev.h.
                     */
                    re.height >>= 1;
                    goto copy;
                } else {
                    /* Split a single (very long) row. */
                    int w2 = w1 >> 1;

                    ++cdev->driver_call_nesting;
                    {
                        code = clist_copy_alpha_hl_color(dev, row, dx,
                                                raster, gx_no_bitmap_id, rx, re.y,
                                                w2, 1, pdcolor, depth);
                        if (code >= 0)
                            code = clist_copy_alpha_hl_color(dev, row, dx + w2,
                                                    raster, gx_no_bitmap_id,
                                                    rx + w2, re.y, w1 - w2, 1,
                                                    pdcolor, depth);
                    }
                    --cdev->driver_call_nesting;
                    if (code < 0 && SET_BAND_CODE(code))
                        goto error_in_rect;
                    continue;
                }
            }
            op += compress;
            if (dx) {
                *dp++ = cmd_count_op(cmd_opv_set_misc, 2, dev->memory);
                *dp++ = cmd_set_misc_data_x + dx;
            }
            *dp++ = cmd_count_op(op, csize, dev->memory);
            *dp++ = depth;
            cmd_put2w(rx, re.y, dp);
            cmd_put2w(w1, re.height, dp);
            re.pcls->rect = rect;
        }
        continue;
error_in_rect:
        if (!(cdev->error_is_retryable && cdev->driver_call_nesting == 0 &&
                SET_BAND_CODE(clist_VMerror_recover_flush(cdev, re.band_code)) >= 0))
            return re.band_code;
        re.y -= re.height;
    } while ((re.y += re.height) < re.yend);
    return 0;
}

int
clist_copy_alpha(gx_device * dev, const byte * data, int data_x,
           int raster, gx_bitmap_id id, int rx, int ry, int rwidth, int rheight,
                 gx_color_index color, int depth)
{
    gx_device_clist_writer * const cdev =
        &((gx_device_clist *)dev)->writer;
    /* I don't like copying the entire body of clist_copy_color */
    /* just to change 2 arguments and 1 opcode, */
    /* but I don't see any alternative that doesn't require */
    /* another level of procedure call even in the common case. */
    int log2_depth = ilog2(depth);
    int y0;
    int data_x_bit;
    cmd_rects_enum_t re;
    gx_color_usage_bits color_usage = gx_color_index2usage(dev, color);

    /* If the target can't perform copy_alpha, exit now */
    if (depth > 1 && (cdev->disable_mask & clist_disable_copy_alpha) != 0)
        return_error(gs_error_unknownerror);

    fit_copy(dev, data, data_x, raster, id, rx, ry, rwidth, rheight);
    y0 = ry;
    data_x_bit = data_x << log2_depth;
    if (cdev->permanent_error < 0)
      return (cdev->permanent_error);
    /* If needed, update the trans_bbox */
    if (cdev->pdf14_needed) {
        gs_int_rect bbox;

        bbox.p.x = rx;
        bbox.q.x = rx + rwidth - 1;
        bbox.p.y = ry;
        bbox.q.y = ry + rheight - 1;

        clist_update_trans_bbox(cdev, &bbox);
    }
    RECT_ENUM_INIT(re, ry, rheight);
    do {
        int dx = (data_x_bit & 7) >> log2_depth;
        int w1 = dx + rwidth;
        const byte *row = data + (re.y - y0) * raster + (data_x_bit >> 3);
        int code;

        RECT_STEP_INIT(re);
        re.pcls->color_usage.or |= color_usage;
        do {
            code = cmd_disable_lop(cdev, re.pcls);
            if (code >= 0)
                code = cmd_disable_clip(cdev, re.pcls);
        } while (RECT_RECOVER(code));
        if (code < 0 && SET_BAND_CODE(code))
            goto error_in_rect;
        if (!re.pcls->color_is_alpha) {
            byte *dp;

            do {
                code =
                    set_cmd_put_op(dp, cdev, re.pcls, cmd_opv_set_copy_alpha, 1);
            } while (RECT_RECOVER(code));
            if (code < 0 && SET_BAND_CODE(code))
                goto error_in_rect;
            re.pcls->color_is_alpha = 1;
        }
        if (re.pcls->color_is_devn) {
            byte *dp;

            do {
                code = set_cmd_put_op(dp, cdev, re.pcls, cmd_opv_extend, 1);
                code = set_cmd_put_op(dp, cdev, re.pcls,
                                      cmd_opv_ext_unset_color_is_devn, 1);
            } while (RECT_RECOVER(code));
            if (code < 0 && SET_BAND_CODE(code))
                goto error_in_rect;
            re.pcls->color_is_alpha = 1;
        }








        if (color != re.pcls->colors[1]) {
            do {
                code = cmd_set_color1(cdev, re.pcls, color);
            } while (RECT_RECOVER(code));
            if (code < 0 && SET_BAND_CODE(code))
                goto error_in_rect;
        }
copy:{
            gx_cmd_rect rect;
            int rsize;
            byte op = (byte) cmd_op_copy_color_alpha;
            byte *dp;
            uint csize;
            uint compress;

            rect.x = rx, rect.y = re.y;
            rect.width = w1, rect.height = re.height;
            rsize = (dx ? 4 : 2) + cmd_size_rect(&rect);
            do {
                code = cmd_put_bits(cdev, re.pcls, row, w1 << log2_depth,
                                    re.height, raster, rsize,
                                    1 << cmd_compress_rle, &dp, &csize);
            } while (RECT_RECOVER(code));
            if (code < 0 && !(code == gs_error_limitcheck) && SET_BAND_CODE(code))
                goto error_in_rect;
            compress = (uint)code;
            if (code < 0) {
                /* The bitmap was too large; split up the transfer. */
                if (re.height > 1) {
                    /* Split the transfer by reducing the height.
                     * See the comment above FOR_RECTS in gxcldev.h.
                     */
                    re.height >>= 1;
                    goto copy;
                } else {
                    /* Split a single (very long) row. */
                    int w2 = w1 >> 1;

                    ++cdev->driver_call_nesting;
                    {
                        code = clist_copy_alpha(dev, row, dx,
                                                raster, gx_no_bitmap_id, rx, re.y,
                                                w2, 1, color, depth);
                        if (code >= 0)
                            code = clist_copy_alpha(dev, row, dx + w2,
                                                    raster, gx_no_bitmap_id,
                                                    rx + w2, re.y, w1 - w2, 1,
                                                    color, depth);
                    }
                    --cdev->driver_call_nesting;
                    if (code < 0 && SET_BAND_CODE(code))
                        goto error_in_rect;
                    continue;
                }
            }
            op += compress;
            if (dx) {
                *dp++ = cmd_count_op(cmd_opv_set_misc, 2, dev->memory);
                *dp++ = cmd_set_misc_data_x + dx;
            }
            *dp++ = cmd_count_op(op, csize, dev->memory);
            *dp++ = depth;
            cmd_put2w(rx, re.y, dp);
            cmd_put2w(w1, re.height, dp);
            re.pcls->rect = rect;
        }
        continue;
error_in_rect:
        if (!(cdev->error_is_retryable && cdev->driver_call_nesting == 0 &&
                SET_BAND_CODE(clist_VMerror_recover_flush(cdev, re.band_code)) >= 0))
            return re.band_code;
        re.y -= re.height;
    } while ((re.y += re.height) < re.yend);
    return 0;
}

int
clist_strip_copy_rop(gx_device * dev,
             const byte * sdata, int sourcex, uint sraster, gx_bitmap_id id,
                     const gx_color_index * scolors,
           const gx_strip_bitmap * textures, const gx_color_index * tcolors,
                     int rx, int ry, int rwidth, int rheight,
                     int phase_x, int phase_y, gs_logical_operation_t lop)
{
    return clist_strip_copy_rop2(dev, sdata, sourcex, sraster, id,
                                 scolors, textures, tcolors,
                                 rx, ry, rwidth, rheight, phase_x, phase_y,
                                 lop, 0);
}

int
clist_strip_copy_rop2(gx_device * dev,
             const byte * sdata, int sourcex, uint sraster, gx_bitmap_id id,
                     const gx_color_index * scolors,
           const gx_strip_bitmap * textures, const gx_color_index * tcolors,
                     int rx, int ry, int rwidth, int rheight,
                     int phase_x, int phase_y, gs_logical_operation_t lop,
                     uint planar_height)
{
    gx_device_clist_writer * const cdev =
        &((gx_device_clist *)dev)->writer;
    gs_rop3_t rop = lop_rop(lop);
    gx_strip_bitmap tile_with_id;
    const gx_strip_bitmap *tiles = textures;
    int y0;
    /* Compute the set of possible colors that this operation can generate. */
    gx_color_usage_bits all = gx_color_usage_all(cdev);
    bool subtractive = dev->color_info.num_components >= 4; /****** HACK ******/
    gx_color_usage_bits S =
        (scolors ? gx_color_index2usage(dev, scolors[0] | scolors[1]) : sdata ? all : 0);
    gx_color_usage_bits T =
        (tcolors ? gx_color_index2usage(dev, tcolors[0] | tcolors[1]) : textures ? all : 0);
    gs_rop3_t color_rop =
        (subtractive ? byte_reverse_bits[rop ^ 0xff] : rop);
    bool slow_rop;
    cmd_rects_enum_t re;

    if (scolors != 0 && scolors[0] != scolors[1]) {
        crop_fill(cdev, rx, ry, rwidth, rheight);
    } else {
        crop_copy(cdev, sdata, sourcex, sraster, id, rx, ry, rwidth, rheight);
    }
    if (rwidth <= 0 || rheight <= 0)
        return 0;

    /* If needed, update the trans_bbox */
    if (cdev->pdf14_needed) {
        gs_int_rect bbox;

        bbox.p.x = rx;
        bbox.q.x = rx + rwidth - 1;
        bbox.p.y = ry;
        bbox.q.y = ry + rheight - 1;

        clist_update_trans_bbox(cdev, &bbox);
    }
    /*
     * On CMYK devices, RasterOps must be executed with complete pixels
     * if the operation involves the destination.
     * This is because the black plane interacts with the other planes
     * in the conversion between RGB and CMYK.  Check for this now.
     */
    {
        gs_rop3_t rop_used = rop;

        if (scolors && (scolors[0] == scolors[1]))
            rop_used = (scolors[0] == gx_device_black(dev) ?
                        rop3_know_S_0(rop_used) :
                        scolors[0] == gx_device_white(dev) ?
                        rop3_know_S_1(rop_used) : rop_used);
        if (tcolors && (tcolors[0] == tcolors[1]))
            rop_used = (tcolors[0] == gx_device_black(dev) ?
                        rop3_know_T_0(rop_used) :
                        tcolors[0] == gx_device_white(dev) ?
                        rop3_know_T_1(rop_used) : rop_used);
        slow_rop = !(rop == rop3_0 || rop == rop3_1 ||
                     rop == rop3_S || rop == rop3_T);
    }
    y0 = ry;
    if (cdev->permanent_error < 0)
      return (cdev->permanent_error);
    /* If needed, update the trans_bbox */
    if (cdev->pdf14_needed) {
        gs_int_rect bbox;

        bbox.p.x = rx;
        bbox.q.x = rx + rwidth - 1;
        bbox.p.y = ry;
        bbox.q.y = ry + rheight - 1;

        clist_update_trans_bbox(cdev, &bbox);
    }
    RECT_ENUM_INIT(re, ry, rheight);
    do {
        const byte *row = sdata + (re.y - y0) * sraster;
        int code;

        RECT_STEP_INIT(re);
        /* rops are defined to work on rop_operands, which are typically 32
         * bits (unsigned long). We need to use gx_color_usage_bits, which
         * are usually 64 bits. So, the following funky bit of code; although
         * this is an if rather than a #if (as sizeof cannot be evaluated at
         * preprocess time), it should resolve properly at compile time. */
        if (sizeof(rop_operand) >= sizeof(gx_color_usage_bits)) {
            gx_color_usage_bits D = re.pcls->color_usage.or;
            re.pcls->color_usage.or =
                    ((rop_proc_table[color_rop])((rop_operand)D, (rop_operand)S, (rop_operand)T) & all) | D;
        } else if (2*sizeof(rop_operand) >= sizeof(gx_color_usage_bits)) {
            rop_operand D;
            D = (rop_operand)re.pcls->color_usage.or;
            re.pcls->color_usage.or |=
                    ((rop_proc_table[color_rop])(D, (rop_operand)S, (rop_operand)T) & all);
#define DOWN(x) ((rop_operand)(((x)>>(4*sizeof(rop_operand)))>>(4*sizeof(rop_operand))))
#define UP(x)   ((((gx_color_usage_bits)(x))<<(4*sizeof(rop_operand)))<<(4*sizeof(rop_operand)))
            D = DOWN(re.pcls->color_usage.or);
            re.pcls->color_usage.or |=
                    UP((rop_proc_table[color_rop])(D, DOWN(S), DOWN(T))) & all;
#undef DOWN
#undef UP
        }
        re.pcls->color_usage.slow_rop |= slow_rop;
        if (rop3_uses_T(rop)) {
            if (tcolors == 0 || tcolors[0] != tcolors[1]) {
                ulong offset_temp;

                if (!cls_has_tile_id(cdev, re.pcls, tiles->id, offset_temp)) {
                    /* Change tile.  If there is no id, generate one. */
                    if (tiles->id == gx_no_bitmap_id) {
                        tile_with_id = *tiles;
                        tile_with_id.id = gs_next_ids(dev->memory, 1);
                        tiles = &tile_with_id;
                    }
                    do {
                        code = clist_change_tile(cdev, re.pcls, tiles,
                                                 (tcolors != 0 ? 1 :
                                                  cdev->clist_color_info.depth));
                    } while (RECT_RECOVER(code));
                    if (code < 0 && !(code == gs_error_limitcheck) && SET_BAND_CODE(code))
                        goto error_in_rect;
                    if (code < 0) {
                        /*
                         * The error is a limitcheck: we have a tile that
                         * is too big to fit in the command reading buffer.
                         * For now, just divide up the transfer into scan
                         * lines.  (If a single scan line won't fit, punt.)
                         * Eventually, we'll need a way to transfer the tile
                         * in pieces.
                         */
                        uint rep_height = tiles->rep_height;
                        gs_id ids;
                        gx_strip_bitmap line_tile;
                        int data_shift = 0, phase_shift = 0, raster;
                        int new_phase = phase_x;
                        int tile_space_phase;
                        int iy;

                        if (rep_height == 1 ||
                            /****** CAN'T HANDLE SHIFT YET ******/
                            tiles->rep_shift != 0
                            )
                            return code;
                        /*
                         * Allocate enough fake IDs, since the inner call on
                         * clist_strip_copy_rop will need them anyway.
                         */
                        ids = gs_next_ids(dev->memory, min(re.height, rep_height));
                        line_tile = *tiles;
                        line_tile.size.y = 1;
                        line_tile.rep_height = 1;
                        raster = line_tile.raster;
                        if (line_tile.num_planes > 0)
                            line_tile.raster *= tiles->size.y;
                        /* The rasterizer assumes tile phase relatively to the rectangle origin,
                           (see x_offset in gdevmr8n.c), so compute "the tile phase in the tile space"
                           with same expression : */
                        tile_space_phase = (rx + phase_x) % tiles->rep_width;
                        if (tile_space_phase + rwidth <= tiles->rep_width) {
                            /* Narrow the tile to prevent buffer overflow - bug 689588.
                               Note currently we don't narrow "wrapped" tiles (because bug 689588 doesn't need) :
                               when tile_space_phase < rep_width && tile_space_phase + rwidth > rep_width,
                               each line to be converted into 2 ones.
                            */
                            int depth = cdev->clist_color_info.depth;

                            if (line_tile.num_planes > 0)
                                depth /= line_tile.num_planes;
#			    if 0
                            /* Align bitmap data : */
                            data_shift = ((tile_space_phase * depth) >> (log2_align_bitmap_mod + 3)) << log2_align_bitmap_mod;
#			    else
                            data_shift = tile_space_phase * depth / 8; /* No bitmap data alignment because we'll only write it to clist.  */
#			    endif
                            phase_shift = data_shift * 8 / depth;
                            line_tile.rep_width = rwidth + (tile_space_phase - phase_shift);
                            /* Normally line_tile.raster must account trailing row alignment bytes and
                               to be computed as bitmap_raster(line_tile.rep_width * depth);
                               but we can't apply it here because the trailing alignment bytes may be absent
                               due to data_shift. We believe it is not harmful because we just write the data to clist,
                               and because the bitmap height is 1.
                               The clist reader must provide the trailing bytes if the rasterizer needs them.
                             */
                            if (line_tile.num_planes <= 1)
                                line_tile.raster = (line_tile.rep_width * depth + 7) / 8;
                            line_tile.size.x = line_tile.rep_width;
                            line_tile.shift = 0;
                            new_phase = (tile_space_phase - phase_shift - rx % line_tile.rep_width);
                            /* Provide a positive phase for clist reader : */
                            new_phase = (new_phase + line_tile.rep_width) % line_tile.rep_width;
                        }
                        for (iy = 0; iy < re.height; ++iy) {
                            line_tile.data = tiles->data + raster *
                                ((re.y + iy + phase_y) % rep_height) + data_shift;
                            line_tile.id = ids + (iy % rep_height);
                            ++cdev->driver_call_nesting;
                            {
                                code = clist_strip_copy_rop2(dev,
                                        (sdata == 0 ? 0 : row + iy * sraster),
                                        sourcex, sraster,
                                        gx_no_bitmap_id, scolors,
                                        &line_tile, tcolors,
                                        rx, re.y + iy, rwidth, 1,
                                        new_phase, 0, lop, planar_height);
                            }
                            --cdev->driver_call_nesting;
                            if (code < 0 && SET_BAND_CODE(code))
                                goto error_in_rect;
                        }
                        continue;
                    }
                    if (((phase_x != re.pcls->tile_phase.x) && (tiles->rep_width > 1)) ||
                        ((phase_y != re.pcls->tile_phase.y) && (tiles->rep_height > 1))) {
                        do {
                            code = cmd_set_tile_phase(cdev, re.pcls, phase_x,
                                                      phase_y);
                        } while (RECT_RECOVER(code));
                        if (code < 0 && SET_BAND_CODE(code))
                            goto error_in_rect;
                    }
                }
            }
            /* Set the tile colors. */
            do {
                code =
                    (tcolors != 0 ?
                     cmd_set_tile_colors(cdev, re.pcls, tcolors[0], tcolors[1]) :
                     cmd_set_tile_colors(cdev, re.pcls, gx_no_color_index,
                                         gx_no_color_index));
            } while (RECT_RECOVER(code));
            if (code < 0 && SET_BAND_CODE(code))
                goto error_in_rect;
        }
        do {
            code = 0;
            if (lop != re.pcls->lop)
                code = cmd_set_lop(cdev, re.pcls, lop);
            if (code >= 0)
                code = cmd_enable_lop(cdev, re.pcls);
        } while (RECT_RECOVER(code));
        if (code < 0 && SET_BAND_CODE(code))
            goto error_in_rect;
        /* Set lop_enabled to -1 so that fill_rectangle / copy_* */
        /* won't attempt to set it to 0. */
        re.pcls->lop_enabled = -1;
        ++cdev->driver_call_nesting;
        {
            if (scolors != NULL) {
                if (scolors[0] == scolors[1])
                    code = clist_fill_rectangle(dev, rx, re.y, rwidth, re.height,
                                                scolors[1]);
                else
                    code = clist_copy_mono(dev, row, sourcex, sraster, id,
                                           rx, re.y, rwidth, re.height,
                                           scolors[0], scolors[1]);
            } else if (planar_height) {
                code = clist_copy_planes(dev, row, sourcex, sraster, id,
                                         rx, re.y, rwidth, re.height,
                                         planar_height);
            } else {
                code = clist_copy_color(dev, row, sourcex, sraster, id,
                                        rx, re.y, rwidth, re.height);
            }
        }
        --cdev->driver_call_nesting;
        re.pcls->lop_enabled = 1;
        if (code < 0 && SET_BAND_CODE(code))
            goto error_in_rect;
        continue;
error_in_rect:
        if (!(cdev->error_is_retryable && cdev->driver_call_nesting == 0 &&
                SET_BAND_CODE(clist_VMerror_recover_flush(cdev, re.band_code)) >= 0))
            return re.band_code;
        re.y -= re.height;
    } while ((re.y += re.height) < re.yend);
    return 0;
}
