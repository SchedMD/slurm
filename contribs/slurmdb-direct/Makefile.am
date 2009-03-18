#
# Makefile for slurmdb-direct scripts

AUTOMAKE_OPTIONS = foreign

bin_SCRIPTS = moab_2_slurmdb
extra = config.slurmdb.pl

moab_2_slurmdb:

_perldir=$(exec_prefix)`perl -e 'use Config; $$T=$$Config{installsitearch}; $$P=$$Config{installprefix}; $$P1="$$P/local"; $$T =~ s/$$P1//; $$T =~ s/$$P//; print $$T;'`

install-binSCRIPTS: $(bin_SCRIPTS)
	@$(NORMAL_INSTALL)
	test -z "$(DESTDIR)$(bindir)" || $(MKDIR_P) "$(DESTDIR)$(bindir)"
	@list='$(bin_SCRIPTS)'; for p in $$list; do \
	   echo "sed 's%use lib .*%use lib qw(${_perldir});%' $(top_srcdir)/contribs/slurmdb-direct/$$p.pl > $(DESTDIR)$(bindir)/$$p"; \
	   sed "s%use lib .*%use lib qw(${_perldir});%" $(top_srcdir)/contribs/slurmdb-direct/$$p.pl >$(DESTDIR)$(bindir)/$$p; \
	   chmod 755 $(DESTDIR)$(bindir)/$$p;\
	done

	test -z "$(DESTDIR)${_perldir}" || $(MKDIR_P) "$(DESTDIR)${_perldir}"
	@list='$(extra)'; for p in $$list; do \
	   echo " cp $(top_srcdir)/contribs/slurmdb-direct/$$p $(DESTDIR)${_perldir}/$$p"; \
	   cp $(top_srcdir)/contribs/slurmdb-direct/$$p $(DESTDIR)${_perldir}/$$p; \
	done

uninstall-binSCRIPTS:
	@$(NORMAL_UNINSTALL)
	@list='$(bin_SCRIPTS)'; for p in $$list; do \
	  echo " rm -f '$(DESTDIR)$(bindir)/$$p'"; \
	  rm -f "$(DESTDIR)$(bindir)/$$p"; \
	done
	@list='$(extra)'; for p in $$list; do \
	  echo " rm -f '$(DESTDIR)$(_perldir)/$$p'"; \
	  rm -f "$(DESTDIR)$(_perldir)/$$p"; \
	done

clean:
