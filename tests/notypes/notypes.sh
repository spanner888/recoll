#!/bin/sh

topdir=`dirname $0`/..
. $topdir/shared.sh

initvariables $0

recollq Enerve_uniqueTerm          2> $mystderr | 
	egrep -v '^Recoll query: ' > $mystdout
recollq notype1_uniqueterm 2>> $mystderr | 
	egrep -v '^Recoll query: ' >> $mystdout

diff -w ${myname}.txt $mystdout > $mydiffs 2>&1

checkresult
