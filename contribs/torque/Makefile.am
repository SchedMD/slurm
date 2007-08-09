#
# Makefile for torque scripts

AUTOMAKE_OPTIONS = foreign

bin_SCRIPTS = pbsnodes qdel qhold qrls qstat qsub mpiexec

pbsnodes:
qdel:
qhold:
qrls:
qstat:
qsub:
mpiexec:

install-binSCRIPTS: $(bin_SCRIPTS)
	@$(NORMAL_INSTALL)
	test -z "$(bindir)" || $(MKDIR_P) "$(DESTDIR)$(bindir)"
	@list='$(bin_SCRIPTS)'; for p in $$list; do \
	   echo " $(INSTALL_PROGRAM_ENV) $(LIBTOOL) --mode=install $(binSCRIPT_INSTALL) '$(top_srcdir)/contribs/torque/$$p.pl' '$(DESTDIR)$(bindir)/$$p'"; \
	   $(INSTALL_PROGRAM_ENV) $(LIBTOOL) --mode=install $(binSCRIPT_INSTALL) "$(top_srcdir)/contribs/torque/$$p.pl" "$(DESTDIR)$(bindir)/$$p" || exit 1; \
	done

uninstall-binSCRIPTS:
	@$(NORMAL_UNINSTALL)
	@list='$(bin_SCRIPTS)'; for p in $$list; do \
	  echo " rm -f '$(DESTDIR)$(bindir)/$$p'"; \
	  rm -f "$(DESTDIR)$(bindir)/$$p"; \
	done

clean:
