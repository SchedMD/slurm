# $Id$
#
# Makefile for smap

AUTOMAKE_OPTIONS = foreign

# BGLSYS_DIR = /bgl
# BRIDGE_PATH = $(BGLSYS_DIR)/bglsched
# SCHED_PATH = $(BGLSYS_DIR)/bglsched
# BGLALLOC_PATH = $(SCHED_PATH)/bglsched
# BGLDB2_DIR = /opt/IBM/db2/V8.1/lib
# LIBS = -l$(NCURSES) -L$(BGLDB2_DIR) -Wl,-rpath $(BRIDGE_PATH) -L$(BRIDGE_PATH) -lbgldb -Wl,-rpath $(BGLDB2_DIR) -L$(BGLDB2_DIR) -ldb2 -lbglmachine -lexpat -ltableapi
# INCLUDES = -I$(top_srcdir) -I$(BRIDGE_PATH) -I$(BGLALLOC_PATH)
# bin_PROGRAMS = smap

# smap_LDADD =					  \
# 	$(top_builddir)/src/partition_allocator/libpartition_allocator.la \
# 	$(BRIDGE_PATH)/bglbridge.a

# noinst_HEADERS = smap.h 
# smap_SOURCES = smap.c job_functions.c partition_functions.c configure_functions.c grid_functions.c opts.c

# force:
# $(smap_LDADD) : force
# 	@cd `dirname $@` && $(MAKE) `basename $@`

# smap_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

LIBS=$(NCURSES) 
INCLUDES = -I$(top_srcdir) $(BGL_INCLUDES) 
bin_PROGRAMS = smap

smap_LDADD = \
	$(top_builddir)/src/partition_allocator/libpartition_allocator.la 

noinst_HEADERS = smap.h 
smap_SOURCES = smap.c job_functions.c partition_functions.c configure_functions.c grid_functions.c opts.c

force:
$(smap_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

smap_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(BGL_LDFLAGS)

