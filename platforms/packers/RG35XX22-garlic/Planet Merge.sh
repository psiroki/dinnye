#!/bin/sh

progdir=`dirname "$0"`/planetmerge
cd $progdir/
./planets > out.txt 2> err.txt
# End the previous line with this for debug info:
# > out.txt 2> err.txt
