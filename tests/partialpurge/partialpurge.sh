#!/bin/sh

topdir=`dirname $0`/..
. $topdir/shared.sh

initvariables $0

d=${tstdata}/partialpurge/

# Check that partial purge works: the message orphaned by shortening
# the mbox should not exist in the index any more
(
    cp $d/longmbox $d/testmbox
    recollindex -Zi $d/testmbox

    echo Should have 2 results: testmbox and longmbox:
    recollq -q deletedmessageuniqueterm
  
    echo
    echo Changing file and reindexing
    cp $d/shortmbox $d/testmbox
    recollindex -Zi $d/testmbox

    echo Should have 1 result: longmbox:
    recollq -q deletedmessageuniqueterm

    echo
    echo Purging whole test file
    recollindex -e $d/testmbox

    echo Should have 1 result: longmbox:
    recollq -q deletedmessageuniqueterm

    echo Should have 2 results: longmbox shortmbox:
    recollq -q stablemessageuniqueterm

) 2> $mystderr | egrep -v '^Recoll query: ' > $mystdout

diff -w ${myname}.txt $mystdout > $mydiffs 2>&1

checkresult
