AUTOMAKE_OPTIONS = foreign

if WITH_ELAN
elan_testprogs = runqsw
else
elan_testprogs =
endif

noinst_PROGRAMS = $(elan_testprogs) pack-test log-test

TESTS = pack-test log-test

runqsw_LDADD      = $(top_srcdir)/src/common/libcommon.a 
pack_test_LDADD   = $(top_srcdir)/src/common/libcommon.a
log_test_LDADD    = $(top_srcdir)/src/common/libcommon.a
