#
# Makefile for cray scripts
#

AUTOMAKE_OPTIONS = foreign

EXTRA_DIST = \
	etc_init_d_munge		\
	etc_sysconfig_slurm		\
	libalps_test_programs.tar.gz	\
	munge_build_script.sh		\
	opt_modulefiles_slurm		\
	pam_job.c			\
	slurm-build-script.sh

srun:
_perldir=$(exec_prefix)`perl -e 'use Config; $$T=$$Config{installsitearch}; $$P=$$Config{installprefix}; $$P1="$$P/local"; $$T =~ s/$$P1//; $$T =~ s/$$P//; print $$T;'`

install-binSCRIPTS: $(bin_SCRIPTS)
	@$(NORMAL_INSTALL)
	test -z "$(DESTDIR)$(bindir)" || $(mkdir_p) "$(DESTDIR)$(bindir)"
	@list='$(bin_SCRIPTS)'; for p in $$list; do \
	   echo "sed 's%use lib .*%use lib qw(${_perldir});%' $(top_srcdir)/contribs/cray/$$p.pl | sed 's%BINDIR%@bindir@%' > $(DESTDIR)$(bindir)/$$p"; \
	         sed "s%use lib .*%use lib qw(${_perldir});%" $(top_srcdir)/contribs/cray/$$p.pl | sed "s%BINDIR%@bindir@%" > $(DESTDIR)$(bindir)/$$p; \
	   chmod 755 $(DESTDIR)$(bindir)/$$p;\
	done

uninstall-binSCRIPTS:
	@$(NORMAL_UNINSTALL)
	@list='$(bin_SCRIPTS)'; for p in $$list; do \
	  echo " rm -f '$(DESTDIR)$(bindir)/$$p'"; \
	  rm -f "$(DESTDIR)$(bindir)/$$p"; \
	done

clean:

