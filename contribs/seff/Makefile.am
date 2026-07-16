# Makefile for seff and smail

AUTOMAKE_OPTIONS = foreign

bin_SCRIPTS = seff smail

seff:
smail:
_perldir := $(shell perl -MConfig -e 'if (($$ENV{SLURM_PERL_INSTALLDIRS} || "site") eq "vendor") { print $$Config{installvendorarch} } else { my $$T = $$Config{installsitearch}; my $$P = $$Config{siteprefix}; $$T =~ s|^\Q$$P\E||; print "$(prefix)$$T" }')

install-binSCRIPTS: $(bin_SCRIPTS)
	@$(NORMAL_INSTALL)
	test -z "$(DESTDIR)$(bindir)" || $(mkdir_p) "$(DESTDIR)$(bindir)"
	@list='$(bin_SCRIPTS)'; for p in $$list; do \
	   echo "sed 's%use lib .*%use lib qw(${_perldir});%; s|@bindir[@]|$(bindir)|g' ${abs_srcdir}/$$p > $(DESTDIR)$(bindir)/$$p"; \
	   sed "s%use lib .*%use lib qw(${_perldir});%; s|@bindir[@]|$(bindir)|g" ${abs_srcdir}/$$p >$(DESTDIR)$(bindir)/$$p; \
	   chmod 755 $(DESTDIR)$(bindir)/$$p;\
	done

uninstall-binSCRIPTS:
	@$(NORMAL_UNINSTALL)
	@list='$(bin_SCRIPTS)'; for p in $$list; do \
	  echo " rm -f '$(DESTDIR)$(bindir)/$$p'"; \
	  rm -f "$(DESTDIR)$(bindir)/$$p"; \
	done

clean:
