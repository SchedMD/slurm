AUTOMAKE_OPTIONS = foreign

if HAVE_ELAN
elan_testprogs = runqsw
elan_lib = $(top_builddir)/src/plugins/switch/elan/switch_elan.la
else
elan_testprogs =
elan_lib =
endif

INCLUDES = 	-I$(top_srcdir)
LDADD =		$(top_builddir)/src/common/libcommon.la \
		$(elan_lib)

check_PROGRAMS = \
	$(TESTS) \
	$(elan_testprogs)

TESTS = \
	pack-test \
        log-test \
	bitstring-test 

