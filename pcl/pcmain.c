/* Copyright (C) 1996 Aladdin Enterprises.  All rights reserved.
   Unauthorized use, copying, and/or distribution prohibited.
 */

/* pcmain.c */
/* PCL5 main program */
#include "malloc_.h"
#include "math_.h"
#include "memory_.h"
#include "stdio_.h"
#include "scommon.h"			/* for pparse.h */
#include "pcparse.h"
#include "pcstate.h"
#include "gdebug.h"
#include "gp.h"
#include "gscdefs.h"
#include "gsgc.h"
#include "gslib.h"
#include "gsmatrix.h"		/* for gsstate.h */
#include "gspaint.h"
#include "gsstate.h"
#include "gscoord.h"
#include "gspath.h"
#include "gxalloc.h"
#include "pjparse.h"
#include "plmain.h"

/*#define BUFFER_SIZE 1024*/	/* minimum is 17 */
#define BUFFER_SIZE 17

extern FILE *gs_debug_out;
extern gs_ref_memory_t *ialloc_alloc_state(P2(gs_memory_t *, uint));

/* Define the table of pointers to initialization data. */
#define init_(init) extern const pcl_init_t init;
#include "pconfig.h"
#undef init_
const pcl_init_t *pcl_init_table[] = {
#define init_(init) &init,
#include "pconfig.h"
#undef init_
	    0
};

/* Pause at the end of each page. */
private int
pause_end_page(pcl_state_t *pcls)
{	int code = pcl_default_end_page(pcls);
	fprintf(stderr, "End of page, press <enter> to continue.\n");
	getchar();
	return code;
}

#ifdef DEBUG
/* Print memory usage. */
private void
print_memory(pcl_state_t *pcls)
{	gs_memory_status_t status;

	gs_memory_status(pcls->memory, &status);
	dprintf2("Memory allocated = %lu, used = %lu\n",
		 status.allocated, status.used);
}
#endif

/* Define a minimal spot function. */
private float
cspotf(floatp x, floatp y)
{	return (x * x + y * y) / 2;
}

/* Here is the real main program. */
int
main(int argc, char *argv[])
{	gs_ref_memory_t *imem;
#define mem ((gs_memory_t *)imem)
	vm_spaces spaces;
	pl_main_instance_t inst;
	int i;
	gs_state *pgs;
	pcl_parser_state_t pstate;
	pcl_state_t state;
	pjl_parser_state_t pjstate;

	/* Initialize the library. */
	gp_init();
	gs_lib_init(stdout);
	gs_debug_out = stdout;
	imem = ialloc_alloc_state(&gs_memory_default, 20000);
	imem->space = 0;		/****** WRONG ******/
	{ int i;
	  for ( i = 0; i < countof(spaces.indexed); ++i )
	    spaces.indexed[i] = 0;
	  spaces.named.local = spaces.named.global = imem;
	}
	pl_main_init(&inst);
	i = pl_main_process_options(&inst, argv, argc, mem);
	pl_main_make_gstate(&inst, &pgs);
	/* Set the default CTM for H-P coordinates. */
	gs_clippath(pgs);
	{ gs_rect bbox;
	  gs_pathbbox(pgs, &bbox);
	  gs_translate(pgs, 0.0, bbox.q.y);
	}
	gs_newpath(pgs);
	gs_scale(pgs, 0.01, -0.01);
	{ gs_matrix mat;
	  gs_currentmatrix(pgs, &mat);
	  gs_setdefaultmatrix(pgs, &mat);
	  state.resolution.x = mat.xx * 7200;
	  state.resolution.y = fabs(mat.yy) * 7200;
	  /* Set the simplest possible halftone.  (This is currently */
	  /* irrelevant, because PCL5e printers never halftone.) */
	  { gs_screen_halftone ht;
	    float dpi = fabs(state.resolution.x + state.resolution.y);
	    ht.frequency = dpi / 10;	/* 100 (non-existent) gray levels */
	    ht.angle = 0;
	    ht.spot_function = cspotf;
	    gs_setscreen(pgs, &ht);
	  }
	}
	gs_gsave(pgs);
	gs_erasepage(pgs);
	state.memory = mem;
	state.pgs = pgs;
	/* Run initialization code. */
	{ const pcl_init_t **init;
	  for ( init = pcl_init_table; *init; ++init )
	    if ( (*init)->do_init )
	      { int code = (*(*init)->do_init)(mem);
	        if ( code < 0 )
		  { lprintf1("Error %d during initialization!\n", code);
		    exit(code);
		  }
	      }
	  pcl_do_resets(&state, pcl_reset_initial);
	}
	if ( inst.pause )
	  state.end_page = pause_end_page;

	for ( ; i < argc; ++i )
	  { /* Process one input file. */
	    char *arg = argv[i];
	    FILE *in = fopen(arg, "rb");
	    byte buf[BUFFER_SIZE];
#define buf_last (buf + (BUFFER_SIZE - 1))
	    int len;
	    int code = 0;
	    stream_cursor_read r;
	    bool in_pjl = true;

#ifdef DEBUG
	    dprintf1("Reading %s: ", arg);
	    print_memory(&state);
#endif
	    if ( in == 0 )
	      { fprintf(stderr, "Unable to open %s for reading.\n", arg);
	        exit(1);
	      }

	  pjl_process_init(&pjstate);
	  r.limit = buf - 1;
	  for ( ; ; )
	    { if_debug1('i', "[i][file pos=%ld]\n", (long)ftell(in));
	      r.ptr = buf - 1;
	      len = fread(r.limit + 1, 1, buf_last - r.limit, in);
	      if ( len <= 0 )
		break;
	      r.limit += len;
process:      if ( in_pjl )
		{ code = pjl_process(&pjstate, NULL, &r);
		  if ( code < 0 )
		    break;
		  else if ( code > 0 )
		    { in_pjl = false;
		      pcl_process_init(&pstate);
		      goto process;
		    }
		}
	      else
		{ code = pcl_process(&pstate, &state, &r);
		  if ( code == e_ExitLanguage )
		    in_pjl = true;
		  else if ( code < 0 )
		    break;
		}
	      /* Move unread data to the start of the buffer. */
	      len = r.limit - r.ptr;
	      if ( len > 0 )
		memmove(buf, r.ptr + 1, len);
	      r.limit = buf + (len - 1);
	    }
#ifdef DEBUG
	  dprintf3("Final file position = %ld, exit code = %d, mode = %s\n",
		   (long)ftell(in) - (r.limit - r.ptr), code,
		   (in_pjl ? "PJL" : state.parse_other ? "HP-GL/2" : "PCL"));
#endif
	  fclose(in);

	  /* Read out any status responses. */
	  { byte buf[200];
	    uint count;
	    while ( (count = pcl_status_read(buf, sizeof(buf), &state)) != 0 )
	      fwrite(buf, 1, count, stdout);
	    fflush(stdout);
	  }
	  gs_reclaim(&spaces, true);
	  }
#ifdef DEBUG
	dputs("At end of execution: ");
	print_memory(&state);
#endif
	gs_lib_finit(0, 0);
	exit(0);
	/* NOTREACHED */
}
