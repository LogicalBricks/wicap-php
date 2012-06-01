#!/bin/sh

# usage:
# ./mkrrd.sh [</path/to/rrdtool> </path/to/dest.rrd>]

# install rrdtool on openbsd 3.7 with:
# pkg_add -v ftp://ftp.openbsd.org/pub/OpenBSD/3.7/packages/i386/rrdtool-1.0.49.tgz

# excellent tutorial on rrdtool:
# http://people.ee.ethz.ch/~oetiker/webtools/rrdtool/tut/rrdtutorial.en.html

rrdtool=`which rrdtool`
rrdfile=/var/www/usr/local/wicap-php/var/stats.rrd

if [ ${1} ];then
	echo "-> Using ${1} for rrdtool path"
	rrdtool=${1}
fi
if [ ${2} ];then
	echo "-> Using ${2} for resulting rrd"
	rrdfile=${2}
fi

#
#
mkrrd (){
	${1} create ${2} \
		DS:auths:ABSOLUTE:600:U:U \
		DS:evicts:ABSOLUTE:600:U:U \
		RRA:AVERAGE:0.5:1:600 \
		RRA:AVERAGE:0.5:6:700 \
		RRA:AVERAGE:0.5:24:775 \
		RRA:AVERAGE:0.5:288:797 \
		RRA:MAX:0.5:1:600 \
		RRA:MAX:0.5:6:700 \
		RRA:MAX:0.5:24:775 \
		RRA:MAX:0.5:288:797
	if [ ${?} != 0 ]; then
		echo "\n-> Error "
		exit 1
	fi
}

mkrrd ${rrdtool} ${rrdfile}
