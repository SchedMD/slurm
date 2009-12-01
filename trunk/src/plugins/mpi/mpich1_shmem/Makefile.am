# Makefile for mpich1/shmem model plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mpi_mpich1_shmem.la

mpi_mpich1_shmem_la_SOURCES = mpich1_shmem.c
mpi_mpich1_shmem_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
