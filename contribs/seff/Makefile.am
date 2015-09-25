# Makefile for seff and smail

AUTOMAKE_OPTIONS = foreign

bin_SCRIPTS = seff smail

# Don't rely on autoconf to replace variables outside of makefiles
smail: smail.in
	sed -e 's|@bindir[@]|$(bindir)|g' \
	    ${abs_srcdir}/smail.in >${abs_srcdir}/smail

install-binSCRIPTS: $(bin_SCRIPTS)
	@$(NORMAL_INSTALL)
	test -z "$(DESTDIR)$(bindir)" || $(mkdir_p) "$(DESTDIR)$(bindir)"
	@list='$(bin_SCRIPTS)'; for p in $$list; do \
	   cp $(top_srcdir)/contribs/seff/$$p $(DESTDIR)$(bindir)/$$p; \
	   chmod 755 $(DESTDIR)$(bindir)/$$p;\
	done

uninstall-binSCRIPTS:
	@$(NORMAL_UNINSTALL)
	@list='$(bin_SCRIPTS)'; for p in $$list; do \
	  echo " rm -f '$(DESTDIR)$(bindir)/$$p'"; \
	  rm -f "$(DESTDIR)$(bindir)/$$p"; \
	done

clean:
