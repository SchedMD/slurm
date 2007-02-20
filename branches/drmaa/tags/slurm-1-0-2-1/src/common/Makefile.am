# $Id$
# Makefile for common library 

AUTOMAKE_OPTIONS = foreign

if HAVE_UNSETENV
build_unsetenv_src = 
extra_unsetenv_src = unsetenv.c unsetenv.h
else
build_unsetenv_src = unsetenv.c unsetenv.h
extra_unsetenv_src = 
endif

INCLUDES     = -I$(top_srcdir) $(SSL_CPPFLAGS) 

noinst_LTLIBRARIES = 			\
	libcommon.la 			\
	libdaemonize.la 		\
	libeio.la  	

libcommon_la_SOURCES = 			\
	xmalloc.c xmalloc.h 		\
	xassert.c xassert.h		\
	xstring.c xstring.h		\
	xsignal.c xsignal.h		\
	global_srun.c                   \
	global_srun.h                   \
	strlcpy.c strlcpy.h		\
	list.c list.h 			\
	net.c net.h                     \
	fd.c fd.h			\
	log.c log.h			\
	cbuf.c cbuf.h			\
	safeopen.c safeopen.h		\
	bitstring.c bitstring.h 	\
	mpi.c mpi.h                     \
	pack.c pack.h			\
	parse_spec.c parse_spec.h	\
	plugin.c plugin.h		\
	plugrack.c plugrack.h		\
	read_config.c read_config.h	\
	node_select.c node_select.h	\
	env.c env.h      		\
	slurm_cred.h       		\
	slurm_cred.c			\
	slurm_errno.c			\
	slurm_protocol_api.c		\
	slurm_protocol_api.h		\
	slurm_protocol_pack.c		\
	slurm_protocol_pack.h		\
	slurm_protocol_util.c		\
	slurm_protocol_util.h		\
	slurm_protocol_socket_implementation.c	\
	slurm_protocol_socket_common.h	\
	slurm_protocol_common.h		\
	slurm_protocol_interface.h	\
	slurm_protocol_defs.c		\
	slurm_protocol_defs.h		\
	slurm_rlimits_info.h		\
	slurm_rlimits_info.c		\
	uid.c uid.h			\
	util-net.c util-net.h		\
	slurm_auth.c slurm_auth.h	\
	slurm_jobacct.c slurm_jobacct.h \
	slurm_jobcomp.c slurm_jobcomp.h	\
	switch.c switch.h		\
	arg_desc.c arg_desc.h		\
	macros.h			\
	malloc.c malloc.h		\
	getopt.h getopt.c getopt1.c     \
	$(build_unsetenv_src)		\
	hostlist.c hostlist.h		\
	dist_tasks.c dist_tasks.h	\
	checkpoint.c checkpoint.h	\
	parse_time.c parse_time.h	\
	slurm_xlator.h

EXTRA_libcommon_la_SOURCES = 	\
	$(extra_unsetenv_src)

libdaemonize_la_SOURCES =  		\
	daemonize.c       	 	\
	daemonize.h        		\
	fd.c fd.h

libeio_la_SOURCES = 	   		\
	eio.c eio.h	   		\
	io_hdr.c io_hdr.h

libcommon_la_LIBADD   = $(SSL_LIBS) -ldl

libcommon_la_LDFLAGS  = $(LIB_LDFLAGS) $(SSL_LDFLAGS)
