AUTOMAKE_OPTIONS = foreign

if HAVE_ELAN
elan_testprogs = runqsw
else
elan_testprogs =
endif

INCLUDES = 	-I$(top_srcdir)
LDADD =		$(top_builddir)/src/common/libcommon.la

check_PROGRAMS = \
	$(TESTS) \
	stream_server stream_client \
	slurm_protocol_message_client \
	slurm_protocol_message_server \
	$(elan_testprogs)

TESTS = \
	pack-test \
        log-test \
	bitstring-test 

