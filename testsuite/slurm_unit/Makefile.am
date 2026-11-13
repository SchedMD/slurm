AUTOMAKE_OPTIONS = foreign

SUBDIRS = backfill common gres_shard topology

if WITH_GNU_LD
AM_CPPFLAGS = -I$(top_srcdir)
AM_CFLAGS = -Wall -ffunction-sections -fdata-sections
AM_LDFLAGS = -Wl,--gc-sections -Wl,-rpath=$(abs_top_builddir)/src/api/.libs
LDADD = -ldl -lpthread -L$(top_builddir)/src/api/.libs -lslurmfull

TESTS = agent_node_update-test health_check_cycle-test ping_nodes-test
check_PROGRAMS = $(TESTS)

agent_node_update_test_SOURCES = agent_node_update-test.c

health_check_cycle_test_SOURCES = health_check_cycle-test.c

ping_nodes_test_SOURCES = ping_nodes-test.c
endif
