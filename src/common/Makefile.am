# Makefile for common library 

AUTOMAKE_OPTIONS = foreign

#
if DEBUG_MODULES
test_modules = bits_bytes bitstring
else
test_modules = 
endif

if WITH_ELAN
elan_sources = qsw.c
else
elan_sources = 
endif

if WITH_AUTHD
authd_sources = slurm_auth_authd.c
else
authd_sources = slurm_auth_authd.c
endif

noinst_LTLIBRARIES = 	libcommon.la libdaemonize.la libcred.la 

CPPFLAGS = $(SSL_CPPFLAGS)

libcommon_la_SOURCES = 	xmalloc.c 	\
		      	xassert.c	\
		      	xstring.c	\
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
			slurm_errno.c	\
			slurm_protocol_api.c	\
			slurm_protocol_pack.c	\
			slurm_protocol_util.c	\
			slurm_protocol_socket_implementation.c	\
			slurm_protocol_defs.c	\
			$(elan_sources) \
			$(authd_sources)

noinst_HEADERS = 	xmalloc.h 	\
		 	xassert.h 	\
		 	xstring.h	\
			macros.h	\
		 	list.h 		\
		 	log.h 		\
			eio.h		\
			cbuf.h		\
			safeopen.h	\
		 	strlcpy.h	\
			bitstring.h     \
			pack.h		\
			hostlist.h	\
			parse_spec.h	\
			slurm_protocol_api.h	\
			slurm_protocol_pack.h	\
			slurm_protocol_util.h	\
			slurm_protocol_defs.h	\
			slurm_protocol_common.h	\
			slurm_protocol_socket_common.h	\
			slurm_protocol_mongo_common.h	\
			slurm_protocol_interface.h	\
			slurm_errno.h		\
			qsw.h			

libdaemonize_la_SOURCES = \
			daemonize.c
libcred_la_SOURCES = credential_utils.c signature_utils.c list.c

EXTRA_libcommon_la_SOURCES = \
			qsw.c

libcred_la_LIBADD   = $(SSL_LIBS) $(AUTHD_LIBS)
libcred_la_LDFLAGS  = $(SSL_LDFLAGS)	
