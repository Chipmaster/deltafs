#!/bin/sh

if [ ! -d /var/lib/defs ]; then
	mkdir /var/lib/defs
fi
chgrp fuse /var/lib/defs
chmod 770 /var/lib/defs
cp defs /usr/local/bin
cp man/defs.1 /usr/local/share/man/man1
cp dln /usr/local/bin
cp man/dln.1 /usr/local/share/man/man1
