#!/bin/sh
#
# $Id$
# $Source$
#
# Run this script to generate aclocal.m4, config.h.in, 
# Makefile.in's, and ./configure...
# 
# To specify extra flags to aclocal (include dirs for example),
# set ACLOCAL_FLAGS
#
echo "running aclocal $ACLOCAL_FLAGS ... "
aclocal $ACLOCAL_FLAGS
echo "running autoheader ... "
autoheader
echo "running automake --add-missing ... "
automake --add-missing
echo "running autoconf ... "
autoconf
echo "removing stale config.status and config.log"
rm -f config.status config.log
echo "now run ./configure to configure slurm for your environment."
