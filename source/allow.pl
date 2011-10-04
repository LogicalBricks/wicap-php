#!/usr/bin/perl -w

# allow.pl
#
# Copyright (c) 2005 Caleb Phillips <cphillips@smallwhitecube.com>
# Licensed under the GNU GPL. For full terms see the file COPYING.
#
# author: Caleb Phillips <cphillips@smallwhitecube.com>
# version: 2005.07.16
#
# purpose: a daemon that blocks on $leasefile which is a FIFO named pipe.
# when a line is ready, we interpret it as an affirmation to the disclaimer,
# and, if everything looks okay, run pfctl -NRf to update the pf rules.
# Also, leases expire after some amount of a) time or b) inactivity.
#
# todo: it wouldn't be too terribly difficult to rewrite this in 'C'...
#
# useful documentation on named-pipes and perl:
# http://www.unix.org.ua/orelly/perl/cookbook/ch16_12.htm

use strict;
use threads;
use threads::shared;

# maximum lease time in seconds
my $maxlease = 24*60*60;
# evict old leases every 5 minutes
my $evictint = 60*5;
# this is a FIFO named pipe
my $leasefile = "/var/www/usr/local/wicap-php/var/leases";
my $debug = 1; 
my $authcount = 1;
# the leases, a hash of (anonymous) hashes
# i.e: $leases{ip} = {mac=>mac,time=>time}
my %leases : shared = ();

# spawn the allow-thread
my $allow_thread = threads->new(\&allow);
# main thread becomes the evict-thread
evict();
# wait for our child to finish
$allow_thread->join();

# loops forever, blocking on the named pipe,
# and auth-ing ips as they arrive
sub allow{
	while(1){
		# open the leasefile (a pipe) for reading
		if(! -p $leasefile){
			die "Serious Dain Bramage, Lease file is not a FIFO pipe!";
		}
		open(FH,"< $leasefile")
			or die "Serious Dain Bramage, Cannot Open Lease file: $!";
	
		# this will block until there
        	# is something to read, then will read until EOF
		# store new auths
 		while(<FH>){
			# read in a line from the fifo
			$ip = $_;
			chomp $ip;

			if(!($ip =~ m/\d{1,}\.\d{1,}\.\d{1,}\.\d{1,}/)){
				print "Rejected $ip - bad format\n" if($debug);
				next;
			}
	
			# lookup the macaddr
			my $arpedmac = get_mac($ip);

			lock(%leases);

			# if it's not a duplicate
			if(!$leases{$ip}){
				update_pf("add",$ip);
				$leases{$ip} = {"mac" => $arpedmac, "time" => time()};
			}else{
				# if this is a reauth, update their timestamp
				print "ReAuth by $ip" if ($debug);
				$leases{$ip}{'time'} = time();
			}
		}
		close FH;
	}
}

# loops forever (polling), evicting rogue ips as we find them
sub evict{
	while(1){
		# sleep, right away
		sleep $evictint;

		# evict old and problematic leases
		my $now = time();
		lock(%leases);
		foreach my $ip (keys(%leases)){
	
			my $arpedmac = get_mac($ip);

			# evict expired leases
			if(($now - $leases{'time'}) > $maxlease){
				print "Rejected $ip - too old\n" if($debug);
				delete $leases{$ip};
				update_pf("delete",$ip);
			} 
			# evict on bad arps and bad macs
			elsif($arpedmac ne $leases{$ip}{'mac'}){
				# if we didn't find a mac
				if($arpedmac eq ''){
					print "Rejected $ip - no arp entry\n" if($debug);
					delete $leases{$ip};
					update_pf("delete",$ip);
				}
				# mismatch, mac spoofing or lucky dhcp...
				else{
					print "Rejected $ip - mac changed\n" if($debug);
					delete $leases{$ip};
					update_pf("delete",$ip);
				}
			}
			# don't evict
			else{
				print "Kept $ip\n" if($debug);
			}
		}
	}
}

# takes a function and an ip address, like update_pf('add','192.168.3.1'); and it
# works just like you would expect.
sub update_pf{
	my $func = shift;
	my $ip = shift;
	my $cmd = 'pfctl -t wiallow -T '.$func.' '.$ip;
	print "$cmd\n" if ($debug);
	print `$cmd`;
}

# sure it would be faster to do more than one
# at a time, but we will leave that for the next version.
# gets a mac address from an ip, using 'arp'
sub get_mac{
	my $ip = shift;
	
	# find the mac addr
	my $mac = `arp -n $ip`;
	chomp $mac;
	return "" if($mac eq "no");
	return $mac;
}
