AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -ldl -lpthread
LDADD = $(top_builddir)/src/api/libslurm.o $(DL_LIBS)

check_PROGRAMS = \
	$(TESTS)

TESTS =

if HAVE_CHECK
MYCFLAGS  = @CHECK_CFLAGS@  #-Wall -ansi -pedantic -std=c99
#MYCFLAGS += -D_ISO99_SOURCE -Wunused-but-set-variable
TESTS += slurm_addto_char_list-test \
	 slurm_addto_id_char_list-test \
	 slurm_addto_mode_char_list-test \
	 slurm_addto_step_list-test \
	 xlate_array_task_str-test

slurm_addto_char_list_test_CFLAGS = $(MYCFLAGS)
slurm_addto_char_list_test_LDADD  = $(LDADD) @CHECK_LIBS@
slurm_addto_id_char_list_test_CFLAGS = $(MYCFLAGS)
slurm_addto_id_char_list_test_LDADD  = $(LDADD) @CHECK_LIBS@
slurm_addto_mode_char_list_test_CFLAGS = $(MYCFLAGS)
slurm_addto_mode_char_list_test_LDADD  = $(LDADD) @CHECK_LIBS@
slurm_addto_step_list_test_CFLAGS = $(MYCFLAGS)
slurm_addto_step_list_test_LDADD  = $(LDADD) @CHECK_LIBS@
xlate_array_task_str_test_CFLAGS = $(MYCFLAGS)
xlate_array_task_str_test_LDADD  = $(LDADD) @CHECK_LIBS@

endif
