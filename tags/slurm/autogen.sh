#!/bin/sh
#
# $Id$
# $Source$
#
echo "running autoheader ... "
autoheader
echo "running automake --add-missing ... "
automake --add-missing
echo "running autoconf ... "
autoconf
echo "removing stale config.status and config.log"
rm -f config.status config.log
