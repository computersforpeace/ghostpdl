/* Copyright (C) 1996, 1997, 1998 Aladdin Enterprises.  All rights
   reserved.  Unauthorized use, copying, and/or distribution
   prohibited.  */

/* pgdraw.c - HP-GL/2 line drawing/path building routines. */

#include "stdio_.h"		
#include "math_.h"
#include "gdebug.h"
#include "gstypes.h"		/* for gsstate.h */
#include "gsmatrix.h"		/* for gsstate.h */
#include "gsmemory.h"		/* for gsstate.h */
#include "gsstate.h"
#include "gscoord.h"
#include "gspath.h"
#include "gspaint.h"
#include "gsrop.h"		/* for gs_setrasterop */
#include "gxfarith.h"		/* for sincos */
#include "gxfixed.h"
#include "pgmand.h"
#include "pgdraw.h"
#include "pggeom.h"
#include "pgmisc.h"
#include "pcdraw.h"
#include "pcpalet.h"
#include "pcpatrn.h"


/* hack to quiet compiler warnings */
#ifndef abs
extern  int     abs( int );
#endif

#define round(x)    (((x) < 0.0) ? (ceil ((x) - 0.5)) : (floor ((x) + 0.5)))

 int
hpgl_set_picture_frame_scaling(hpgl_state_t *pgls)
{
    if ( (pgls->g.picture_frame_height == 0) ||
	 (pgls->g.picture_frame_width == 0) ||
	 (pgls->g.plot_width == 0) ||
	 (pgls->g.plot_height == 0) ) {
	dprintf("bad picture frame coordinates\n");
	return 0;
    } else {
	hpgl_real_t vert_scale = (pgls->g.plot_size_vertical_specified) ?
 	    ((hpgl_real_t)pgls->g.picture_frame_height /
 	     (hpgl_real_t)pgls->g.plot_height) :
 	    1.0;
	hpgl_real_t horz_scale = (pgls->g.plot_size_horizontal_specified) ?
 	    ((hpgl_real_t)pgls->g.picture_frame_width /
 	     (hpgl_real_t)pgls->g.plot_width) :
 	    1.0;
	hpgl_call(gs_scale(pgls->pgs, horz_scale, vert_scale));
    }
    return 0;

}

/* ctm to translate from pcl space to plu space */
 int
hpgl_set_pcl_to_plu_ctm(hpgl_state_t *pgls)
{
	hpgl_call(pcl_set_ctm(pgls, false));
	if ( pgls->personality == rtl ) {
	    /* for plot length >= width, y increases across the short
               edge and x increases down the plot.  Rotate the pcl
               coordinate system -90, scale and flip the x axis.  If
               the plot width > length the origin is in the upper
               right and x increases going to the left and y increases
               going down.  Translate the pcl coordinate system by the
               picture frame width, scale and flip x. */
	    if ( pgls->g.picture_frame_height >= pgls->g.picture_frame_width )
		hpgl_call(gs_rotate(pgls->pgs, -90));
	    else
		hpgl_call(gs_translate(pgls->pgs, pgls->g.picture_frame_width, 0));
	    hpgl_call(gs_scale(pgls->pgs, -(7200.0/1016.0), (7200.0/1016.0)));
	} else {
	    hpgl_call(gs_translate(pgls->pgs,
			       pgls->g.picture_frame.anchor_point.x,
			       pgls->g.picture_frame.anchor_point.y));
	    /* move the origin */
	    hpgl_call(gs_translate(pgls->pgs, 0, pgls->g.picture_frame_height));
	    /* scale to plotter units and a flip for y */
	    hpgl_call(gs_scale(pgls->pgs, (7200.0/1016.0), -(7200.0/1016.0)));
	    /* account for rotated coordinate system */
	}
	hpgl_call(gs_rotate(pgls->pgs, pgls->g.rotation));
	{
	  hpgl_real_t fw_plu = (coord_2_plu(pgls->g.picture_frame_width));
	  hpgl_real_t fh_plu = (coord_2_plu(pgls->g.picture_frame_height));
	  switch (pgls->g.rotation)
	    {
	    case 0 :
	      hpgl_call(gs_translate(pgls->pgs, 0, 0));
	      break;
	    case 90 :
	      hpgl_call(gs_translate(pgls->pgs, 0, -fw_plu));
	      break;
	    case 180 :
	      hpgl_call(gs_translate(pgls->pgs, -fw_plu, -fh_plu));
	      break;
	    case 270 :
	      hpgl_call(gs_translate(pgls->pgs, -fh_plu, 0));
	      break;
	    }
	}
	hpgl_call(hpgl_set_picture_frame_scaling(pgls));
	{
	    gs_matrix mat;
	    gs_currentmatrix(pgls->pgs, &mat);
	    mat.ty = floor(mat.ty); mat.tx = floor(mat.tx);
	    gs_setmatrix(pgls->pgs, &mat);
	}
	hpgl_call(gs_setdotorientation(pgls->pgs));
	return 0;
}

/* Set the CTM to map PLU to device units, regardless of scaling. */
/* (We need this for labels when scaling is on.) */
 int
hpgl_set_plu_ctm(hpgl_state_t *pgls)
{
	hpgl_call(hpgl_set_pcl_to_plu_ctm(pgls));
	return 0;
}

/* The CTM maps PLU to device units; adjust it to handle scaling, if any. */
 int
hpgl_compute_user_units_to_plu_ctm(const hpgl_state_t *pgls, gs_matrix *pmat)
{	floatp origin_x = pgls->g.P1.x, origin_y = pgls->g.P1.y;

	switch ( pgls->g.scaling_type )
	  {
	  case hpgl_scaling_none:
	    gs_make_identity(pmat);
	    break;
	  case hpgl_scaling_point_factor:
	    hpgl_call(gs_make_translation(origin_x, origin_y, pmat));
	    hpgl_call(gs_matrix_scale(pmat, pgls->g.scaling_params.factor.x,
				      pgls->g.scaling_params.factor.y, pmat));
	    hpgl_call(gs_matrix_translate(pmat, -pgls->g.scaling_params.pmin.x,
					  -pgls->g.scaling_params.pmin.y, pmat));
	    break;
	  default:
	  /*case hpgl_scaling_anisotropic:*/
	  /*case hpgl_scaling_isotropic:*/
	    {
	      floatp window_x = pgls->g.P2.x - origin_x,
		range_x = pgls->g.scaling_params.pmax.x -
		  pgls->g.scaling_params.pmin.x,
		scale_x = window_x / range_x;
	      floatp window_y = pgls->g.P2.y - origin_y,
		range_y = pgls->g.scaling_params.pmax.y -
		  pgls->g.scaling_params.pmin.y,
		scale_y = window_y / range_y;

	      
	      if ( pgls->g.scaling_type == hpgl_scaling_isotropic ) {
		  /* it is unclear what should happen if scale x and
                     scale y have different signs for now we do
                     nothing */
		  if ( scale_x * scale_y < 0 )
		      ;
		  else if ( scale_x > scale_y ) { /* Reduce the X scaling. */
		      origin_x += range_x * (scale_x - scale_y) *
			(pgls->g.scaling_params.left / 100.0);
		      scale_x = scale_y;
		  } else if ( scale_y > scale_x ) { /* Reduce the Y scaling. */
		      origin_y += range_y * (scale_y - scale_x) *
			(pgls->g.scaling_params.bottom / 100.0);
		      scale_y = scale_x;
		  }
	      }
	      /* looks like HP keeps 5 decimal digits of accuracy
                 here.  This was derived empirically */
	      scale_x -= fmod(scale_x, 1.0E-5);
	      scale_y -= fmod(scale_y, 1.0E-5);
	      hpgl_call(gs_make_translation(origin_x, origin_y, pmat));
	      hpgl_call(gs_matrix_scale(pmat, scale_x, scale_y, pmat));
	      hpgl_call(gs_matrix_translate(pmat, -pgls->g.scaling_params.pmin.x,
					    -pgls->g.scaling_params.pmin.y, pmat));
	      break;
	    }
	  }
	return 0;

}

/* set up ctm's.  Uses the current render mode to figure out which ctm
   is appropriate */
 int
hpgl_set_ctm(hpgl_state_t *pgls)
{
    hpgl_call(hpgl_set_plu_ctm(pgls));
    if ( pgls->g.scaling_type != hpgl_scaling_none ) {
	gs_matrix mat;
	hpgl_call(hpgl_compute_user_units_to_plu_ctm(pgls, &mat));
	hpgl_call(gs_concat(pgls->pgs, &mat));
	hpgl_call(gs_currentmatrix(pgls->pgs, &mat));
	mat.tx = round(mat.tx);
	mat.ty = round(mat.ty);
	hpgl_call(gs_setmatrix(pgls->pgs, &mat));
    }
    return 0;
}

/* Compute the pattern length.  Normally, if the pattern length is
   relative it is 4% of the diagonal distance from P1 to P2, for
   isotropic scaling we need 4% of the distance of the plotter unit
   equivalent of the scaling SC coordinates xmin, xmax, ymin, and
   ymax.. */
 private floatp
hpgl_get_line_pattern_length(hpgl_state_t *pgls)
{
    /* dispense with the unusual "isotropic relative" case first.  The
       normal calculation afterward is straightforward */
    if ( (pgls->g.line.current.pattern_length_relative == 0) &&
	 (pgls->g.scaling_type == hpgl_scaling_isotropic) ) {
	    /* the box in user space */
	    gs_rect isotropic_user_box;
	    /* box in plotter units we compute 4% of the diagonal of this box */
	    gs_rect isotropic_plu_box;
	    gs_matrix user_to_plu_mat;

	    hpgl_call(hpgl_compute_user_units_to_plu_ctm(pgls, &user_to_plu_mat));

	    isotropic_user_box.p = pgls->g.scaling_params.pmin;
	    isotropic_user_box.q = pgls->g.scaling_params.pmax;

	    hpgl_call(gs_bbox_transform(&isotropic_user_box, 
					&user_to_plu_mat,
					&isotropic_plu_box));

	    return (pgls->g.line.current.pattern_length / 100.0) *
		hpgl_compute_distance(isotropic_plu_box.p.x,
				      isotropic_plu_box.p.y,
				      isotropic_plu_box.q.x,
				      isotropic_plu_box.q.y);
    }

    /* simple case 4% of the diagonal of P1 and P2 or absolute in millimeters */
    return ((pgls->g.line.current.pattern_length_relative == 0) ?
	    ((pgls->g.line.current.pattern_length / 100.0) *
	     hpgl_compute_distance(pgls->g.P1.x,
				   pgls->g.P1.y,
				   pgls->g.P2.x,
				   pgls->g.P2.y)) :
	    (mm_2_plu(pgls->g.line.current.pattern_length)));
}

 private int
hpgl_set_graphics_dash_state(hpgl_state_t *pgls)
{
	int entry = abs(pgls->g.line.current.type);
	bool adaptive;
	const hpgl_line_type_t *pat;
	float length;
	float pattern[20];
	float offset;
	int count;
	int i;

	/* Make sure we always draw dots. */
	hpgl_call(gs_setdotlength(pgls->pgs, 0.00098, true));

	/* handle the simple case (no dash) and return */
	if ( pgls->g.line.current.is_solid )
	  {
	    /* use a 0 count pattern to turn off dashing in case it is
               set */
	    hpgl_call(gs_setdash(pgls->pgs, pattern, 0, 0));
	    return 0;
	  }
	
	if ( entry == 0 )
	  {
	    /* dot length NOTE this is in absolute 1/72" units not
               user space */
	    /* Use an adaptive pattern with an infinitely long segment length
	       to get the dots drawn just at the ends of lines. */
	    pattern[0] = 0;
	    pattern[1] = 1.0e6;  /* "infinity" */
	    hpgl_call(gs_setdash(pgls->pgs, pattern, 2, 0));
	    gs_setdashadapt(pgls->pgs, true);
	    return 0;
	  }

	adaptive =  ( pgls->g.line.current.type < 0 );
	pat = ((adaptive) ?
	       (&pgls->g.adaptive_line_type[entry - 1]) :
	       (&pgls->g.fixed_line_type[entry - 1]));
	
	length = hpgl_get_line_pattern_length(pgls);
	gs_setdashadapt(pgls->pgs, adaptive);
	/*
	 * The graphics library interprets odd pattern counts differently
	 * from GL: if the pattern count is odd, we need to do something
	 * a little special.
	 */
	count = pat->count;
	for ( i = 0; i < count; i++ )
	  pattern[i] = length * pat->gap[i];
	offset = pgls->g.line.current.pattern_offset * hpgl_get_line_pattern_length(pgls);
	if ( count & 1 )
	  {
	    /*
	     * Combine the last gap with the first one, and change the
	     * offset to compensate.  NOTE: this doesn't work right with
	     * adaptive line type: we may need to change the library to
	     * make this work properly.
	     */
	    --count;
	    pattern[0] += pattern[count];
	    offset += pattern[count];
	  }
	
	hpgl_call(gs_setdash(pgls->pgs, pattern, count, offset));
	
	return 0;
}

/* catch pen not being in the palette */
 int
hpgl_get_selected_pen(hpgl_state_t *pgls)
{
    /* get the current pen */
    int pen = pgls->g.pen.selected;
    /* 0 is the first pen */
    int max_pen = pcl_palette_get_num_entries(pgls->ppalet) - 1;
    /* this is bad */
    if ( pen < 0 )
	pen = -pen;
    /* try to recover if necessary */
    while ( pen > max_pen )
	pen = pen - max_pen;
    return pen;
}

/*
 * set up joins, caps, miter limit, and line width
 */
 private int
hpgl_set_graphics_line_attribute_state(
    hpgl_state_t *          pgls,
    hpgl_rendering_mode_t   render_mode
)
{
    const gs_line_cap       cap_map[] = { gs_cap_butt,      /* 0 not supported */
					  gs_cap_butt,      /* 1 butt end */
					  gs_cap_square,    /* 2 square end */
					  gs_cap_triangle,  /* 3 triag. end */
					  gs_cap_round      /* 4 round cap */
                                          };

    const gs_line_join      join_map[] = { gs_join_none,    /* 0 not supported */
					   gs_join_miter,   /* 1 mitered */
					   gs_join_miter,   /* 2 mitrd/bevld */
					   gs_join_triangle, /* 3 triag. join */
					   gs_join_round,   /* 4 round join */
					   gs_join_bevel,   /* 5 bevel join */
					   gs_join_none     /* 6 no join */
                                           };
    const float *           widths = pcl_palette_get_pen_widths(pgls->ppalet);
    floatp                  pen_wid = widths[hpgl_get_selected_pen(pgls)];

    gs_setfilladjust(pgls->pgs, 0.5, 0.5);
	    
    /* character mode has already set up all of this information in
       the build character routine for the font */
    if ( render_mode == hpgl_rm_character )
	return 0;
    /*
     * HP appears to use default line attributes if the the pen
     * width is less than or equal to .35mm or 14.0 plu.  This
     * is not documented PCLTRM.  Pen widths are maintained in
     * plotter units
     */
    if ( render_mode != hpgl_rm_character && pen_wid <= 14.0 ) {
	hpgl_call(gs_setlinejoin(pgls->pgs, gs_join_miter));
	hpgl_call(gs_setlinecap(pgls->pgs, gs_cap_butt));
	hpgl_call(gs_setlinewidth(pgls->pgs, pen_wid));
	hpgl_call(gs_setmiterlimit(pgls->pgs, 5.0));
	return 0;
    }

    switch (render_mode) {

      case hpgl_rm_character:
      case hpgl_rm_polygon:
      case hpgl_rm_clip_and_fill_polygon:
	hpgl_call(gs_setlinejoin(pgls->pgs, gs_join_round));
	hpgl_call(gs_setlinecap(pgls->pgs, gs_cap_round));
	break;

      case hpgl_rm_vector_fill:
      case hpgl_rm_vector:
vector:
	hpgl_call(gs_setlinejoin(pgls->pgs, join_map[pgls->g.line.join]));
	hpgl_call(gs_setlinecap(pgls->pgs, cap_map[pgls->g.line.cap]));
	hpgl_call(gs_setlinewidth(pgls->pgs, pen_wid));
	break;

      default:
	/* shouldn't happen; we must have a mode to properly parse hpgl file. */
	dprintf("warning no hpgl rendering mode set using vector mode\n");
	goto vector;
    }

#ifdef COMMENT
    /* I do not remember the rational for the large miter */
    hpgl_call(gs_setmiterlimit( pgls->pgs,
				(pgls->g.line.join == 1)
                                   ? 5000.0
                                   : pgls->g.miter_limit
                                ) );
#endif
    hpgl_call(gs_setmiterlimit(pgls->pgs, pgls->g.miter_limit));
    return 0;
}

/*
 * A bounding box for the current polygon -- used for HPGL/2 vector
 * fills.
 */
 private int
hpgl_polyfill_bbox(
    hpgl_state_t *  pgls,
    gs_rect *       bbox
)
{
    /* get the bounding box for the current path / polygon */
    hpgl_call(gs_pathbbox(pgls->pgs, bbox));
    return 0;
}

/* set up an hpgl clipping region -- intersection of IW command and
   picture frame. */
 int
hpgl_set_clipping_region(hpgl_state_t *pgls, hpgl_rendering_mode_t render_mode)
{
	/* if we are doing vector fill a clipping path has already
           been set up using the last polygon */
	if ( render_mode == hpgl_rm_vector_fill )
	  return 0;
	else
	  {
	    gs_fixed_rect fixed_box;
	    gs_rect pcl_clip_box;
	    gs_rect dev_clip_box;
	    gs_matrix save_ctm;
	    gs_matrix pcl_ctm;

	    /* get pcl to device ctm and restore the current ctm */
	    hpgl_call(gs_currentmatrix(pgls->pgs, &save_ctm));
	    hpgl_call(pcl_set_ctm(pgls, false));
	    hpgl_call(gs_currentmatrix(pgls->pgs, &pcl_ctm));
	    hpgl_call(gs_setmatrix(pgls->pgs, &save_ctm));
	    /* find the clipping region defined by the picture frame
               which is defined in pcl coordinates */
	    pcl_clip_box.p.x = pgls->g.picture_frame.anchor_point.x;
	    pcl_clip_box.p.y = pgls->g.picture_frame.anchor_point.y;
	    pcl_clip_box.q.x = pcl_clip_box.p.x + pgls->g.picture_frame_width;
	    pcl_clip_box.q.y = pcl_clip_box.p.y + pgls->g.picture_frame_height;
	    
	    hpgl_call(gs_bbox_transform(&pcl_clip_box,
					&pcl_ctm,
					&dev_clip_box));
	    /* the clip box defined by the picture frame appears to be
               open and the clip box defined by IW is closed */
	    dev_clip_box.q.x += 1.0;
	    dev_clip_box.q.y += 1.0;
	    if ( gs_debug_c('P') )
		dprintf4("gl/2 device picture frame: p.x=%f p.y=%f q.x=%f q.y=%f\n",
			 dev_clip_box.p.x, dev_clip_box.p.y,
			 dev_clip_box.q.x, dev_clip_box.q.y);
	    /* if the clipping window is active calculate the new clip
               box derived from IW and the intersection of the device
               space boxes replace the current box.  Note that IW
               coordinates are in current units and and the picture
               frame in pcl coordinates. */
	    if ( pgls->g.soft_clip_window.state == active ) {
		gs_rect dev_soft_window_box;
		gs_matrix ctm;
		hpgl_call(gs_currentmatrix(pgls->pgs, &ctm));
		hpgl_call(gs_bbox_transform(&pgls->g.soft_clip_window.rect,
					    &ctm,
					    &dev_soft_window_box));
		/* Enlarge IW by 1 device dot to compensate for it's 
		   'on the line' is not clipped behavior. */
	        dev_clip_box.p.x = max(dev_clip_box.p.x, dev_soft_window_box.p.x - 1.0);
		dev_clip_box.p.y = max(dev_clip_box.p.y, dev_soft_window_box.p.y - 1.0);
	    	dev_clip_box.q.x = min(dev_clip_box.q.x, dev_soft_window_box.q.x + 1.0);
		dev_clip_box.q.y = min(dev_clip_box.q.y, dev_soft_window_box.q.y + 1.0);
		if ( gs_debug_c('P') ) {
		    dprintf4("gl/2 window user units: p.x=%f p.y=%f q.x=%f q.y=%f\n",
			     pgls->g.soft_clip_window.rect.p.x, 
			     pgls->g.soft_clip_window.rect.p.y,
			     pgls->g.soft_clip_window.rect.q.x, 
			     pgls->g.soft_clip_window.rect.q.y);
		    dprintf4("gl/2 device soft clip region: p.x=%f p.y=%f q.x=%f q.y=%f\n",
			     dev_soft_window_box.p.x - 1.0, dev_soft_window_box.p.y - 1.0,
			     dev_soft_window_box.q.x + 1.0, dev_soft_window_box.q.y + 1.0);
		}
	    }
	    /* convert intersection box to fixed point and clip */
	    fixed_box.p.x = float2fixed(floor(dev_clip_box.p.x));
	    fixed_box.p.y = float2fixed(floor(dev_clip_box.p.y));
	    fixed_box.q.x = float2fixed(ceil(dev_clip_box.q.x));
	    fixed_box.q.y = float2fixed(ceil(dev_clip_box.q.y));
	    /* intersect with pcl clipping region */
	    fixed_box.p.x = max(fixed_box.p.x, pgls->xfm_state.dev_print_rect.p.x);
	    fixed_box.p.y = max(fixed_box.p.y, pgls->xfm_state.dev_print_rect.p.y);
	    fixed_box.q.x = min(fixed_box.q.x, pgls->xfm_state.dev_print_rect.q.x);
	    fixed_box.q.y = min(fixed_box.q.y, pgls->xfm_state.dev_print_rect.q.y);
	    if ( gs_debug_c('P') ) {
		gs_matrix mat;
		dprintf4("gl/2 device clip region: p.x=%f p.y=%f q.x=%f q.y=%f\n",
			 fixed2float(fixed_box.p.x), fixed2float(fixed_box.p.y),
			 fixed2float(fixed_box.q.x), fixed2float(fixed_box.q.y));
		    gs_make_identity(&mat);
		    hpgl_gsave(pgls);
		    gs_setmatrix(pgls->pgs, &mat);
		    gs_setgray(pgls->pgs, 0);
		    gs_newpath(pgls->pgs);
		    gs_setlinewidth(pgls->pgs, 0);
		    gs_moveto(pgls->pgs, fixed2float(fixed_box.p.x), 
			      fixed2float(fixed_box.p.y));
		    gs_lineto(pgls->pgs, fixed2float(fixed_box.p.x),
			      fixed2float(fixed_box.q.y));
		    gs_lineto(pgls->pgs, fixed2float(fixed_box.q.x),
			      fixed2float(fixed_box.q.y));
		    gs_lineto(pgls->pgs, fixed2float(fixed_box.q.x),
			      fixed2float(fixed_box.p.y));
		    gs_closepath(pgls->pgs);
		    gs_stroke(pgls->pgs);
		    hpgl_grestore(pgls);
		} else
		    hpgl_call(gx_clip_to_rectangle(pgls->pgs, &fixed_box)); 
	  }
	return 0;
}

/* Plot one vector for vector fill all these use absolute coordinates. */
  private int
hpgl_draw_vector_absolute(
    hpgl_state_t *          pgls,
    hpgl_real_t             x0,
    hpgl_real_t             y0,
    hpgl_real_t             x1,
    hpgl_real_t             y1,
    hpgl_rendering_mode_t   render_mode
)
{
    bool                    set_ctm = (render_mode != hpgl_rm_character);

    hpgl_call( hpgl_add_point_to_path( pgls,
                                       x0,
                                       y0,
				       hpgl_plot_move_absolute,
                                       set_ctm
                                       ) );
    hpgl_call( hpgl_add_point_to_path( pgls,
                                       x1,
                                       y1,
				       hpgl_plot_draw_absolute,
                                       set_ctm
                                       ) );
    hpgl_call(hpgl_draw_current_path(pgls, hpgl_rm_vector_fill));
    return 0;
}

 private int
hpgl_get_adjusted_corner(
    hpgl_real_t x_fill_increment,
    hpgl_real_t y_fill_increment,
    gs_rect *   bbox,
    gs_point *  current_anchor_corner,
    gs_point *  adjusted_anchor_corner
)
{
    adjusted_anchor_corner->x = current_anchor_corner->x;
    adjusted_anchor_corner->y = current_anchor_corner->y;
    /* account for anchor corner greater than origin */
    if (x_fill_increment != 0) {
	while (adjusted_anchor_corner->x > bbox->p.x)
	    adjusted_anchor_corner->x -= x_fill_increment;
    } else if (adjusted_anchor_corner->x > bbox->p.x)
	adjusted_anchor_corner->x = bbox->p.x;
    if (y_fill_increment != 0) {
	while (adjusted_anchor_corner->y > bbox->p.y)
	    adjusted_anchor_corner->y -= y_fill_increment;
    } else if (adjusted_anchor_corner->y > bbox->p.y)
	adjusted_anchor_corner->y = bbox->p.y;
    return 0;
}

 private void
hpgl_alternate_line_pattern_offset(hpgl_state_t *pgls, uint lines_filled)
{
    if ( lines_filled & 1 )
	pgls->g.line.current.pattern_offset = 0.5;
    else
	pgls->g.line.current.pattern_offset = 0.0;
}

/*
 * HAS should replicate lines beginning at the anchor corner to +X and
 * +Y.  Not quite right - anchor corner not yet supported.
 * pgls->g.anchor_corner needs to be used to set dash offsets
 */
 private int
hpgl_polyfill(
    hpgl_state_t *              pgls,
     hpgl_rendering_mode_t      render_mode
)
{
    hpgl_real_t                 diag_mag, endx, endy;
    gs_sincos_t                 sincos;
    gs_point                    start;

#define sin_dir sincos.sin
#define cos_dir sincos.cos

    gs_rect                     bbox;
    gs_matrix   user_to_plu_mat;
    hpgl_pen_state_t            saved_pen_state;
    hpgl_real_t                 x_fill_increment, y_fill_increment;
    hpgl_FT_pattern_source_t    type = pgls->g.fill.type;
    bool                        cross = (type == hpgl_FT_pattern_two_lines);
    const hpgl_hatch_params_t * params = (cross ? &pgls->g.fill.param.crosshatch
                                                : &pgls->g.fill.param.hatch);
    gs_point                    spacing;
    hpgl_real_t                 direction = params->angle;
    hpgl_real_t                 unscaled_direction;
    float saved_line_pattern_offset = pgls->g.line.current.pattern_offset;
    int lines_filled;

    /* spacing is always relevant to the scaling of the x-axis.  It
       can be specified in user space if provided to FT or by default
       it is 1% of the distance from P1 to P2 */
    spacing.x = params->spacing;
    /* save the pen position */
    hpgl_save_pen_state(pgls, &saved_pen_state, hpgl_pen_pos);
    hpgl_call(hpgl_compute_user_units_to_plu_ctm(pgls, &user_to_plu_mat));
    if (params->spacing == 0) {
        /* Per TRM 22-12, use 1% of the P1/P2 distance. */
	
	spacing.x = 0.01 * hpgl_compute_distance( pgls->g.P1.x,
						  pgls->g.P1.y,
						  pgls->g.P2.x,
						  pgls->g.P2.y
						  );

	/* 1% is in plu, convert back to user units */
	spacing.y = spacing.x / fabs(user_to_plu_mat.yy);
	spacing.x /= fabs(user_to_plu_mat.xx);
    }
    else {
	/* set spacing.y based on ratio of x/y scaling, still in user units */
	spacing.y = spacing.x * fabs(user_to_plu_mat.xx) / fabs(user_to_plu_mat.yy);
    }
    /* get the bounding box */
    hpgl_call(hpgl_polyfill_bbox(pgls, &bbox));
    /*
     * if the line width exceeds the spacing we use the line width
     * to avoid overlapping of the fill lines.  HAS this can be
     * integrated with the logic above for spacing as not to
     * duplicate alot of code.
     */
    {
        const float *   widths = pcl_palette_get_pen_widths(pgls->ppalet);
        hpgl_real_t     line_width = widths[hpgl_get_selected_pen(pgls)];

        line_width /= min(fabs(user_to_plu_mat.xx), fabs(user_to_plu_mat.yy));
	if (line_width >= spacing.x  || line_width >= spacing.y) {
            hpgl_call(hpgl_draw_current_path(pgls, hpgl_rm_polygon));
	    return 0;
	}
    }

    /* get rid of the current path */

    hpgl_call(hpgl_clear_current_path(pgls));


 start:  /* Change direction, come back and draw crosshatch lines. */

    /* hatch angles are isotropic; ie 45degrees is not affected by y scale != x scale.
       unscaled_direction ::= user requested angle  
       direction ::= the angle that when scaled will generate user requested angle */

    /* save unscaled angle */
    unscaled_direction = direction;

    /* not necessery if direction is orthogonal */
    if ( !equal(fmod( direction, 90 ), 0 ) ) {
	hpgl_real_t slope, scaled_slope;
	gs_sincos_degrees(direction, &sincos);
	/* take the tangent by dividing sin by cos.  Since we know
	   the angle is non-orthogonal the cosine is non-zero */
	slope = sin_dir / cos_dir;
	/* scale the slope by the ratio of the scaling factors */
	scaled_slope = (user_to_plu_mat.xx / user_to_plu_mat.yy) * slope;
	/* preserved angle in user space */
	direction = radians_to_degrees * atan(scaled_slope);
    }

    lines_filled = 0;

    /* spacing is done with the unscaled angle 
       spacing.x .y is already scaled hence the unscaled angle is used. 
       scale * spacing * sin(unscaled_angle) */
    gs_sincos_degrees(unscaled_direction, &sincos);
    if (sin_dir < 0)
	sin_dir = -sin_dir, cos_dir = -cos_dir; /* ensure y_inc >= 0 */
    x_fill_increment = (sin_dir != 0) ? fabs(spacing.x / sin_dir) : 0;
    y_fill_increment = (cos_dir != 0) ? fabs(spacing.y / cos_dir) : 0;
    
    /* scaled angle is used for end point calculation 
       distance * sin( scaled_angle ) 
       scale is applyed once and only once per calculation */
    gs_sincos_degrees(direction, &sincos);
    if (sin_dir < 0)
	sin_dir = -sin_dir, cos_dir = -cos_dir; /* ensure y_inc >= 0 */

    hpgl_call( hpgl_get_adjusted_corner( x_fill_increment,
                                         y_fill_increment,
                                         &bbox,
                                         &pgls->g.anchor_corner,
                                         &start
                                         ) );

    /*
     * calculate the diagonals magnitude.  Note we clip this
     * latter in the library.  If desired we could clip to the
     * actual bbox here to save processing time.  For now we simply
     * draw all fill lines using the diagonals magnitude
     */
    diag_mag = hpgl_compute_distance(start.x, start.y, bbox.q.x, bbox.q.y);
    endx = (diag_mag * cos_dir) + start.x;
    endy = (diag_mag * sin_dir) + start.y;
    hpgl_alternate_line_pattern_offset(pgls, lines_filled++);
    hpgl_call( hpgl_draw_vector_absolute( pgls,
                                          start.x,
                                          start.y,
					  endx,
                                          endy,
                                          render_mode
                                          ) );
    /* Travel along +x using current spacing. */
    if (x_fill_increment != 0) {
	while ( endx += x_fill_increment,
		(start.x += x_fill_increment) <= bbox.q.x ) {

	    hpgl_alternate_line_pattern_offset(pgls, lines_filled++);
	    hpgl_call( hpgl_draw_vector_absolute( pgls,
                                                  start.x,
                                                  start.y,
						  endx,
                                                  endy,
                                                  render_mode
                                                  ) );
	}
    }

    /* Travel along +Y similarly. */
    if (y_fill_increment != 0) {
	/*
	 * If the slope is negative, we have to travel along the right
	 * edge of the box rather than the left edge.  Fortuitously,
	 * the X loop left everything set up exactly right for this case.
	 */
	if (cos_dir >= 0) {
	    hpgl_call( hpgl_get_adjusted_corner( x_fill_increment,
		                                 y_fill_increment,
                                                 &bbox,
                                                 &pgls->g.anchor_corner,
                                                 &start
                                                 ) );
	    endx = (diag_mag * cos_dir) + start.x;
	    endy = (diag_mag * sin_dir) + start.y;
	} else
	    start.y -= y_fill_increment, endy -= y_fill_increment;

	while ( endy += y_fill_increment,
		(start.y += y_fill_increment) <= bbox.q.y ) {
	    hpgl_alternate_line_pattern_offset(pgls, lines_filled++);
	    hpgl_call( hpgl_draw_vector_absolute( pgls,
                                                  start.x,
                                                  start.y,
						  endx,
                                                  endy,
                                                  render_mode
                                                  ) );
	}
	
    }
    if (cross) { 
	/* go back and draw the perpendicular lines, +90degress */
	cross = false;
	direction = unscaled_direction + 90;
	if ( direction >= 180 )
	    direction -= 180;
	goto start;
    }
    hpgl_restore_pen_state(pgls, &saved_pen_state, hpgl_pen_pos);
    pgls->g.line.current.pattern_offset = saved_line_pattern_offset;
    return 0;

#undef sin_dir
#undef cos_dir

}

/* gl/2 vector filled objects always have a white background.  It can
   be either a transparent or white.  In the former case we don't have
   to do anything.  We expect the fill area of the object to already
   be defined in the graphics state. */
 private int
hpgl_fill_polyfill_background(hpgl_state_t *pgls)
{
    /* if we are drawing on a transparent background */
    if ( pgls->g.source_transparent )
	return 0;
    /* preserve the current foreground color */
    hpgl_call(hpgl_gsave(pgls));
    /* fill a white region.  NB have not experimented with different
       rasterops and transparency. */
    hpgl_call(gs_setgray(pgls->pgs, 1.0));
    hpgl_call(gs_fill(pgls->pgs));
    /* restore the foreground color */
    hpgl_call(hpgl_grestore(pgls));
    return 0;
}
    
 private int
hpgl_polyfill_using_current_line_type(
    hpgl_state_t *        pgls,
    hpgl_rendering_mode_t render_mode
)
{
    /* gsave and grestore used to preserve the clipping region */
    hpgl_call(hpgl_gsave(pgls));

    /*
     * Use the current path to set up a clipping path
     * beginning at the anchor corner replicate lines
     */
    if (pgls->g.fill_type == hpgl_even_odd_rule)
	hpgl_call(gs_eoclip(pgls->pgs));
    else
	hpgl_call(gs_clip(pgls->pgs));
    hpgl_call(hpgl_fill_polyfill_background(pgls));
    hpgl_call(hpgl_polyfill(pgls, render_mode));
    hpgl_call(hpgl_grestore(pgls));
    return 0;
}

 int
hpgl_set_drawing_color(
    hpgl_state_t *          pgls,
    hpgl_rendering_mode_t   render_mode
)
{
    int                     code = 0;
    pcl_pattern_set_proc_t  set_proc;
    byte pixel_placement_mode = 0;
    switch (render_mode) {

      case hpgl_rm_clip_and_fill_polygon:
	hpgl_call(hpgl_polyfill_using_current_line_type(pgls, render_mode));
	return 0;

      case hpgl_rm_character:
	switch (pgls->g.character.fill_mode) {

	  case hpgl_char_solid_edge:    /* fall through */
	  case hpgl_char_edge:
            set_proc = pcl_pattern_get_proc_FT(hpgl_FT_pattern_solid_pen1);
            code = set_proc(pgls, hpgl_get_selected_pen(pgls), false);
	    break;

	  case hpgl_char_fill:
	  case hpgl_char_fill_edge:
	    if ( (pgls->g.fill.type == hpgl_FT_pattern_one_line) ||
		 (pgls->g.fill.type == hpgl_FT_pattern_two_lines)  ) {
		hpgl_call( hpgl_polyfill_using_current_line_type( pgls,
                                                                  render_mode
                                                                  ) );
		return 0;
	    } else
		goto fill;

	  default:
	    dprintf("hpgl_set_drawing_color: internal error illegal fill\n");
	    return 0;
	}
	break;

	/* fill like a polygon */
      case hpgl_rm_polygon:
fill:
        /* pixel placement mode is only relevant to polygon fills */ 
        pixel_placement_mode = pgls->pp_mode;
        set_proc = pcl_pattern_get_proc_FT(pgls->g.fill.type);
	switch (pgls->g.fill.type) {

	  case hpgl_FT_pattern_solid_pen1:
	  case hpgl_FT_pattern_solid_pen2:
	    /*
             * this is documented incorrectly PCLTRM 22-12 says
             * these should be solid black but they are actually
             * set to the value of the current pen - (i.e pen 0 is
             * solid white
             */
	    code = set_proc(pgls, hpgl_get_selected_pen(pgls), false);
	    break;

          case hpgl_FT_pattern_one_line:
	  case hpgl_FT_pattern_two_lines:
	    set_proc = pcl_pattern_get_proc_FT(hpgl_FT_pattern_solid_pen1);
            code = set_proc(pgls, hpgl_get_selected_pen(pgls), false);
	    break;

	  case hpgl_FT_pattern_cross_hatch:
            code = set_proc( pgls,
                             pgls->g.fill.param.pattern_type,
                             hpgl_get_selected_pen(pgls)
                             );
	    break;

	  case hpgl_FT_pattern_shading:
            code = set_proc( pgls,
                             pgls->g.fill.param.shading,
                             hpgl_get_selected_pen(pgls)
                             );
	    break;

	  case hpgl_FT_pattern_user_defined:
            code = set_proc( pgls,
                             pgls->g.fill.param.pattern_id,
                             hpgl_get_selected_pen(pgls)
                             );
            break;

	  case hpgl_FT_pattern_RF:
	      /* pcl5e does not care about the current pen it always uses black (1). */
	      if ( pgls->personality == pcl5e )
		  code = set_proc( pgls,
				   pgls->g.fill.param.user_defined.pattern_index,
				   1 );
	      else
		  code = set_proc( pgls,
				   pgls->g.fill.param.user_defined.pattern_index,
				   (pgls->g.fill.param.user_defined.use_current_pen
				    ? hpgl_get_selected_pen(pgls)
				    : -hpgl_get_selected_pen(pgls))
				   );

            break;
	  default:
	    dprintf("hpgl_set_drawing_color: internal error illegal fill\n");
	    break;
	}
	break;

      case hpgl_rm_vector:
      case hpgl_rm_vector_fill:
        set_proc = pcl_pattern_get_proc_SV(pgls->g.screen.type);
	switch(pgls->g.screen.type) {

	  case hpgl_SV_pattern_solid_pen:
	    code = set_proc(pgls, hpgl_get_selected_pen(pgls), false);
	    break;

	  case hpgl_SV_pattern_shade:
            code = set_proc( pgls,
                             pgls->g.screen.param.shading,
                             hpgl_get_selected_pen(pgls)
                             );
	    break;

          case hpgl_SV_pattern_cross_hatch:
            code = set_proc( pgls,
                             pgls->g.screen.param.pattern_type,
                             hpgl_get_selected_pen(pgls)
                             );
	    break;

          case hpgl_SV_pattern_RF:
            code = set_proc( pgls,
                             pgls->g.screen.param.user_defined.pattern_index,
                             (pgls->g.screen.param.user_defined.use_current_pen
                                  ? hpgl_get_selected_pen(pgls)
			          : -hpgl_get_selected_pen(pgls))
                             );
            break;

	  case hpgl_SV_pattern_user_defined:
            code = set_proc( pgls,
                             pgls->g.screen.param.pattern_id,
                             hpgl_get_selected_pen(pgls)
                             );
            break;

	  default:
	    dprintf("hpgl_set_drawing_color: internal error illegal fill\n");
	    break;
	}
	break;

      default:
	dprintf("hpgl_set_drawing_color: internal error illegal mode\n");
	break;
    }

    if (code >= 0) {
        /* PCL and GL/2 no longer use graphic library transparency */
        gs_setrasterop(pgls->pgs, (gs_rop3_t)pgls->logical_op);
    }
    return code;
}

 private int
hpgl_set_drawing_state(
    hpgl_state_t *           pgls,
    hpgl_rendering_mode_t    render_mode
)
{
    /* do dash stuff. */
    hpgl_call(hpgl_set_graphics_dash_state(pgls));

    /* joins, caps, and line width. */
    hpgl_call(hpgl_set_graphics_line_attribute_state(pgls, render_mode));

    /* set up a clipping region */
    hpgl_call(hpgl_set_clipping_region(pgls, render_mode));

    /* set up the hpgl fills (GL's source transp. is PCL's pattern trasp. */
    pgls->pattern_transparent = pgls->g.source_transparent;
    hpgl_call(hpgl_set_drawing_color(pgls, render_mode));

    return 0;
}

 int
hpgl_get_current_position(
    hpgl_state_t *  pgls,
    gs_point *      pt
)
{
    *pt = pgls->g.pos;
    return 0;
}

 int
hpgl_update_carriage_return_pos(hpgl_state_t *pgls)
{
    pgls->g.carriage_return_pos = pgls->g.pos;
    return 0;
}

 int
hpgl_set_current_position(
    hpgl_state_t *  pgls,
    gs_point *      pt
)
{
    pgls->g.pos = *pt;
    if ( gs_debug_c('P') )
	dprintf2("gl/2 setting position: x=%f y=%f\n", pt->x, pt->y);
    return 0;
}

 int
hpgl_add_point_to_path(
    hpgl_state_t *          pgls,
    floatp                  x,
    floatp                  y,
    hpgl_plot_function_t    func,
    bool                    set_ctm
)
{	
    static int              (*const gs_procs[])(P3(gs_state *, floatp, floatp))
                                = { hpgl_plot_function_procedures };

    /*  HP lunacy... if we are starting a polygon path the path
       machinery (hpgl_plot()) has already made sure the first point
       is a pen up - if we are drawing for some other reason the flag
       is moot - so we set it to false. */
    pgls->g.subpolygon_started = false;
    if (gx_path_is_null(gx_current_path(pgls->pgs))) {
	/* Start a new GL/2 path. */
	gs_point    current_pt;

	if (set_ctm)
	    hpgl_call(hpgl_set_ctm(pgls));
	hpgl_call(gs_newpath(pgls->pgs));

	/* moveto the current position */
	hpgl_call(hpgl_get_current_position(pgls, &current_pt));
	hpgl_call_check_lost( gs_moveto( pgls->pgs,
					 current_pt.x,
					 current_pt.y
					 ) );
    }
    {
        int     code = (*gs_procs[func])(pgls->pgs, x, y);
	if ( gs_debug_c('P') ) {
	    static const char *hpgl_draw_func[] = {
		"hpgl_plot_move_absolute",
		"hpgl_plot_move_relative",
		"hpgl_plot_draw_absolute",
		"hpgl_plot_draw_relative" 
	    };
	    gs_point dev_pt;
	    hpgl_call(gs_transform(pgls->pgs, x, y, &dev_pt));
	    dprintf1("gl/2 plot function=%s\t", hpgl_draw_func[func]);
	    dprintf2("gl/2 user coord: x=%f y=%f\t", x, y);
	    dprintf2("gl/2 device coord: x=%f y=%f\n", dev_pt.x, dev_pt.y);
    	}
	if (code < 0) {
            hpgl_call_note_error(code);
	    if (code == gs_error_limitcheck)
		hpgl_set_lost_mode(pgls, hpgl_lost_mode_entered);
	} else {
            gs_point    point;

	    if (hpgl_plot_is_absolute(func))
	        hpgl_set_lost_mode(pgls, hpgl_lost_mode_cleared);

	    /* update hpgl's state position */
	    if (hpgl_plot_is_absolute(func)) {
		point.x = x; 
		point.y = y;
	    } else {
		hpgl_call(hpgl_get_current_position(pgls, &point));
		point.x += x; point.y += y;
	    }
	    hpgl_call(hpgl_set_current_position(pgls, &point));
	}
    }
    return 0;
}

/* destroy the current path. */
 int
hpgl_clear_current_path(hpgl_state_t *pgls)
{
	hpgl_call(gs_newpath(pgls->pgs));
	return 0;
}

/* closes the current path, making the first point and last point coincident */
 int
hpgl_close_current_path(hpgl_state_t *pgls)
{
	hpgl_call(gs_closepath(pgls->pgs));
	return 0;
}

/* converts pcl coordinate to device space and back to hpgl space */
 int
hpgl_add_pcl_point_to_path(hpgl_state_t *pgls, const gs_point *pcl_pt)
{
	gs_point dev_pt, hpgl_pt;

	hpgl_call(hpgl_clear_current_path(pgls));
	pcl_set_ctm(pgls, true);
	hpgl_call(gs_transform(pgls->pgs, pcl_pt->x, pcl_pt->y, &dev_pt));
	hpgl_call(hpgl_set_ctm(pgls));
	hpgl_call(gs_itransform(pgls->pgs, dev_pt.x, dev_pt.y, &hpgl_pt));
	hpgl_call(hpgl_add_point_to_path(pgls, hpgl_pt.x, hpgl_pt.y,
					 hpgl_plot_move_absolute, true));
	return 0;
}

 int
hpgl_add_arc_to_path(hpgl_state_t *pgls, floatp center_x, floatp center_y,
		     floatp radius, floatp start_angle, floatp sweep_angle,
		     floatp chord_angle, bool start_moveto, hpgl_plot_function_t draw,
		     bool set_ctm)
{
	/*
	 * Ensure that the sweep angle is an integral multiple of the
	 * chord angle, by decreasing the chord angle if necessary.
	 */
	int num_chords = (int)ceil(sweep_angle / chord_angle);
	floatp integral_chord_angle = sweep_angle / num_chords;
	int i;
	floatp arccoord_x, arccoord_y;

	(void)hpgl_compute_arc_coords(radius, center_x, center_y,
				      start_angle,
				      &arccoord_x, &arccoord_y);
	hpgl_call(hpgl_add_point_to_path(pgls, arccoord_x, arccoord_y,
					 (draw && !start_moveto ?
					 hpgl_plot_draw_absolute :
					 hpgl_plot_move_absolute), set_ctm));

	/* HAS - pen up/down is invariant in the loop */
	for ( i = 0; i < num_chords; i++ )
	  {
	    start_angle += integral_chord_angle;
	    hpgl_compute_arc_coords(radius, center_x, center_y,
				    start_angle, &arccoord_x, &arccoord_y);
	    hpgl_call(hpgl_add_point_to_path(pgls, arccoord_x, arccoord_y,
					     (draw ? hpgl_plot_draw_absolute :
					     hpgl_plot_move_absolute), set_ctm));
	  }
	return 0;
}

/* add a 3 point arc to the path */
 int
hpgl_add_arc_3point_to_path(hpgl_state_t *pgls, floatp start_x, floatp
			    start_y, floatp inter_x, floatp inter_y,
			    floatp end_x, floatp end_y, floatp chord_angle,
			    hpgl_plot_function_t draw)
{
	/* handle unusual cases as per pcltrm */
	if ( hpgl_3_same_points(start_x, start_y,
				inter_x, inter_y,
				end_x, end_y) )
	  {
	    hpgl_call(hpgl_add_point_to_path(pgls, start_x, start_y, draw, true));
	    return 0;
	  }
	if ( hpgl_3_no_intermediate(start_x, start_y,
				    inter_x, inter_y,
				    end_x, end_y) )
	  {
	    hpgl_call(hpgl_add_point_to_path(pgls, start_x, start_y, draw, true));
	    hpgl_call(hpgl_add_point_to_path(pgls, end_x, end_y, draw, true));
	    return 0;
	  }
	if ( hpgl_3_same_endpoints(start_x, start_y,
				   inter_x, y_inter,
				   end_x, end_y) )
	  {
	    hpgl_call(hpgl_add_arc_to_path(pgls, (start_x + inter_x) / 2.0,
					   (start_y + inter_y) / 2.0,
					   (hypot((inter_x - start_x),
						  (inter_y - start_y)) / 2.0),
					   0.0, 360.0, chord_angle, false,
					   draw, true));
	    return 0;
	  }

	if ( hpgl_3_colinear_points(start_x, start_y, inter_x, inter_y, end_x, end_y) )
	  {
	    if ( hpgl_3_intermediate_between(start_x, start_y,
					     inter_x, inter_y,
					     end_x, end_y) )
	      {
		hpgl_call(hpgl_add_point_to_path(pgls, start_x, start_y, draw, true));
		hpgl_call(hpgl_add_point_to_path(pgls, end_x, end_x, draw, true));
	      }
	    else
	      {
		hpgl_call(hpgl_add_point_to_path(pgls, start_x, start_y, draw, true));
		hpgl_call(hpgl_add_point_to_path(pgls, inter_x, inter_y, draw, true));
		hpgl_call(hpgl_add_point_to_path(pgls, end_x, end_y, draw, true));
	      }
	    return 0;
	  }

	/* normal 3 point arc case */
	{
	  hpgl_real_t center_x, center_y, radius;
	  hpgl_real_t start_angle, inter_angle, end_angle;
	  hpgl_real_t sweep_angle;

	  hpgl_call(hpgl_compute_arc_center(start_x, start_y,
					    inter_x, inter_y,
					    end_x, end_y,
					    &center_x, &center_y));

	  radius = hypot(start_x - center_x, start_y - center_y);
	  start_angle = radians_to_degrees *
	    hpgl_compute_angle(start_x - center_x, start_y - center_y);

	  inter_angle = radians_to_degrees *
	    hpgl_compute_angle(inter_x - center_x, inter_y - center_y);

	  end_angle = radians_to_degrees *
	    hpgl_compute_angle(end_x - center_x, end_y - center_y);
	  sweep_angle = end_angle - start_angle;

	    /*
	     * Figure out which direction to draw the arc, depending on the
	     * relative position of start, inter, and end.  Case analysis
	     * shows that we should draw the arc counter-clockwise from S to
	     * E iff exactly 2 of S<I, I<E, and E<S are true, and clockwise
	     * if exactly 1 of these relations is true.  (These are the only
	     * possible cases if no 2 of the points coincide.)
	     */

	  if ( (start_angle < inter_angle) + (inter_angle < end_angle) +
	       (end_angle < start_angle) == 1
	       )
	    {
	      if ( sweep_angle > 0 )
		sweep_angle -= 360;
	    }
	  else
	    {
	      if ( sweep_angle < 0 )
		sweep_angle += 360;
	    }

	  hpgl_call(hpgl_add_arc_to_path(pgls, center_x, center_y,
					 radius, start_angle, sweep_angle,
					 (sweep_angle < 0.0 ) ?
					 -chord_angle : chord_angle, false,
					 draw, true));
	  return 0;
	}
}

/* Bezier's are handled a bit differently than arcs as we use the
   gs_curveto() operator directly in lieue of flattening and using
   gs_moveto() and gs_lineto(). */

 int
hpgl_add_bezier_to_path(hpgl_state_t *pgls, floatp x1, floatp y1,
			floatp x2, floatp y2, floatp x3, floatp y3,
			floatp x4, floatp y4, hpgl_plot_function_t draw)
{
    gs_fixed_point dummy_pt;
    gx_path *       ppath = gx_current_path(pgls->pgs);
    /* Don't add superflous points in polygon mode */
    if ( ( !pgls->g.polygon_mode ) || 
	 ( !ppath->current_subpath ) )
	hpgl_call(hpgl_add_point_to_path(pgls, x1, y1,
					 hpgl_plot_move_absolute, true));
    if ( draw )
	hpgl_call(gs_curveto(pgls->pgs, x2, y2, x3, y3, x4, y4));
    /* update hpgl's state position to last point of the curve. */
    {
	gs_point point;
	point.x = x4; point.y = y4;
	hpgl_call(hpgl_set_current_position(pgls, &point));
    }
    return 0;
}

/*
 * an implicit gl/2 style closepath.  If the first and last point are
 * the same the path gets closed. HAS - eliminate CSE gx_current_path
 */
 int
hpgl_close_path(
    hpgl_state_t *  pgls
)
{
    gs_point        first, last;
    gs_fixed_point  first_device;
    gx_path *ppath = gx_current_path(pgls->pgs);
    if (!ppath->current_subpath)
	return 0;
    /* if we do not have a subpath there is nothing to do, get the
       first points of the path in device space and convert to floats */
    if ( gx_path_subpath_start_point(gx_current_path(pgls->pgs),
				     &first_device) < 0 )
 	return 0;
    first.x = fixed2float(first_device.x);
    first.y = fixed2float(first_device.y);
    /* get gl/2 current position -- always current units */
    hpgl_call(hpgl_get_current_position(pgls, &last));
    /* convert to device space using the current ctm */
    hpgl_call(gs_transform(pgls->pgs, last.x, last.y, &last));
    /*
     * if the first and last are the same close the path (i.e
     * force gs to apply join and miter)
     */
    if (equal(first.x, last.x) && equal(first.y, last.y))
	hpgl_call(gs_closepath(pgls->pgs));
    return 0;
}


/* To implement centered we simply slide everything up and left.  This
   is not exactly correct but produces the desired results in most
   cases.  Note this routine is sensitive to the orientation of the
   device */
int
hpgl_set_special_pixel_placement(hpgl_state_t *pgls, hpgl_rendering_mode_t render_mode)
{
    if ( pgls->pp_mode == 1 ) {
	gs_matrix default_matrix;
	gs_point distance, adjust;
	gx_path *ppath = gx_current_path(pgls->pgs);
	/* arbitrary just need the signs after transformation to
	   device space */
	gs_setfilladjust(pgls->pgs, 0, 0);
	adjust.x = -1; 
	adjust.y = -1;           
	/* determine the adjustment in device space */
	hpgl_call(gs_defaultmatrix(pgls->pgs, &default_matrix));
	hpgl_call(gs_distance_transform(adjust.x, adjust.y, &default_matrix, &distance));
	/* translate all points in the path by the adjustment.  The
           1.5 is a hack, it should be 1 */
	hpgl_call(gx_path_translate(ppath,
		   float2fixed(1.0 * (distance.x / fabs(distance.x))), 
		   float2fixed(1.0 * (distance.y / fabs(distance.y)))));
	hpgl_call(gx_path_translate(ppath,
		   float2fixed(-1.0 * (distance.x / fabs(distance.x))), 
		   float2fixed(-1.0 * (distance.y / fabs(distance.y)))));
    }
    return 0;
}

/*
 * Draw (stroke or fill) the current path.
 */
 int
hpgl_draw_current_path(
    hpgl_state_t *          pgls,
    hpgl_rendering_mode_t   render_mode
)
{
    gs_fixed_point          dummy_pt;
    gs_state *              pgs = pgls->pgs;
    pcl_pattern_set_proc_t  set_proc;
    int                     code = 0;
    gx_path *       ppath = gx_current_path(pgls->pgs);

    /* check if we have a current path - we don't need the current
       point */
    if (!ppath->current_subpath) {
	hpgl_call(hpgl_clear_current_path(pgls));
 	return 0;
    }

    if ( render_mode == hpgl_rm_vector_no_close )
	render_mode = hpgl_rm_vector;
    else
	hpgl_call(hpgl_close_path(pgls));

    hpgl_call(hpgl_set_drawing_state(pgls, render_mode));

    switch (render_mode) {
    case hpgl_rm_character:
	{
	    /* HAS need to set attributes in set_drawing color (next 2) */

	    /* Intellifonts require eofill, but TrueType require fill. */
	    /****** HACK: look at the scaling technology of ******/
	    /****** the current font to decide. ******/
	    int     (*fill)(P1(gs_state *));

	    if (pgls->g.font->scaling_technology == plfst_Intellifont)
		fill = gs_eofill;
	    else
		fill = gs_fill;

	    switch (pgls->g.character.fill_mode) {
		
	    case hpgl_char_solid_edge:
                set_proc = pcl_pattern_get_proc_FT(hpgl_FT_pattern_solid_pen1);
                if ((code = set_proc(pgls, hpgl_get_selected_pen(pgls), false)) < 0)
                    return code;
		hpgl_call((*fill)(pgs));
		/* falls through */

	    case hpgl_char_edge:
		if (pgls->g.bitmap_fonts_allowed)
		    break;	/* no edging */
                set_proc = pcl_pattern_get_proc_FT(hpgl_FT_pattern_solid_pen1);
                if ((code = set_proc(pgls, hpgl_get_character_edge_pen(pgls), false)) < 0)
                    return code;
		hpgl_call(hpgl_set_plu_ctm(pgls));
		hpgl_call(gs_setlinewidth(pgls->pgs, 
         		  pgls->g.font_selection[pgls->g.font_selected].params.height_4ths * 0.0375));
		hpgl_call(gs_stroke(pgs));
		break;

	    case hpgl_char_fill:
		/* the fill has already been done if the fill type is
                   hpgl/2 vector fills.  This was handled when we set
                   the drawing color */
		if ((pgls->g.fill.type != hpgl_FT_pattern_one_line) &&
		    (pgls->g.fill.type != hpgl_FT_pattern_two_lines))
		    hpgl_call((*fill)(pgs));
		else
		    hpgl_call(hpgl_clear_current_path(pgls));
		break;
	    case hpgl_char_fill_edge:
		if (pgls->g.bitmap_fonts_allowed)
		    break;	/* no edging */
		/* the fill has already been done if the fill type is
                   hpgl/2 vector fills.  This was handled when we set
                   the drawing color.  gsave to preserve the path for
                   subsequent edging */
		if ((pgls->g.fill.type != hpgl_FT_pattern_one_line) &&
		    (pgls->g.fill.type != hpgl_FT_pattern_two_lines)) {
		    hpgl_call(hpgl_gsave(pgls));
		    /* all character fills appear to have 0 fill adjustment */
		    gs_setfilladjust(pgls->pgs, 0, 0);
		    hpgl_call((*fill)(pgs));
		    hpgl_call(hpgl_grestore(pgls));
		}
		set_proc = pcl_pattern_get_proc_FT(hpgl_FT_pattern_solid_pen1);
		if ((code = set_proc(pgls, hpgl_get_character_edge_pen(pgls), false)) < 0)
		    return code;
		hpgl_call(hpgl_set_plu_ctm(pgls));
		/* use the default raster operation for the edge.  It
                   is automaticall restored next drawing operation */
		hpgl_call(gs_setrasterop(pgls->pgs, (gs_rop3_t)252));
		hpgl_call(gs_setlinewidth(pgls->pgs,
			  pgls->g.font_selection[pgls->g.font_selected].params.height_4ths * 0.0375));
		hpgl_call(gs_stroke(pgs));
		break;
	    }
	    break;
	}
	break;
    case hpgl_rm_polygon:
	hpgl_set_special_pixel_placement(pgls, hpgl_rm_polygon);
	if (pgls->g.fill_type == hpgl_even_odd_rule)
	    hpgl_call(gs_eofill(pgs));
	else    /* hpgl_winding_number_rule */
	    hpgl_call(gs_fill(pgs));
	break;

    case hpgl_rm_clip_and_fill_polygon:
	/*
	 * A bizarre HPISM - If the pen is white we do a solid
         * white fill this is true for *all* fill types (arg!) as
         * tested on the 6mp.  If pen 1 (black)
         * hpgl_set_drawing_color() handles this case by drawing
         * the lines that comprise the vector fill
         */
	if (hpgl_get_selected_pen(pgls) == 0)
	    hpgl_call(gs_fill(pgls->pgs));
	hpgl_call(hpgl_clear_current_path(pgls));
	break;

    case hpgl_rm_vector:
    case hpgl_rm_vector_fill:
	/*
         * we reset the ctm before stroking to preserve the line width 
         * information, then restore the ctm.
         */
	{
	    gs_matrix save_ctm;
	    hpgl_call(gs_currentmatrix(pgs, &save_ctm));
	    hpgl_call(hpgl_set_plu_ctm(pgls));
	    if ( !pgls->g.line.current.is_solid && (pgls->g.line.current.type == 0) )
		hpgl_call(gs_reversepath(pgls->pgs));
	    hpgl_call(gs_stroke(pgls->pgs));
	    gs_setmatrix(pgs, &save_ctm);
	    break;
	}
    default :
        dprintf("unknown render mode\n");
    }

    return 0;
}

 int
hpgl_copy_current_path_to_polygon_buffer(
    hpgl_state_t *  pgls
)
{
    gx_path *       ppath = gx_current_path(pgls->pgs);

    gx_path_new(&pgls->g.polygon.buffer.path);
    gx_path_copy(ppath, &pgls->g.polygon.buffer.path);
    return 0;
}

 int
hpgl_copy_polygon_buffer_to_current_path(
    hpgl_state_t *  pgls
)
{
    gx_path *       ppath = gx_current_path(pgls->pgs);

    gx_path_copy(&pgls->g.polygon.buffer.path, ppath);
    return 0;
}
