#    Copyright (C) 1997, 1998 Aladdin Enterprises.  All rights reserved.
#    Unauthorized use, copying, and/or distribution prohibited.

# sgi_top.mak
# Generic top-level makefile for Silicon Graphics Irix 6.* platforms.

SHELL   = /bin/sh

# The product-specific top-level makefile defines the following:
#
#	MAKEFILE, CCLD, COMMONDIR, CONFIG, DEVICE_DEVS, GCFLAGS, GENDIR
#	GLSRCDIR, GLGENDIR, GLOBJDIR,
#	MAIN_OBJ, TARGET_DEVS, TARGET_XE
#
# It also must include the product-specific *.mak.

default: $(TARGET_XE)

# Platform specification
include $(COMMONDIR)/sgidefs.mak
include $(COMMONDIR)/unixdefs.mak
include $(COMMONDIR)/generic.mak

# Configure for debugging
debug:
	make GENOPT='-DDEBUG'

# Configure for profiling
pg-fp:
	make GENOPT=''                                         \
             CFLAGS='-pg -O2 $(CDEFS) $(SGICFLAGS) $(XCFLAGS)' \
             LDFLAGS='$(XLDFLAGS) -pg'

# Configure for optimization (product configuration)
product:
	make GENOPT=''                                      \
             CFLAGS='-O2 $(CDEFS) $(SGICFLAGS) $(XCFLAGS)'  \
             FPU_TYPE=1                                     \
             XOBJS='$(GLOBJDIR)/gsfemu.o'

# Build the required GS library files.
# It's simplest always to build the floating point emulator,
# even though we don't always link it in.
$(GENDIR)/ldl$(CONFIG).tr: $(MAKEFILE)
	-mkdir $(GLGENDIR)
	-mkdir $(GLOBJDIR)
	 make                                                               \
	     SGICFLAGS='$(SGICFLAGS)' FPU_TYPE='$(FPU_TYPE)'                \
	     CONFIG='$(CONFIG)'                                             \
             FEATURE_DEVS='$(FEATURE_DEVS)'                                 \
	     DEVICE_DEVS='$(DEVICE_DEVS) $(DD)bbox.dev'                     \
	     BAND_LIST_STORAGE=memory BAND_LIST_COMPRESSOR=zlib             \
	     GLSRCDIR='$(GLSRCDIR)'                                         \
	     GLGENDIR='$(GLGENDIR)' GLOBJDIR='$(GLOBJDIR)'                  \
	     -f $(GLSRCDIR)/sgicclib.mak                                    \
	     $(GLOBJDIR)/ld$(CONFIG).tr                                     \
	     $(GLOBJDIR)/gsargs.o $(GLOBJDIR)/gsfemu.o \
	     $(GLOBJDIR)/gconfig$(CONFIG).o $(GLOBJDIR)/gscdefs$(CONFIG).o
	cp $(GLOBJDIR)/ld$(CONFIG).tr $(GENDIR)/ldl$(CONFIG).tr

# Build the configuration file.
$(GENDIR)/pconf$(CONFIG).h $(GENDIR)/ldconf$(CONFIG).tr: $(TARGET_DEVS)         \
                                                        $(GLOBJDIR)/genconf$(XE)
	$(GLOBJDIR)/genconf -n - $(TARGET_DEVS) -h $(GENDIR)/pconf$(CONFIG).h   \
                         -p "%s&s&&" -o $(GENDIR)/ldconf$(CONFIG).tr

# Link a Unix executable.
$(TARGET_XE): $(GENDIR)/ldl$(CONFIG).tr $(GENDIR)/ldconf$(CONFIG).tr $(MAIN_OBJ)
	$(ECHOGS_XE) -w $(GENDIR)/ldt.tr -n - $(CCLD) $(LDFLAGS) $(XLIBDIRS)\
                     -o $(TARGET_XE)
	$(ECHOGS_XE) -a $(GENDIR)/ldt.tr -n -s                              \
                     $(GLOBJDIR)/gsargs.o                                   \
                     $(GLOBJDIR)/gconfig$(CONFIG).o                         \
                     $(GLOBJDIR)/gscdefs$(CONFIG).o                         \
                     -s
	$(ECHOGS_XE) -a ldt.tr -n -s $(XOBJS) -s
	cat $(GENDIR)/ldl$(CONFIG).tr $(GENDIR)/ldconf$(CONFIG).tr          \
                     >> $(GENDIR)/ldt.tr
	$(ECHOGS_XE) -a $(GENDIR)/ldt.tr -s - $(MAIN_OBJ) $(EXTRALIBS) -lm
	LD_RUN_PATH=$(XLIBDIR); export LD_RUN_PATH; sh < $(GENDIR)/ldt.tr
