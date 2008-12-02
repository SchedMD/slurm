#!/bin/bash
# Clean up previous files
rm -f munge.key private.socket

# Create a random munge key
dd if=/dev/urandom bs=1 count=1024 >munge.key

# Start munged in the foreground
/usr/sbin/munged -F -f --socket=private.socket --key-file=munge.key
