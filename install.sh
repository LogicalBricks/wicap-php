#!/bin/sh

# careful, not really tested
# ... needs more testing

# Configuration
basedir=/var/www
target=usr/local/wicap-php
dest=${basedir}/${target}
rrdtool=/usr/local/bin/rrdtool

uid=$(/usr/bin/id -ur)
if [ ${uid} != 0 ]; then
	echo "-> Error, you do not have root permissions"
	exit 1
fi

echo "-> Compiling 'allow' daemon..."
( cd source
make clean all
) || exit 1

echo "-> Creating '$dest' structure"
mkdir -p ${dest}/{var,bin,htdocs} || exit 1

echo "-> Copying files to '$dest'"
cp source/allow ${dest}/bin
cp source/allow.rc ${dest}/bin
cp source/rotate_something.sh ${dest}/bin
cp source/rotate.cron ${dest}/bin
cp source/htdocs/*.php ${dest}/htdocs
cp source/htdocs/*.jpg ${dest}/htdocs

echo "-> Setting permissions to '$dest'"
chown -R www ${dest}/var
chmod 700 ${dest}/bin/*

#echo "-> Do you want me to create a Round-Robin-Database (RRD)"
#echo -n "-> for stats? (rrdtool must be installed) (y or n)? "
#read yn
#if [ $yn != "y" ];then
#	echo "Okay, I didn't do it"
#else
#	echo "Making RRD..."
#	./mkrrd.sh $rrdtool $dest/var/stats.rrd
#fi

( cd $dest/htdocs
  echo "-> Linking..."
  echo "   $dest/htdocs/wicap.php -> $dest/htdocs/index.php"
  ln -sf wicap.php index.php
  echo "-> Linking..."
  echo "   $dest -> /$target"
  ln -sf $dest /$target
) || exit 1

echo "-> Installing..."
echo "   source/conf/httpd-minimal.conf as ${basedir}/conf/httpd.conf"
if [ -f ${basedir}/conf/httpd.conf ]; then
	osha256=$(/bin/sha256 -q ${basedir}/conf/httpd.conf)
	nsha256=$(/bin/sha256 -q source/conf/httpd-minimal.conf)
	if [ "${osha256}" != "${nsha256}" ]; then
		echo "-> Error, ${basedir}/conf/httpd.conf differs from"
		echo "   source/conf/httpd-minimal.conf"
		echo "   Make a backup of ${basedir}/conf/httpd.conf, delete it"
		echo "   and execute once again this shell script"
		exit 1
	fi
fi
cat source/conf/httpd-minimal.conf > ${basedir}/conf/httpd.conf

echo "-> Done"
