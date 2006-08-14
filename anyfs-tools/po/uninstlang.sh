#!/bin/bash

INSTALLPATH="$1"
shift

while [ $1 ]; do
	LNG="$1"
	shift
	echo "rm -f \"$INSTALLPATH/share/locale/$LNG/LC_MESSAGES/anyfs-tools.mo\""
	rm -f "$INSTALLPATH/share/locale/$LNG/LC_MESSAGES/anyfs-tools.mo"
done

