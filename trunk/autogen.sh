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
if [ -e config.status ]; then
  echo "removing stale config.status."
  rm -f config.status 
fi
if [ -e config.log    ]; then
  echo "removing old config.log."
  rm -f config.log
fi
echo "now run ./configure to configure slurm for your environment."
