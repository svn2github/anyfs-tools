#!/bin/bash

INSTALLPATH="$1"
MSGFMT="msgfmt --statistics"
shift

while [ $1 ]; do
	LNG="$1"
	shift
	mkdir -p "$INSTALLPATH/share/locale/$LNG/LC_MESSAGES"
	echo "$MSGFMT -o \"$INSTALLPATH/share/locale/$LNG/LC_MESSAGES/anyfs-tools.mo\" $LNG.po"
	$MSGFMT -o "$INSTALLPATH/share/locale/$LNG/LC_MESSAGES/anyfs-tools.mo" $LNG.po
done

