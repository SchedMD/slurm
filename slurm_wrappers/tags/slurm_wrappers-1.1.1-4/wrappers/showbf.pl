#!/usr/bin/perl -w

#
# showbf - just mjstat
#
# Last Update: 2010-07-27
#
# Copyright (C) 2010 Lawrence Livermore National Security.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
# Written by Philip D. Eckert <eckert2@llnl.gov>
# CODE-OCEC-09-009. All rights reserved.
#

use strict;

#
# simply execute mjstat, with arguments.
#
exec("mjstat @ARGV");

exit;
