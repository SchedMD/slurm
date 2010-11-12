#
# Makefile for slurmdb-direct scripts

AUTOMAKE_OPTIONS = foreign

sbin_SCRIPTS = moab_2_slurmdb
extra = config.slurmdb.pl

moab_2_slurmdb:

_perldir=$(exec_prefix)`perl -e 'use Config; $$T=$$Config{installsitearch}; $$P=$$Config{installprefix}; $$P1="$$P/local"; $$T =~ s/$$P1//; $$T =~ s/$$P//; print $$T;'`

install-sbinSCRIPTS: $(sbin_SCRIPTS)
	@$(NORMAL_INSTALL)
	test -z "$(DESTDIR)$(sbindir)" || $(mkdir_p) "$(DESTDIR)$(sbindir)"
	@list='$(sbin_SCRIPTS)'; for p in $$list; do \
	   echo "sed 's%use lib .*%use lib qw(${_perldir});%' $(top_srcdir)/contribs/slurmdb-direct/$$p.pl > $(DESTDIR)$(sbindir)/$$p"; \
	   sed "s%use lib .*%use lib qw(${_perldir});%" $(top_srcdir)/contribs/slurmdb-direct/$$p.pl >$(DESTDIR)$(sbindir)/$$p; \
	   chmod 755 $(DESTDIR)$(sbindir)/$$p;\
	done

	test -z "$(DESTDIR)${_perldir}" || $(mkdir_p) "$(DESTDIR)${_perldir}"
	@list='$(extra)'; for p in $$list; do \
	   echo " cp $(top_srcdir)/contribs/slurmdb-direct/$$p $(DESTDIR)${_perldir}/$$p"; \
	   cp $(top_srcdir)/contribs/slurmdb-direct/$$p $(DESTDIR)${_perldir}/$$p; \
	done

uninstall-sbinSCRIPTS:
	@$(NORMAL_UNINSTALL)
	@list='$(sbin_SCRIPTS)'; for p in $$list; do \
	  echo " rm -f '$(DESTDIR)$(sbindir)/$$p'"; \
	  rm -f "$(DESTDIR)$(sbindir)/$$p"; \
	done
	@list='$(extra)'; for p in $$list; do \
	  echo " rm -f '$(DESTDIR)$(_perldir)/$$p'"; \
	  rm -f "$(DESTDIR)$(_perldir)/$$p"; \
	done

clean:
