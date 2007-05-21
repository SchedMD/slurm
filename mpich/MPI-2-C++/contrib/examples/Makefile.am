# -*- makefile -*-

AUTOMAKE_OPTIONS	= foreign dist-tarZ no-dependencies

CXX			= $(bindir)/balky
INCLUDES		= $(EXAM_OPTFLAGS)
EXTRA_DIST		= README

noinst_PROGRAMS		= chapter_10_mpi2 hello_world pi ring topology user_bcast

chapter_10_mpi2_SOURCES	= chapter_10_mpi2.cc
hello_world_SOURCES	= hello_world.cc
pi_SOURCES		= pi.cc
pi_LDADD		= -lm
ring_SOURCES		= ring.cc
topology_SOURCES	= topology.cc
user_bcast_SOURCES	= user_bcast.cc

