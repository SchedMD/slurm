AUTOMAKE_OPTIONS = foreign

if WITH_ELAN
elan_testprogs = runqsw
else
elan_testprogs =
endif

noinst_PROGRAMS = $(elan_testprogs) pack-test log-test slurm_protocol_stream_client slurm_protocol_stream_server slurm_protocol_message_client slurm_protocol_message_server

TESTS = pack-test log-test slurm_protocol_stream_client slurm_protocol_stream_server slurm_protocol_message_client slurm_protocol_message_server

runqsw_LDADD      = $(top_srcdir)/src/common/libcommon.a 
pack_test_LDADD   = $(top_srcdir)/src/common/libcommon.a
log_test_LDADD    = $(top_srcdir)/src/common/libcommon.a
slurm_protocol_stream_client_LDADD    = $(top_srcdir)/src/common/libcommon.a
slurm_protocol_stream_server_LDADD    = $(top_srcdir)/src/common/libcommon.a
slurm_protocol_message_client_LDADD    = $(top_srcdir)/src/common/libcommon.a
slurm_protocol_message_server_LDADD    = $(top_srcdir)/src/common/libcommon.a
