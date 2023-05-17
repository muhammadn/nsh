#!/bin/sh
#
# This script is called by NSH when it wants to permanently save the
# configuration to disk/flash.  This script can be modified by the user
# to do more if necessary.
#

if [ ! -f "$1" ]; then
  echo save.sh: not found: $1
  exit
fi

cp -p $1 /etc/nshrc
#cp -p /var/run/pf.conf /etc/pf.conf
sync
