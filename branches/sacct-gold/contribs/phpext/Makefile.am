AUTOMAKE_OPTIONS = foreign
php_dir = slurm_php
phpize = /usr/bin/phpize

all-local:
if HAVE_AIX
	@cd $(php_dir) && \
	if [ ! -f Makefile ]; then \
		if [ ! -f configure ]; then \
			$(phpize); \
		fi && \
		./configure; \
	fi && \
	$(MAKE) CC="$(CC)" CCFLAGS="-g -static $(CFLAGS)"; \
	cd ..;
else
	@cd $(php_dir) && \
	if [ ! -f Makefile ]; then \
		if [ ! -f configure ]; then \
			$(phpize); \
		fi && \
		./configure; \
	fi && \
	$(MAKE) CC="$(CC)" LD="$(CC) $(CFLAGS)" CCFLAGS="-g -static $(CFLAGS)"; \
	cd ..;
endif

install-exec-local:
	@cd $(php_dir) && \
	$(MAKE) DESTDIR=$(DESTDIR) install && \
	cd ..;

clean-generic:
	@cd $(php_dir); \
	$(phpize) --clean; \
	cd ..;

distclean-generic:
	@cd $(php_dir); \
	$(phpize) --clean; \
	cd ..;
