# -*- makefile -*-

AUTOMAKE_OPTIONS	= foreign dist-tarZ no-dependencies

common_h		= \
			mpi2c++_config.h \
			mpi++.h \
			mpi2c++_map.h \
			mpi2c++_list.h \
			constants.h \
			functions.h \
			datatype.h \
			exception.h \
			op.h \
			status.h \
			request.h \
			group.h \
			comm.h \
			errhandler.h \
			intracomm.h \
			topology.h \
			intercomm.h \
			datatype_inln.h \
			functions_inln.h \
			request_inln.h \
			comm_inln.h \
			intracomm_inln.h \
			topology_inln.h \
			intercomm_inln.h \
			group_inln.h \
			op_inln.h \
			errhandler_inln.h \
			status_inln.h
profile_h		= \
			pmpi++.h \
			pcomm.h \
			pdatatype.h \
			perrhandler.h \
			pexception.h \
			pgroup.h \
			pgroup_inln.h \
			pintercomm.h \
			pintracomm.h \
			pop.h \
			pop_inln.h \
			prequest.h \
			prequest_inln.h \
			pstatus.h \
			pstatus_inln.h \
			ptopology.h

if WANT_PROFILING
pro_headers		= $(profile_h)
else
pro_headers		=
endif

pkginclude_HEADERS	= $(common_h) $(pro_headers)
EXTRA_HEADERS		= $(common_h) $(profile_h)
EXTRA_DIST		= $(profile_h)

install-exec-hook:
	(rm -f $(includedir)/mpi++.h ; \
	 if test ! -d $(pkgincludedir); then mkdir -p $(pkgincludedir); fi ; \
	 $(LN_S) $(pkgincludedir)/mpi++.h $(includedir)/mpi++.h)

uninstall-local:
	(rm -f $(includedir)/mpi++.h)
