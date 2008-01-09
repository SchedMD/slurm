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
	   echo "sed 's%use lib .*%use lib qw(${exec_prefix}/lib ${exec_prefix}/lib/perl5);%' $(top_srcdir)/contribs/torque/$$p.pl > $(DESTDIR)$(bindir)/$$p"; \
	   sed 's%use lib .*%use lib qw(@libdir@ @libdir@/perl5);%' $(top_srcdir)/contribs/torque/$$p.pl >$(DESTDIR)$(bindir)/$$p; \
	   chmod 755 $(DESTDIR)$(bindir)/$$p;\
	done

uninstall-binSCRIPTS:
	@$(NORMAL_UNINSTALL)
	@list='$(bin_SCRIPTS)'; for p in $$list; do \
	  echo " rm -f '$(DESTDIR)$(bindir)/$$p'"; \
	  rm -f "$(DESTDIR)$(bindir)/$$p"; \
	done

clean:
