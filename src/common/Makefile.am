# Makefile for common library 

AUTOMAKE_OPTIONS = foreign

INCLUDES     = -I$(top_srcdir) $(SSL_CPPFLAGS) 

if WITH_ELAN
elan_sources = qsw.c
else
elan_sources = 
endif

if WITH_AUTHD
auth_sources = slurm_auth_authd.c
else
auth_sources = slurm_auth_authd.c
endif

noinst_LTLIBRARIES = 	libcommon.la libdaemonize.la libcred.la 


libcommon_la_SOURCES = 	\
	xmalloc.c 	\
	xassert.c	\
	xstring.c	\
	xsignal.c	\
	strlcpy.c	\
	list.c  	\
	fd.c		\
	log.c		\
	eio.c		\
	cbuf.c		\
	safeopen.c	\
	bitstring.c     \
	pack.c		\
	hostlist.c	\
	parse_spec.c	\
	read_config.c	\
	slurm_errno.c	\
	slurm_protocol_api.c	\
	slurm_protocol_pack.c	\
	slurm_protocol_util.c	\
	slurm_protocol_socket_implementation.c	\
	slurm_protocol_defs.c	\
	util-net.c	\
	$(elan_sources) \
        $(auth_sources)

noinst_HEADERS = 	\
	xmalloc.h 	\
	xassert.h 	\
	xstring.h	\
	xsignal.h	\
	macros.h	\
	list.h 		\
	log.h 		\
	eio.h		\
	cbuf.h		\
	fd.h		\
	safeopen.h	\
	strlcpy.h	\
	bitstring.h     \
	pack.h		\
	hostlist.h	\
	parse_spec.h	\
	read_config.h	\
	slurm_auth.h    \
	slurm_protocol_api.h	\
	slurm_protocol_pack.h	\
	slurm_protocol_util.h	\
	slurm_protocol_defs.h	\
	slurm_protocol_common.h	\
	slurm_protocol_socket_common.h	\
	slurm_protocol_mongo_common.h	\
	slurm_protocol_interface.h	\
	slurm_errno.h	\
	util-net.h	\
	wrapper.h	\
	qsw.h			

libdaemonize_la_SOURCES =  \
	daemonize.c        \
	daemonize.h

libcred_la_SOURCES =       \
        credential_utils.c \
	credential_utils.h \
	signature_utils.c  \
	signature_utils.h

EXTRA_libcommon_la_SOURCES = \
	qsw.c

libcommon_la_LIBADD   = $(AUTHD_LIBS) $(SSL_LIBS)
libcred_la_LIBADD     = $(SSL_LIBS) 
libcred_la_LDFLAGS    = $(SSL_LDFLAGS)	
