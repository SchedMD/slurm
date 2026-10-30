AUTOMAKE_OPTIONS = foreign

SUBDIRS = bitstring \
	  hostlist \
	  slurm_protocol_defs \
	  slurm_protocol_pack \
	  slurmdb_defs \
	  slurmdb_pack

AM_CPPFLAGS = -I$(top_srcdir)
LDADD = -ldl -lpthread $(LIB_SLURM)

check_PROGRAMS = \
	$(TESTS)

TESTS = \
	log-test \
	dependency-test

if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@ -Wall
MYCFLAGS += -D_ISO99_SOURCE
TESTS += xhash-test \
	 buf-test \
	 persist_conn-test \
	 data-test \
	 dns-test \
	 http-test \
	 sluid-test \
	 xbase64-test \
	 xstring-test \
	 parse_time-test \
	 pack-test \
	 reverse_tree-test \
	 tpu-layout-test \
	 xahash-test

xhash_test_CFLAGS = $(MYCFLAGS)
xhash_test_LDADD  = $(LDADD) @CHECK_LIBS@
xahash_test_CFLAGS = $(MYCFLAGS)
xahash_test_LDADD  = $(LDADD) @CHECK_LIBS@
buf_test_CFLAGS   = $(MYCFLAGS)
buf_test_LDADD    = $(LDADD) @CHECK_LIBS@
persist_conn_test_CFLAGS = $(MYCFLAGS) \
	-DTEST_PLUGIN_DIR=\"$(abs_top_builddir)/src/plugins/url_parser/internal/.libs:$(abs_top_builddir)/src/plugins/auth/none/.libs:$(abs_top_builddir)/src/plugins/hash/k12/.libs:$(abs_top_builddir)/src/plugins/tls/none/.libs\"
persist_conn_test_LDADD  = $(LDADD) @CHECK_LIBS@
data_test_CFLAGS  = $(MYCFLAGS)
data_test_LDADD   = $(LDADD) @CHECK_LIBS@
dns_test_CFLAGS  = $(MYCFLAGS)
dns_test_LDADD   = $(LDADD) @CHECK_LIBS@
http_test_CFLAGS  = $(MYCFLAGS)
http_test_LDADD   = $(LDADD) @CHECK_LIBS@
sluid_test_CFLAGS = $(MYCFLAGS)
sluid_test_LDADD = $(LDADD) @CHECK_LIBS@
xbase64_test_CFLAGS = $(MYCFLAGS)
xbase64_test_LDADD = $(LDADD) @CHECK_LIBS@
xstring_test_CFLAGS   = $(MYCFLAGS)
xstring_test_LDADD    = $(LDADD) @CHECK_LIBS@
parse_time_test_CFLAGS= $(MYCFLAGS)
parse_time_test_LDADD = $(LDADD) @CHECK_LIBS@
pack_test_CFLAGS = $(MYCFLAGS)
pack_test_LDADD = $(LDADD) @CHECK_LIBS@
reverse_tree_test_CFLAGS = $(MYCFLAGS)
reverse_tree_test_LDADD = $(LDADD) @CHECK_LIBS@
tpu_layout_test_CFLAGS = $(MYCFLAGS)
tpu_layout_test_LDADD = $(LDADD) @CHECK_LIBS@

if HAVE_LUA

TESTS += lua-test

AM_CPPFLAGS += $(lua_CFLAGS)

lua_test_CFLAGS = $(MYCFLAGS) \
	-DLUA_TEST_SCRIPT=\"$(top_srcdir)/testsuite/slurm_unit/common/lua-test.lua\"
lua_test_LDADD = \
	$(LDADD) @CHECK_LIBS@ \
	$(top_builddir)/src/lua/libslurm_lua.la \
	$(lua_LIBS)

endif

endif
