AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -ldl -lpthread
LDADD = $(LIB_SLURM)

check_PROGRAMS = \
	$(TESTS)

TESTS =

if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@  #-Wall -ansi -pedantic -std=c99
#MYCFLAGS += -D_ISO99_SOURCE -Wunused-but-set-variable
TESTS += slurmdb_addto_qos_char_list-test

slurmdb_addto_qos_char_list_test_CFLAGS = $(MYCFLAGS)
slurmdb_addto_qos_char_list_test_LDADD = $(LDADD) @CHECK_LIBS@

endif
