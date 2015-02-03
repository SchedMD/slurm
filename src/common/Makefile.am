# Makefile for common library
# If you are linking to this lib and know you are not linking to
# anything else that will need variables here link to
# $(top_builddir)/src/common/libcommon.la
# if you are linking to a stand alone program that needs all symbols link to
# $(top_builddir)/src/common/libcommon.o with -ldl added
# This avoids having multiple symbols running around when there should
# only be 1 address per symbol.  If you link to the libcommon.la in
# a plugin you will get 2 addresses for one symbol which could lead to problems.

AUTOMAKE_OPTIONS = foreign

if HAVE_UNSETENV
build_unsetenv_src =
extra_unsetenv_src = unsetenv.c unsetenv.h
else
build_unsetenv_src = unsetenv.c unsetenv.h
extra_unsetenv_src =
endif

AM_CPPFLAGS     = -I$(top_srcdir) $(BG_INCLUDES)

noinst_PROGRAMS = libcommon.o libeio.o libspank.o
# This is needed if compiling on windows
EXEEXT=

noinst_LTLIBRARIES = 			\
	libcommon.la 			\
	libdaemonize.la 		\
	libeio.la  			\
	libspank.la

libcommon_la_SOURCES = 			\
	cpu_frequency.c cpu_frequency.h \
	assoc_mgr.c assoc_mgr.h 	\
	xmalloc.c xmalloc.h 		\
	xassert.c xassert.h		\
	xstring.c xstring.h		\
	xsignal.c xsignal.h		\
	strnatcmp.c strnatcmp.h		\
	forward.c forward.h     	\
	strlcpy.c strlcpy.h		\
	list.c list.h 			\
	xtree.c xtree.h			\
	xhash.c xhash.h			\
	net.c net.h                     \
	log.c log.h			\
	cbuf.c cbuf.h			\
	safeopen.c safeopen.h		\
	bitstring.c bitstring.h 	\
	mpi.c mpi.h                     \
	pack.c pack.h			\
	parse_config.c parse_config.h	\
	parse_value.c parse_value.h	\
	parse_spec.c parse_spec.h	\
	plugin.c plugin.h		\
	plugrack.c plugrack.h		\
	print_fields.c print_fields.h	\
	read_config.c read_config.h	\
	node_select.c node_select.h	\
	env.c env.h      		\
	fd.c fd.h       		\
	slurm_cred.h       		\
	slurm_cred.c			\
	slurm_errno.c			\
	slurm_ext_sensors.c slurm_ext_sensors.h \
	slurm_priority.c		\
	slurm_priority.h		\
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
	slurmdb_defs.c slurmdb_defs.h   \
	slurmdb_pack.c slurmdb_pack.h   \
	slurmdbd_defs.c slurmdbd_defs.h	\
	working_cluster.c working_cluster.h   \
	uid.c uid.h			\
	util-net.c util-net.h		\
	slurm_auth.c slurm_auth.h	\
	slurm_acct_gather.c slurm_acct_gather.h \
	slurm_accounting_storage.c slurm_accounting_storage.h \
	slurm_jobacct_gather.c slurm_jobacct_gather.h \
	slurm_acct_gather_energy.c slurm_acct_gather_energy.h \
	slurm_acct_gather_profile.c slurm_acct_gather_profile.h \
	slurm_acct_gather_infiniband.c slurm_acct_gather_infiniband.h \
	slurm_acct_gather_filesystem.c slurm_acct_gather_filesystem.h \
	slurm_jobcomp.c slurm_jobcomp.h	\
	slurm_route.c slurm_route.h \
	slurm_topology.c slurm_topology.h \
	switch.c switch.h		\
	arg_desc.c arg_desc.h		\
	macros.h			\
	malloc.c malloc.h		\
	getopt.h getopt.c getopt1.c     \
	$(build_unsetenv_src)		\
	slurm_selecttype_info.c 	\
	slurm_selecttype_info.h		\
	slurm_resource_info.c 		\
	slurm_resource_info.h		\
	hostlist.c hostlist.h		\
	slurm_step_layout.c slurm_step_layout.h	\
	checkpoint.c checkpoint.h	\
	job_resources.c job_resources.h	\
	parse_time.c parse_time.h	\
	job_options.c job_options.h	\
	global_defaults.c		\
	timers.c timers.h		\
	slurm_xlator.h			\
	stepd_api.c stepd_api.h		\
	write_labelled_message.c	\
	write_labelled_message.h	\
	proc_args.c proc_args.h		\
	slurm_strcasestr.c slurm_strcasestr.h \
	node_conf.h node_conf.c		\
	gres.h gres.c			\
	entity.h entity.c		\
	layout.h layout.c		\
	layouts_mgr.h layouts_mgr.c	\
	xcgroup_read_config.c xcgroup_read_config.h

EXTRA_libcommon_la_SOURCES = 		\
	$(extra_unsetenv_src)		\
	uthash/LICENSE			\
	uthash/README			\
	uthash/uthash.h

libdaemonize_la_SOURCES =  		\
	daemonize.c       	 	\
	daemonize.h

libeio_la_SOURCES = 	   		\
	eio.c eio.h	   		\
	io_hdr.c io_hdr.h

libspank_la_SOURCES = \
	plugstack.c plugstack.h \
	optz.c      optz.h

libcommon_la_LIBADD   = $(DL_LIBS)

libcommon_la_LDFLAGS  = $(LIB_LDFLAGS) -module --export-dynamic

# This was made so we could export all symbols from libcommon
# on multiple platforms
libcommon_o_SOURCES =
libcommon.o$(EXEEXT) :  $(libcommon_la_OBJECTS) $(libcommon_la_DEPENDENCIES)
	$(LINK)  $(libcommon_la_OBJECTS)

# This was made so we could export all symbols from libeio
# on multiple platforms
libeio_o_SOURCES =
libeio.o$(EXEEXT) :  $(libeio_la_OBJECTS) $(libeio_la_DEPENDENCIES)
	$(LINK)  $(libeio_la_OBJECTS)

# This was made so we could export all symbols from libspank
# on multiple platforms
libspank_o_SOURCES =
libspank.o$(EXEEXT) :  $(libspank_la_OBJECTS) $(libspank_la_DEPENDENCIES)
	$(LINK)  $(libspank_la_OBJECTS)


global_defaults.c : $(top_builddir)/config.h Makefile
	@( echo "/* This file autogenerated by src/common/Makefile */"; \
	   echo	"char *default_plugin_path = \"$(pkglibdir)\";"; \
	   echo \
	   "char *default_slurm_config_file = \"$(sysconfdir)/slurm.conf\";"; \
	   echo "char *default_plugstack = \"$(sysconfdir)/plugstack.conf\";";\
	 ) > global_defaults.c
