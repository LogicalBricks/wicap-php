#!/bin/sh

# usage: ./rotate_something.sh <file_to_rotate> <last kept (0 to what?)> <owned by> <archive?> <chmod>
# i.e. : ./rotate_something.sh /usr/local/spool/qmailscan/qmail-queue.log 6 root 0 666
#
# to setup something for rotation you need to move the file to <filename>.0
# and make a link from the original filename to the new one...i.e:
#
# mv cron.log cron.log.0
# ln -s cron.log.0 cron.log
# 
# use at your own risk
#
# author: Caleb Phillips
# version: 2004.03.11

WHAT=$1
LAST=$2
PERM=$3
ARCH=
EXTN=
SEXT=
CHMD=
if [ ! $4 == 1 ]; then 
	echo "We will not archive"
	ARCH=0
else
	echo "We will archive"
	ARCH=1
	EXTN=.tar.gz
fi

if [ ! $5 == "" ]; then
	echo "Using perm $5"
	CHMD=$5
else
	echo "Using default perm 744"
	CHMD=744
fi	

# remove the last
if [ -e "$WHAT.$LAST$EXTN" ]; then	
        echo rm -f $WHAT.$LAST$EXTN
        rm -f $WHAT.$LAST$EXTN
fi
                                                                                                                                                             
# rotate the perms
i=$LAST
while [ "$i" != "0" ]
do
     j=$(($i-1))
     if [ -e "$WHAT.$j$EXTN" ]; then
         echo mv $WHAT.$j$EXTN $WHAT.$i$EXTN
    	 mv $WHAT.$j$EXTN $WHAT.$i$EXTN
     fi
     i=$j
done

# if we are making an archive - make it,
# otherwise, create an empty file for 0
if [ $ARCH == 1 ]; then
	echo tar cvf $WHAT.0.tar --preserve $WHAT
	tar cf $WHAT.0.tar --preserve $WHAT
	echo gzip $WHAT.0.tar
	gzip $WHAT.0.tar
else
	echo touch $WHAT.0
	touch $WHAT.0
fi

echo chmod $CHMD $WHAT.0$EXTN
chmod $CHMD $WHAT.0$EXTN
echo chown $3:$3 $WHAT.0$EXTN
chown $3:$3 $WHAT.0$EXTN
