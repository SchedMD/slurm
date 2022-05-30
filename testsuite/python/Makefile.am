install-data-hook:
	if [ ! -f $(srcdir)/pytest.ini ]; then \
		cp -p $(srcdir)/pytest.ini.sample $(srcdir)/pytest.ini; \
	fi

uninstall-hook:
	rm -f $(srcdir)/pytest.ini
