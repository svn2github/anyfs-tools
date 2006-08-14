#!/bin/bash

generateforshell()
{
	cat "$1" | awk '{gsub(/\$GT/, "gettext "); print; }' | 
		xgettext $2 -d anyfs-tools -L Shell /dev/stdin 2>/dev/null

	cat anyfs-tools.po | awk '/^#:/ { gsub(/\/dev\/stdin/, "'$1'"); print >"anyfs-tools.po"; next; }; {print >"anyfs-tools.po";}'
}

generateforC()
{
	cat "$1" | awk '{gsub(/_\(/, "gettext ("); print; }' | 
		xgettext $2 -d anyfs-tools -L C /dev/stdin 2>/dev/null

	cat anyfs-tools.po | awk '/^#:/ { gsub(/\/dev\/stdin/, "'$1'"); print >"anyfs-tools.po"; next; }; {print >"anyfs-tools.po";}'
}

generateforshell ../scripts/anyconvertfs

for i in `find .. -iname "*.c"`; do
	generateforC "$i" -j
done

mv anyfs-tools.po anyfs-tools.pot
