AUTOMAKE_OPTIONS = foreign

if WITH_ELAN
elan_testprogs = runqsw
else
elan_testprogs =
endif

noinst_PROGRAMS = $(elan_testprogs)

TESTS =

runqsw_SOURCES = runqsw.c 
runqsw_LDADD = $(top_srcdir)/src/common/libcommon.a 
