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


/* pxink.c */
/* PCL XL ink setting operators */

#include "math_.h"
#include "stdio_.h"                     /* for NULL */
#include "memory_.h"
#include "gdebug.h"
#include "gstypes.h"
#include "gsmemory.h"
#include "pxoper.h"
#include "pxstate.h"
#include "gxarith.h"
#include "gsstate.h"
#include "gxcspace.h"                   /* must precede gscolor2.h */
#include "gscolor2.h"
#include "gscoord.h"
#include "gsimage.h"
#include "gscie.h"
#include "gscrd.h"
#include "gspath.h"
#include "gxdevice.h"
#include "gxht.h"
#include "gxstate.h"
#include "pldraw.h"
#include "plsrgb.h"
#include "plht.h"
#include "pxptable.h"

/*
 * Contrary to the documentation, SetColorSpace apparently doesn't set the
 * brush or pen to black.  To produce this behavior, uncomment the
 * following #define.
 */
#define SET_COLOR_SPACE_NO_SET_BLACK

/* ---------------- Utilities ---------------- */

/* ------ Halftones ------ */

/* Define a transfer function without gamma correction. */
static float
identity_transfer(floatp tint, const gx_transfer_map *ignore_map)
{       return tint;
}

/* Set the default halftone screen. */
static const byte order16x16[256] = {
#if 0
  /*
   * The following is a standard 16x16 ordered dither, except that
   * the very last pass goes in the order (0,1,2,3) rather than
   * (0,3,1,2).  This leads to excessive cancellation when the source
   * and paint halftones interact, but it's better than the standard
   * order, which has inadequate cancellation.
   *
   * This matrix is generated by the following call:
   *    [ <00 88 08 80> <00 44 04 40> <00 22 02 20> <00 01 10 11> ]
   *    { } makedither
   */
 0,64,32,96,8,72,40,104,2,66,34,98,10,74,42,106,
 128,192,160,224,136,200,168,232,130,194,162,226,138,202,170,234,
 48,112,16,80,56,120,24,88,50,114,18,82,58,122,26,90,
 176,240,144,208,184,248,152,216,178,242,146,210,186,250,154,218,
 12,76,44,108,4,68,36,100,14,78,46,110,6,70,38,102,
 140,204,172,236,132,196,164,228,142,206,174,238,134,198,166,230,
 60,124,28,92,52,116,20,84,62,126,30,94,54,118,22,86,
 188,252,156,220,180,244,148,212,190,254,158,222,182,246,150,214,
 3,67,35,99,11,75,43,107,1,65,33,97,9,73,41,105,
 131,195,163,227,139,203,171,235,129,193,161,225,137,201,169,233,
 51,115,19,83,59,123,27,91,49,113,17,81,57,121,25,89,
 179,243,147,211,187,251,155,219,177,241,145,209,185,249,153,217,
 15,79,47,111,7,71,39,103,13,77,45,109,5,69,37,101,
 143,207,175,239,135,199,167,231,141,205,173,237,133,197,165,229,
 63,127,31,95,55,119,23,87,61,125,29,93,53,117,21,85,
 191,255,159,223,183,247,151,215,189,253,157,221,181,245,149,213
#  define source_phase_x 1
#  define source_phase_y 1
#else
/*
 * The following is a 45 degree spot screen with the spots enumerated
 * in a defined order.  This matrix is generated by the following call:

/gamma_transfer {
  dup dup 0 le exch 1 ge or not {
    dup 0.17 lt
     { 3 mul }
     { dup 0.35 lt { 0.78 mul 0.38 add } { 0.53 mul 0.47 add } ifelse }
   ifelse
  } if
} def

[ [0 136 8 128 68 204 76 196]
  [18 33 17 34   1 2 19 35 50 49 32 16   3 48 0 51
   -15 -14 20 36 66 65 47 31   -13 64 15 52   -30 81 30 37]
] /gamma_transfer load makedither

 */
 38,11,14,32,165,105,90,171,38,12,14,33,161,101,88,167,
 30,6,0,16,61,225,231,125,30,6,1,17,63,222,227,122,
 27,3,8,19,71,242,205,110,28,4,9,20,74,246,208,106,
 35,24,22,40,182,46,56,144,36,25,22,41,186,48,58,148,
 152,91,81,174,39,12,15,34,156,95,84,178,40,13,16,34,
 69,212,235,129,31,7,2,18,66,216,239,133,32,8,2,18,
 79,254,203,114,28,4,10,20,76,250,199,118,29,5,10,21,
 193,44,54,142,36,26,23,42,189,43,52,139,37,26,24,42,
 39,12,15,33,159,99,87,169,38,11,14,33,163,103,89,172,
 31,7,1,17,65,220,229,123,30,6,1,17,62,223,233,127,
 28,4,9,20,75,248,210,108,27,3,9,19,72,244,206,112,
 36,25,23,41,188,49,60,150,35,25,22,41,184,47,57,146,
 157,97,85,180,40,13,16,35,154,93,83,176,39,13,15,34,
 67,218,240,135,32,8,3,19,70,214,237,131,31,7,2,18,
 78,252,197,120,29,5,11,21,80,255,201,116,29,5,10,21,
 191,43,51,137,37,27,24,43,195,44,53,140,37,26,23,42
#  define source_phase_x 4
#  define source_phase_y 0
#endif
};

/* Set the size for a default halftone screen. */
static void
px_set_default_screen_size(px_state_t *pxs, int method)
{       px_gstate_t *pxgs = pxs->pxgs;

        pxgs->halftone.width = pxgs->halftone.height = 16;
}

/* If necessary, set the halftone in the graphics state. */
int
px_set_halftone(px_state_t *pxs)
{
    px_gstate_t *pxgs = pxs->pxgs;
    int code;

    if ( pxgs->halftone.set )
        return 0;
    if ( pxgs->halftone.method != eDownloaded ) {
        gs_string thresh;
        thresh.data = (byte *)order16x16;
        thresh.size = 256;
        code = pl_set_pcl_halftone(pxs->pgs,
                                   /* set transfer */ identity_transfer,
                                   /* width */ 16, /*height */ 16,
                                   /* dither data */ thresh,
                                   /* x phase */ (int)pxgs->halftone.origin.x,
                                   /* y phase */ (int)pxgs->halftone.origin.y);
    } else { /* downloaded */
        int ht_width, ht_height;
        switch ( pxs->orientation ) {
            case ePortraitOrientation:
            case eReversePortrait:
                ht_width = pxgs->halftone.width;
                ht_height = pxgs->halftone.height;
                break;
            case eLandscapeOrientation:
            case eReverseLandscape:
                ht_width = pxgs->halftone.height;
                ht_height = pxgs->halftone.width;
                break;
            default:
                return -1;
        }

        code = pl_set_pcl_halftone(pxs->pgs,
                                   /* set transfer */ identity_transfer,
                                   /* width */ ht_width, /*height */ ht_height,
                                   /* dither data */ pxgs->halftone.thresholds,
                                   /* x phase */ (int)pxgs->halftone.origin.x,
                                   /* y phase */ (int)pxgs->halftone.origin.y);
        if ( code < 0 )
            gs_free_string(pxs->memory, pxgs->halftone.thresholds.data,
                           pxgs->halftone.thresholds.size,
                           "px_set_halftone(thresholds)");
        else {
            gs_free_string(pxs->memory, (byte *)pxgs->dither_matrix.data,
                           pxgs->dither_matrix.size,
                           "px_set_halftone(dither_matrix)");
            pxgs->dither_matrix = pxgs->halftone.thresholds;
        }
        pxgs->halftone.thresholds.data = 0;
        pxgs->halftone.thresholds.size = 0;
    }
    if ( code < 0 )
        return code;
    pxgs->halftone.set = true;
    /* Cached patterns have already been halftoned, so clear the cache. */
    px_purge_pattern_cache(pxs, eSessionPattern);
    return 0;
}

/* ------ Patterns ------ */

/*
 * The library caches patterns in their fully rendered form, i.e., after
 * halftoning.  In order to avoid seams or anomalies, we have to replicate
 * the pattern so that its size is an exact multiple of the halftone size.
 */

static uint
ilcm(uint x, uint y)
{       return x * (y / igcd(x, y));
}

/* Render a pattern. */
static int
px_paint_pattern(const gs_client_color *pcc, gs_state *pgs)
{       
    const gs_client_pattern *ppat = gs_getpattern(pcc);
    const px_pattern_t *pattern = ppat->client_data;
    const byte *dp = pattern->data;
    gs_image_enum *penum;
    gs_image_t image;
    int code;
    int num_components =
        (pattern->params.indexed || pattern->params.color_space == eGray ?
         1 : 3);
    uint rep_width = pattern->params.width;
    uint rep_height = pattern->params.height;
    uint full_width = (uint)ppat->XStep;
    uint full_height = (uint)ppat->YStep;
    uint bits_per_row, bytes_per_row;
    int x;

    code = px_image_color_space(&image, &pattern->params, &pattern->palette, pgs);
    if ( code < 0 )
        return code;

    bits_per_row = rep_width * image.BitsPerComponent * num_components;
    bytes_per_row = (bits_per_row + 7) >> 3;
    /*
     * As noted above, in general, we have to replicate the original
     * pattern to a multiple that avoids halftone seams.  If the
     * number of bits per row is a multiple of 8, we can do this with
     * a single image; otherwise, we need one image per X replica.
     * To simplify the code, we always use the (slightly) slower method.
     */
    image.Width = rep_width;
    image.Height = full_height;
    image.CombineWithColor = true;
    for ( x = 0; x < full_width; x += rep_width ) { 
        int y;

        image.ImageMatrix.tx = (float)-x;
        code = pl_begin_image2(&penum, &image, pgs);
        if ( code < 0 )
            break;
        for ( y = 0; code >= 0 && y < full_height; ++y ) { 
            const byte *row = dp + (y % rep_height) * bytes_per_row;
            uint used; /* better named not_used */
            code = pl_image_data2(penum, row, bytes_per_row, &used);
            if (code < 0)
                break;
        }
        pl_end_image2(penum, pgs);
    }
    return code;
}

/* Create the rendering of a pattern. */
static int
render_pattern(gs_client_color *pcc, const px_pattern_t *pattern,
  const px_value_t *porigin, const px_value_t *pdsize, px_state_t *pxs)
{       px_gstate_t *pxgs = pxs->pxgs;
        uint rep_width = pattern->params.width;
        uint rep_height = pattern->params.height;
        uint full_width, full_height;
        gs_state *pgs = pxs->pgs;
        gs_client_pattern templat;

        /*
         * If halftoning may occur, replicate the pattern so we don't get
         * halftone seams.
         */
        { gx_device *dev = gs_currentdevice(pgs);
          if (!gx_device_must_halftone(dev))
            { /* No halftoning. */
              full_width = rep_width;
              full_height = rep_height;
            }
          else
            { full_width = ilcm(rep_width, pxgs->halftone.width);
              full_height = ilcm(rep_height, pxgs->halftone.height);
              /*
               * If the pattern would be enormous, don't replicate it.
               * This is a HACK.
               */
              if ( full_width > 10000 )
                full_width = rep_width;
              if ( full_height > 10000 )
                full_height = rep_height;
            }
        }
        /* Construct a Pattern for the library, and render it. */
        gs_pattern1_init(&templat);
        uid_set_UniqueID(&templat.uid, pattern->id);
        templat.PaintType = 1;
        templat.TilingType = 1;
        templat.BBox.p.x = 0;
        templat.BBox.p.y = 0;
        templat.BBox.q.x = full_width;
        templat.BBox.q.y = full_height;
        templat.XStep = (float)full_width;
        templat.YStep = (float)full_height;
        templat.PaintProc = px_paint_pattern;
        templat.client_data = (void *)pattern;
        { gs_matrix mat;
          gs_point dsize;
          int code;

          if ( porigin )
            gs_make_translation(real_value(porigin, 0),
                                real_value(porigin, 1), &mat);
          else
            gs_make_identity(&mat);

          if ( pdsize )
            { dsize.x = real_value(pdsize, 0);
              dsize.y = real_value(pdsize, 1);
            }
          else
            { dsize.x = pattern->params.dest_width;
              dsize.y = pattern->params.dest_height;
            }
          gs_matrix_scale(&mat, dsize.x / rep_width, dsize.y / rep_height,
                          &mat);
          /*
           * gs_makepattern will make a copy of the current gstate.
           * We don't want this copy to contain any circular back pointers
           * to px_pattern_ts: such pointers are unnecessary, because
           * px_paint_pattern doesn't use the pen and brush (in fact,
           * it doesn't even reference the px_gstate_t).  We also want to
           * reset the path and clip path.  The easiest (although not by
           * any means the most efficient) way to do this is to do a gsave,
           * reset the necessary things, do the makepattern, and then do
           * a grestore.
           */
          code = gs_gsave(pgs);
          if ( code < 0 )
            return code;
          { px_gstate_t *pxgs = pxs->pxgs;

            px_gstate_rc_adjust(pxgs, -1, pxgs->memory);
            pxgs->brush.type = pxgs->pen.type = pxpNull;
            gs_newpath(pgs);
            px_initclip(pxs);
          }
          {
              gs_color_space *pcs;
              /* set the color space */
              switch ( pattern->params.color_space ) {
              case eGray:
                  pcs = gs_cspace_new_DeviceGray(pxgs->memory);
                  if ( pcs == NULL )
                      return_error(errorInsufficientMemory);
                  break;
              case eRGB:
                  pcs = gs_cspace_new_DeviceRGB(pxgs->memory);
                  if ( pcs == NULL )
                      return_error(errorInsufficientMemory);
                  break;
              case eSRGB:
              case eCRGB:
                  if ( pl_cspace_init_SRGB(&pcs, pgs) < 0 )
                      /* should not happen */
                      return_error(errorInsufficientMemory);
                  break;
              default:
                  return_error(errorIllegalAttributeValue);
              }
              gs_setcolorspace(pgs, pcs);
          }
          code = gs_makepattern(pcc, &templat, &mat, pgs, NULL);
          gs_grestore(pgs);
          return code;
        }
}

/* ------ Brush/pen ------ */

/* Check parameters and execute SetBrushSource or SetPenSource: */
/* pxaRGBColor, pxaGrayLevel, pxaNullBrush/Pen, pxaPatternSelectID, */
/* pxaPatternOrigin, pxaNewDestinationSize */
static ulong
int_type_max(px_data_type_t type)
{       return
          (type & pxd_ubyte ? 255 :
           type & pxd_uint16 ? 65535 :
           type & pxd_sint16 ? 32767 :
           type & pxd_uint32 ? (ulong)0xffffffff :
           /* type & pxd_sint32 */ 0x7fffffff);
}
static real
fraction_value(const px_value_t *pv, int i)
{       px_data_type_t type = pv->type;
        real v;

        if ( type & pxd_real32 )
          return pv->value.ra[i];
        v = (real)pv->value.ia[i];
        return (v < 0 ? 0 : v / int_type_max(type));
}

/* we use an enumeration instead of index numbers improve readability in this
   "very busy" routine */
typedef enum {
    aRGBColor = 0,
    aGrayLevel = 1,
    aPrimaryArray = 2,
    aPrimaryDepth = 3,
    aNullBrushPen = 4,
    aPatternSelectID = 5,
    aPatternOrigin = 6,
    aNewDestinationSize = 7
} pxl_source_t;

/* given a paint type decide if a halftone is necessary */
static bool
px_needs_halftone(const gs_memory_t *mem, px_paint_t *ppt)
{
    bool needs_halftone;
    if ( ppt->type == pxpPattern )
        needs_halftone = true;
    else if ( ppt->type == pxpNull )
        needs_halftone = false;
    else if ( ppt->type == pxpGray )
        needs_halftone = ppt->value.gray != 0 && ppt->value.gray != 1;
    else if ( ppt->type == pxpRGB || ppt->type == pxpSRGB ) { /* rgb or srgb */
        int i;
        needs_halftone = false;
        for ( i = 0; i < 3; i++ ) {
            if ( ppt->value.rgb[i] != 0 && ppt->value.rgb[i] != 1 ) {
                needs_halftone = true;
                break;
            }
        }
    } else {
        dmprintf(mem, "unknown paint type\n");
        needs_halftone = true;
    }
    return needs_halftone;
}

static int
set_source(const px_args_t *par, px_state_t *pxs, px_paint_t *ppt)
{
    px_gstate_t *pxgs = pxs->pxgs;
    int code = 0;
    /* pxaPatternSelectID */
    if ( par->pv[aPatternSelectID] ) {
        px_value_t key;
        void *value;
        px_pattern_t *pattern;
        gs_client_color ccolor;
        int code;
        if ( par->pv[aRGBColor] || par->pv[aGrayLevel] || par->pv[aNullBrushPen] )
            return_error(errorIllegalAttributeCombination);
        key.type = pxd_array | pxd_ubyte;
        key.value.array.data = (byte *)&par->pv[aPatternSelectID]->value.i;
        key.value.array.size = sizeof(int32_t);
        if ( !(px_dict_find(&pxgs->temp_pattern_dict, &key, &value) ||
               px_dict_find(&pxs->page_pattern_dict, &key, &value) ||
               px_dict_find(&pxs->session_pattern_dict, &key, &value))
             )
            return_error(errorRasterPatternUndefined);
        pattern = value;

        /* the HP 4700 error page for the pattern and graphic's state
           color space not matching is wrong, it reports "Error: 15", we
           use "ColorSpaceMismatch" which we suspect was hp
           intention */
        if (pattern->params.color_space != pxgs->color_space)
            return_error(errorColorSpaceMismatch);

        px_set_halftone(pxs);
        code = render_pattern(&ccolor, pattern, par->pv[aPatternOrigin],
                              par->pv[aNewDestinationSize], pxs);
        /*
         * We don't use px_paint_rc_adjust(... 1 ...) here, because
         * gs_makepattern creates pattern instances with a reference
         * count already set to 1.
         */
        rc_increment(pattern);
        if ( code < 0 )
            return code;
        px_paint_rc_adjust(ppt, -1, pxs->memory);
        ppt->type = pxpPattern;
        ppt->value.pattern.pattern = pattern;
        ppt->value.pattern.color = ccolor;
        /* not pxaPatternSelectID but we have a pattern origin or
           newdestination size */
    } else if ( par->pv[aPatternOrigin] || par->pv[aNewDestinationSize] ) {
        return_error(errorIllegalAttributeCombination);
    } else if ( par->pv[aRGBColor] ) {
        const px_value_t *prgb = par->pv[aRGBColor];
        int i;
        if ( par->pv[aGrayLevel] || par->pv[aNullBrushPen] )
            return_error(errorIllegalAttributeCombination);
        if ( pxgs->color_space != eRGB && pxgs->color_space != eSRGB )
            return_error(errorColorSpaceMismatch);
        px_paint_rc_adjust(ppt, -1, pxs->memory);
        if ( pxs->useciecolor )
            ppt->type = pxpSRGB;
        else
            ppt->type = pxpRGB;
        for ( i = 0; i < 3; ++i )
            if ( prgb->type & pxd_any_real )
                ppt->value.rgb[i] = real_elt(prgb, i);
            else {
                int32_t v = integer_elt(prgb, i);
                ppt->value.rgb[i] = (v < 0 ? 0 : (real)v / int_type_max(prgb->type));
            }
    } else if ( par->pv[aGrayLevel] )   /* pxaGrayLevel */ {
        if ( par->pv[aNullBrushPen] )
            return_error(errorIllegalAttributeCombination);
        if ( pxgs->color_space != eGray )
            return_error(errorColorSpaceMismatch);
        px_paint_rc_adjust(ppt, -1, pxs->memory);
        ppt->type = pxpGray;
        ppt->value.gray = fraction_value(par->pv[aGrayLevel], 0);
    } else if ( par->pv[aNullBrushPen] )        /* pxaNullBrush/Pen */ {
        px_paint_rc_adjust(ppt, -1, pxs->memory);
        ppt->type = pxpNull;
    } else if ( par->pv[aPrimaryDepth] && par->pv[aPrimaryArray] ) {
        px_paint_rc_adjust(ppt, -1, pxs->memory);
        if ( pxgs->color_space == eRGB )
            if ( pxs->useciecolor )
                ppt->type = pxpSRGB;
            else
                ppt->type = pxpRGB;
        else if ( pxgs->color_space == eGray )
            ppt->type = pxpGray;
        else if ( pxgs->color_space == eSRGB )
            ppt->type = pxpSRGB;
        else {
            dmprintf1(pxgs->memory, "Warning unknown color space %d\n", pxgs->color_space);
            ppt->type = pxpGray;
        }
        /* NB depth?? - for range checking */
        if ( ppt->type == pxpRGB || ppt->type == pxpSRGB ) {

            ppt->value.rgb[0] = (float)par->pv[aPrimaryArray]->value.array.data[0] / 255.0;
            ppt->value.rgb[1] = (float)par->pv[aPrimaryArray]->value.array.data[1] / 255.0;
            ppt->value.rgb[2] = (float)par->pv[aPrimaryArray]->value.array.data[2] / 255.0;
        } else
            /* NB figure out reals and ints */
            ppt->value.gray = (float)par->pv[aPrimaryArray]->value.array.data[0] / 255.0;
    } else
        return_error(errorMissingAttribute);
    /*
     * Update the halftone to the most recently set one.
     * This will do the wrong thing if we set the brush or pen source,
     * set the halftone, and then set the other source, but we have
     * no way to handle this properly with the current library.
     */
    if ( code >= 0 && px_needs_halftone(pxs->memory, ppt) )
        code = px_set_halftone(pxs);
    return code;
}

/* Set up a brush or pen for drawing. */
/* If it is a pattern, SetBrush/PenSource guaranteed that it is compatible */
/* with the current color space. */
int
px_set_paint(const px_paint_t *ppt, px_state_t *pxs)
{
    gs_state *pgs = pxs->pgs;
    px_paint_type_t type;

    if ( pxs->useciecolor && ppt->type == pxpRGB )
        type = pxpSRGB;
    else
        type = ppt->type;
    switch ( type ) {
    case pxpNull:
        gs_setnullcolor(pgs);
        return 0;
    case pxpRGB:
        return gs_setrgbcolor(pgs, ppt->value.rgb[0], ppt->value.rgb[1],
                              ppt->value.rgb[2]);
    case pxpGray:
        return gs_setgray(pgs, ppt->value.gray);
    case pxpPattern:
        return gs_setpattern(pgs, &ppt->value.pattern.color);
    case pxpSRGB:
        return pl_setSRGBcolor(pgs,
                               ppt->value.rgb[0],
                               ppt->value.rgb[1],
                               ppt->value.rgb[2]);
    default:            /* can't happen */
        return_error(errorIllegalAttributeValue);
    }
}

/* ---------------- Operators ---------------- */

const byte apxSetBrushSource[] = {
  0, pxaRGBColor, pxaGrayLevel, pxaPrimaryArray, pxaPrimaryDepth, pxaNullBrush, pxaPatternSelectID,
  pxaPatternOrigin, pxaNewDestinationSize, 0
};

int
pxSetBrushSource(px_args_t *par, px_state_t *pxs)
{       return set_source(par, pxs, &pxs->pxgs->brush);
}

const byte apxSetColorSpace[] = {
  0, pxaColorSpace, pxaColorimetricColorSpace, pxaXYChromaticities, pxaWhiteReferencePoint,
  pxaCRGBMinMax, pxaGammaGain, pxaPaletteDepth, pxaPaletteData, 0
};

/* it appears the 4600 does not support CRGB define this to enable support */
/* #define SUPPORT_COLORIMETRIC */

int
pxSetColorSpace(px_args_t *par, px_state_t *pxs)
{       px_gstate_t *pxgs = pxs->pxgs;
        pxeColorSpace_t cspace;

        if ( par->pv[0] )
            cspace = par->pv[0]->value.i;
        else if ( par->pv[1] )
            cspace = par->pv[1]->value.i;
        else
            return_error(errorIllegalAttributeValue);
#ifndef SUPPORT_COLORIMETRIC
        if ( cspace == eCRGB )
            /* oddly the 4600 reports this a missing attribute, not
               the expected illegal attribute */
            return_error(errorMissingAttribute);
#endif
        /* substitute srgb if cie color is in effect */
        if ( ( cspace == eRGB ) && pxs->useciecolor )
            cspace = eSRGB;
        if ( par->pv[6] && par->pv[7] )
          { int ncomp = (( cspace == eRGB || cspace == eSRGB || cspace == eCRGB ) ? 3 : 1);
            uint size = par->pv[7]->value.array.size;
            if ( !(size == ncomp << 1 || size == ncomp << 4 ||
                   size == ncomp << 8)
               )
                /* The HP printers we've tested appear to truncate
                   this value and not produce an error on overflow */
              if (size > ncomp << 8)
                  size = ncomp << 8;
              else
                  return_error(errorIllegalAttributeValue);
            /* The palette is in an array, but we want a string. */
            {
                if ( pxgs->palette.data && !pxgs->palette_is_shared &&
                   pxgs->palette.size != size
                 )
                    { gs_free_string(pxs->memory, (byte *)pxgs->palette.data,
                                     pxgs->palette.size,
                                     "pxSetColorSpace(old palette)");
                  pxgs->palette.data = 0;
                  pxgs->palette.size = 0;
                }
              if ( pxgs->palette.data == 0 || pxgs->palette_is_shared )
                { byte *pdata =
                    gs_alloc_string(pxs->memory, size,
                                    "pxSetColorSpace(palette)");

                  if ( pdata == 0 )
                    return_error(errorInsufficientMemory);
                  pxgs->palette.data = pdata;
                  pxgs->palette.size = size;
                }
              memcpy((void *)pxgs->palette.data, par->pv[7]->value.array.data, size);
            }
          }
        else if ( par->pv[6] || par->pv[7] )
          return_error(errorMissingAttribute);
        else if ( pxgs->palette.data )
          { if ( !pxgs->palette_is_shared )
                  gs_free_string(pxs->memory, (byte *)pxgs->palette.data,
                             pxgs->palette.size,
                             "pxSetColorSpace(old palette)");
            pxgs->palette.data = 0;
            pxgs->palette.size = 0;
          }
        pxgs->palette_is_shared = false;
        pxgs->color_space = cspace;
#ifndef SET_COLOR_SPACE_NO_SET_BLACK
          { px_paint_rc_adjust(&pxgs->brush, -1, pxs->memory);
            pxgs->brush.type = pxpGray;
            pxgs->brush.value.gray = 0;
          }
          { px_paint_rc_adjust(&pxgs->pen, -1, pxs->memory);
            pxgs->pen.type = pxpGray;
            pxgs->pen.value.gray = 0;
          }
#endif
        return 0;
}

const byte apxSetHalftoneMethod[] = {
   0, pxaDitherOrigin, pxaDeviceMatrix, pxaDitherMatrixDataType,
   pxaDitherMatrixSize, pxaDitherMatrixDepth, pxaAllObjectTypes,
   pxaTextObjects, pxaVectorObjects, pxaRasterObjects, 0
};

int
pxSetHalftoneMethod(px_args_t *par, px_state_t *pxs)
{       gs_state *pgs = pxs->pgs;
        px_gstate_t *pxgs = pxs->pxgs;
        pxeDitherMatrix_t method;

        if ( par->pv[6] || par->pv[7] || par->pv[8] || par->pv[9] )
            /* ignore object type arguments */
            return 0;

        if ( par->pv[1] )
          { /* Internal halftone */
            if ( par->pv[2] || par->pv[3] || par->pv[4] )
              return_error(errorIllegalAttributeCombination);
            method = par->pv[1]->value.i;
            px_set_default_screen_size(pxs, method);
            pxs->download_string.data = 0;
            pxs->download_string.size = 0;
          }
        else if ( par->pv[2] && par->pv[3] && par->pv[4] )
          { /* Dither matrix */
            uint width = par->pv[3]->value.ia[0];
            uint source_width = (width + 3) & ~3;
            uint height = par->pv[3]->value.ia[1];
            uint size = width * height;
            uint source_size = source_width * height;

            if ( par->source.position == 0 )
              { byte *data;
                if ( par->source.available == 0 )
                  return pxNeedData;
                data = gs_alloc_string(pxs->memory, size, "dither matrix");
                if ( data == 0 )
                  return_error(errorInsufficientMemory);
                pxs->download_string.data = data;
                pxs->download_string.size = size;
              }
            while ( par->source.position < source_size )
              { uint source_x = par->source.position % source_width;
                uint source_y = par->source.position / source_width;
                uint used;

                if ( par->source.available == 0 )
                  return pxNeedData;
                if ( source_x >= width )
                  { /* Skip padding bytes at end of row. */
                    used = min(par->source.available, source_width - source_x);
                  }
                else
                  { /* Read data. */
                    const byte *src = par->source.data;
                    byte *dest = pxs->download_string.data;
                    uint i;
                    int skip;

                    used = min(par->source.available, width - source_x);
                    /*
                     * The documentation doesn't say this, but we have to
                     * rotate the dither matrix to match the orientation,
                     * remembering that we have a Y-inverted coordinate
                     * system.  This is quite a nuisance!
                     */
                    switch ( pxs->orientation )
                      {
                      case ePortraitOrientation:
                        dest += source_y * width + source_x;
                        skip = 1;
                        break;
                      case eLandscapeOrientation:
                        dest += (width - 1 - source_x) * height + source_y;
                        skip = -height;
                        break;
                      case eReversePortrait:
                        dest += (height - 1 - source_y) * width +
                          width - 1 - source_x;
                        skip = -1;
                        break;
                      case eReverseLandscape:
                        dest += source_x * height + width - 1 - source_y;
                        skip = height;
                        break;
                      default:
                        return -1;
                      }
                    for ( i = 0; i < used; ++i, ++src, dest += skip )
                      *dest = *src;
                  }
                par->source.position += used;
                par->source.available -= used;
                par->source.data += used;

              }
            pxgs->halftone.width = width;
            pxgs->halftone.height = height;
            method = eDownloaded;
          }
        else
          return_error(errorMissingAttribute);
        if ( par->pv[0] )
          gs_transform(pgs, real_value(par->pv[0], 0),
                       real_value(par->pv[0], 1), &pxgs->halftone.origin);
        else
          gs_transform(pgs, 0.0, 0.0, &pxgs->halftone.origin);
        pxgs->halftone.thresholds = pxs->download_string;
        pxgs->halftone.method = method;
        pxgs->halftone.set = false;
        return 0;
}

const byte apxSetPenSource[] = {
  0, pxaRGBColor, pxaGrayLevel, pxaPrimaryArray, pxaPrimaryDepth, pxaNullPen, pxaPatternSelectID,
  pxaPatternOrigin, pxaNewDestinationSize, 0
};

int
pxSetPenSource(px_args_t *par, px_state_t *pxs)
{       return set_source(par, pxs, &pxs->pxgs->pen);
}

const byte apxSetColorTreatment[] =
    {0, pxaColorTreatment, pxaAllObjectTypes, pxaTextObjects, pxaVectorObjects, pxaRasterObjects, 0};

int
pxSetColorTreatment(px_args_t *par, px_state_t *pxs)
{
    return 0;
}

const byte apxSetNeutralAxis[] = {0, pxaAllObjectTypes, pxaTextObjects,
                                  pxaVectorObjects, pxaRasterObjects, 0};

int
pxSetNeutralAxis(px_args_t *par, px_state_t *pxs)
{
    return 0;
}

const byte apxSetColorTrapping[] = {pxaAllObjectTypes, 0, 0};

int
pxSetColorTrapping(px_args_t *par, px_state_t *pxs)
{
    return 0;
}

const byte apxSetAdaptiveHalftoning[] =
    {0, pxaAllObjectTypes, pxaTextObjects, pxaVectorObjects, pxaRasterObjects, 0};

int
pxSetAdaptiveHalftoning(px_args_t *par, px_state_t *pxs)
{
    return 0;
}
