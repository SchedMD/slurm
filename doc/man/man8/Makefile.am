htmldir = ${datadir}/doc/${PACKAGE}-${SLURM_VERSION_STRING}/html

man8_MANS = \
	sackd.8 \
	slurmctld.8 \
	slurmd.8 \
	slurmdbd.8 \
	slurmrestd.8 \
	slurmstepd.8 \
	spank.8

if HAVE_MAN2HTML

generated_html = \
	sackd.html \
	slurmctld.html \
	slurmd.html \
	slurmdbd.html \
	slurmrestd.html \
	slurmstepd.html \
	spank.html

# Generated HTML man pages are kept out of html_DATA so that `make` (and
# `make install`) do not rebuild them. Use `make html` to build and
# `make install-html` to install.
html-local: ${generated_html}

install-html-local: ${generated_html}
	@$(NORMAL_INSTALL)
	test -z "$(htmldir)" || $(MKDIR_P) "$(DESTDIR)$(htmldir)"
	@list='$(generated_html)'; for f in $$list; do \
	  echo " $(INSTALL_DATA) $$f '$(DESTDIR)$(htmldir)'"; \
	  $(INSTALL_DATA) "$$f" "$(DESTDIR)$(htmldir)" || exit $$?; \
	done

uninstall-local:
	@$(NORMAL_UNINSTALL)
	@list='$(generated_html)'; for f in $$list; do \
	  echo " rm -f '$(DESTDIR)$(htmldir)/$$f'"; \
	  rm -f "$(DESTDIR)$(htmldir)/$$f"; \
	done

MOSTLYCLEANFILES = ${generated_html}

SUFFIXES = .html

.8.html:
	`dirname $<`/../man2html.py @SLURM_MAJOR@.@SLURM_MINOR@ $(srcdir)/../../html/header.txt $(srcdir)/../../html/footer.txt $<

endif
