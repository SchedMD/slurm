htmldir = ${datadir}/doc/${PACKAGE}-${SLURM_VERSION_STRING}/html

man1_MANS = \
	sacct.1 \
	sacctmgr.1 \
	salloc.1 \
	sattach.1 \
	sbatch.1 \
	sbcast.1 \
	scancel.1 \
	scontrol.1 \
	scrun.1 \
	scrontab.1 \
	sdiag.1	\
	sinfo.1   \
	slurm.1 \
	sprio.1 \
	squeue.1 \
	sreport.1 \
	srun.1 \
	sshare.1 \
	sstat.1 \
	strigger.1

if BUILD_HDF5
man1_MANS += sh5util.1
endif

if BUILD_SVIEW
man1_MANS += sview.1
endif

if HAVE_MAN2HTML

generated_html = \
	sacct.html \
	sacctmgr.html \
	salloc.html \
	sattach.html \
	sbatch.html \
	sbcast.html \
	scancel.html \
	scontrol.html \
	scrun.html \
	scrontab.html \
	sdiag.html \
	sinfo.html \
	sprio.html \
	squeue.html \
	sreport.html \
	srun.html \
	sshare.html \
	sstat.html \
	strigger.html

if BUILD_HDF5
generated_html += sh5util.html
endif

if BUILD_SVIEW
generated_html += sview.html
endif

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

.1.html:
	`dirname $<`/../man2html.py @SLURM_MAJOR@.@SLURM_MINOR@ $(srcdir)/../../html/header.txt $(srcdir)/../../html/footer.txt $<

endif
