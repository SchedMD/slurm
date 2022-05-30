AUTOMAKE_OPTIONS = foreign

install-data-hook:
	if [ ! -f $(srcdir)/globals.local ]; then \
		cp -p globals.local.sample $(srcdir)/globals.local; \
	fi

uninstall-hook:
	rm -f $(srcdir)/globals.local

distclean-local:
	rm -rf *error *output
