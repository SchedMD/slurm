AUTOMAKE_OPTIONS = foreign

if WITH_ELAN
elan_testprogs = runqsw
else
elan_testprogs =
endif

noinst_PROGRAMS = $(elan_testprogs) slurm_protocol_stream_client slurm_protocol_stream_server 

LDADD = $(top_srcdir)/src/common/libcommon.la
