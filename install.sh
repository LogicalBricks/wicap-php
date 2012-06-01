#!/bin/sh
#
# careful, not really tested          _
# ... needs more testing             (_)
#              |    .
#          .   |L  /|   .          _
#      _ . |\ _| \--+._/| .       (_)
#     / ||\| Y J  )   / |/| ./
#    J  |)'( |        ` F`.'/        _
#  -<|  F         __     .-<        (_)
#    | /       .-'. `.  /-. L___      _____                 ____   _____ _____
#    J \      <    \  | | O\|.-' _   / ___ \               |  _ \ / ____|  __ \
#  _J \  .-    \/ O | | \  |F   (_) / /  / /___  ___  ____ | |_) | (___ | |  | |
# '-F  -<_.     \   .-'  `-' L__   / /  / / __ \/ _ \/ __ \|  _ < \___ \| |  | |
#__J  _   _.     >-'  )._.   |-'  / /__/ / /_/ /  __/ / / /| |_) |____) | |__| |
#`-|.'   /_.           \_|   F    \_____/ .___/\___/_/ /_/ |____/|_____/|_____/
#  /.-   .                _.<    ======/ /======================================
# /'    /.'             .'  `\        /_/ Wicap Install
#  /L  /'   |/      _.-'-\
# /'J       ___.---'\|
#   |\  .--' V  | `. `
#   |/`. `-.     `._)
#      / .-.\
#      \ (  `\
# Configuration
basedir=/var/www
target=usr/local/wicap-php
dest=${basedir}/${target}
rrdtool=/usr/local/bin/rrdtool
source=$(dirname `readlink -f "$0"`)/source
# check permissions
#
check_permissions (){
	uid=$(/usr/bin/id -ur)
	if [ ${uid} != 0 ]; then
		echo "\n-> Error, you do not have root permissions"
		exit 1
	fi
}
check_permissions

# Compile deamon
#
make_daemon (){
	echo "-> Compiling 'allow' daemon..."
	cd ${source}
	make clean all
	if [ ${?} != 0 ]; then
		echo "\n-> Error while compiling the daemon"
		echo "   You should check you have all the dependencies."
		exit 1
	fi
}
make_daemon

# Create the following structure:
# /var/www/usr/local/wicap-php/
#                             ├── bin
#                             ├── htdocs
#                             └── var
create_structure (){
	echo "-> Creating '${dest}' structure"
	(mkdir -p ${dest}/var && mkdir -p ${dest}/bin && mkdir -p ${dest}/htdocs)
	if [ ${?} != 0 ]; then
		echo "\n-> Error while creating the structure"
		exit 1
	fi
}
create_structure

# copy files
#
copy_files (){
	echo "-> Copying files to '$dest'"
	( cp ${source}/allow ${dest}/bin && \
	cp ${source}/allow.rc ${dest}/bin && \
	cp ${source}/rotate_something.sh ${dest}/bin && \
	cp ${source}/rotate.cron ${dest}/bin && \
	cp ${source}/htdocs/*.php ${dest}/htdocs && \
	cp ${source}/htdocs/*.jpg ${dest}/htdocs )
	if [ ${?} != 0 ]; then
		echo "\n-> Error while Copying the files"
		exit 1
	fi
}
copy_files

#set permissions
#
set_permissions (){
	echo "-> Setting permissions to '$dest'"
	(chown -R www ${dest}/var && \
	chmod 700 ${dest}/bin/* )
	if [ ${?} != 0 ]; then
		echo "\n-> Error while Setting permissions to '$dest'"
		exit 1
	fi
}
set_permissions

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
echo "   ${source}/conf/httpd-minimal.conf as ${basedir}/conf/httpd.conf"
if [ -f ${basedir}/conf/httpd.conf ]; then
	osha256=$(/bin/sha256 -q ${basedir}/conf/httpd.conf)
	nsha256=$(/bin/sha256 -q ${source}/conf/httpd-minimal.conf)
	if [ "${osha256}" != "${nsha256}" ]; then
		echo "-> Error, ${basedir}/conf/httpd.conf differs from"
		echo "   ${source}/conf/httpd-minimal.conf"
		echo "   Make a backup of ${basedir}/conf/httpd.conf, delete it"
		echo "   and execute once again this shell script"
		exit 1
	fi
fi
cat ${source}/conf/httpd-minimal.conf > ${basedir}/conf/httpd.conf
echo "-> Done"
