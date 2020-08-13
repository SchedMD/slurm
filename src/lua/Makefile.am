# Makefile for lua library

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

if HAVE_LUA
LUA_LIB = libslurm_lua.la
libslurm_lua_la_SOURCES = slurm_lua.c slurm_lua.h
libslurm_lua_la_LIBADD   = $(lua_LIBS)
libslurm_lua_la_LDFLAGS  = $(LIB_LDFLAGS)
libslurm_lua_la_CFLAGS = $(lua_CFLAGS) $(AM_CFLAGS)
else
LUA_LIB =
EXTRA_libslurm_lua_la_SOURCES = slurm_lua.c slurm_lua.h
endif

noinst_LTLIBRARIES = $(LUA_LIB)
