#!/bin/sh

# careful, not really tested

# configuration 
basedir=/var/www
target=usr/local/wicap-php
dest=$basedir/$target
rrdtool=/usr/local/bin/rrdtool
# end configuration

echo "-> I am going to compile the daemon..."
cd source
make
cd ..

echo "-> I am going to install some files at $dest..."
echo -n "-> do you want to continue (y or n) ? "
read yn
if [ $yn != "y" ];then
        echo "-> okay, goodbye."
        exit;
fi

if [ ! -d $dest ];then
	echo "-> Making Directories..."
	mkdir -p $dest
	mkdir $dest/var
	mkdir $dest/bin
	mkdir $dest/htdocs

	echo "-> Copying Files..."
	cp source/allow $dest/bin/
	cp source/allow.rc $dest/bin/
	cp source/rotate_something.sh $dest/bin/
	cp source/rotate.cron $dest/bin/
	cp source/htdocs/*.php $dest/htdocs/
	cp source/htdocs/*.jpg $dest/htdocs/
		
	echo "-> Setting Permissions..."
	chown www $dest/var
	chmod 700 $dest/bin/allow
	chmod 700 $dest/bin/allow.rc
	chmod 700 $dest/bin/rotate.cron
	chmod 700 $dest/bin/rotate_something.sh

	echo "-> Do you want me to create a Round-Robin-Database (RRD)"
	echo -n "-> for stats? (rrdtool must be installed) (y or n)? "
	read yn
	if [ $yn != "y" ];then
		echo "Okay, I didn't do it"
	else
		echo "Making RRD..."
		./mkrrd.sh $rrdtool $dest/var/stats.rrd
	fi

	echo "-> Creating Symbolic-Links..."
	cd $dest/htdocs
	ln -s wicap.php index.php
	ln -s $dest /$target
else
	echo
	echo "-> Fatal: You have something at $dest..."
	echo "-> perhaps this means you are upgrading...in any case"
	echo "-> you should move/delete this directory first, and then"
	echo "-> rerun this script"

	exit 
fi

echo "-> Okay, All done!"
echo "-> You can use $dest/bin/allow.rc to start the daemon"
