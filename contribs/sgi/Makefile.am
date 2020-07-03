#
# Makefile for sgi programs
#

AUTOMAKE_OPTIONS = foreign

if HAVE_NETLOC

bin_PROGRAMS = netloc_to_topology
netloc_to_topology_SOURCES  = netloc_to_topology.c
netloc_to_topology_CPPFLAGS = $(NETLOC_CPPFLAGS) $(HWLOC_CPPFLAGS)
netloc_to_topology_LDFLAGS  = $(NETLOC_LDFLAGS) $(NETLOC_LIBS) $(HWLOC_LDFLAGS) $(HWLOC_LIBS)

endif
