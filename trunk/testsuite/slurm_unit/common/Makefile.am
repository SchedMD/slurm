AUTOMAKE_OPTIONS = foreign

if WITH_ELAN
elan_testprogs = runqsw
else
elan_testprogs =
endif

noinst_PROGRAMS = $(elan_testprogs) pack-test log-test stream_server stream_client bitstring-test slurm_protocol_message_client slurm_protocol_message_server

LDADD   = $(top_srcdir)/src/common/libcommon.la
