/**
 *  allow.c
 *
 * Copyright (c) 2005 Caleb Phillips <cphillips@smallwhitecube.com>
 * Licensed under the GNU GPL. For full terms see the file COPYING.
 *
 * purpose: a rewrite in 'C' of allow.pl which ended up being entirely
 * different than it's inspiration.
 *
 * lessons learned:
 *    - sleep() will sleep forever if you let it
 *    - there is a bug in pthreads where a blocking fopen() on a named pipe
 *      will cause *all* threads to block, not just the called
 *    - the userland equivalent of the timeout API is the event API
 *    - low level pipe() based IPC works best if you cowboy-up and 
 *      use read() and write() directly (especially if you want 
 *      to do fancy stuff, like O_NONBLOCKING)
 *    - message queues work even better than low level pipe()ing for IPC
 *
 * author: Caleb Phillips <cphillips@smallwhitecube.com>
 * version: 2005.07.21
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>

#include <sys/time.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include <event.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <netinet/in.h>  
#include <netinet/if_ether.h>
#include <arpa/inet.h>

// ---- STRUCTURES ----

struct lease_t{
        struct lease_t *next;
        char *ip;
        char *mac;
        int timestamp;
};

struct mac_t{
        struct mac_t *next;
        char *ip;
        char *mac;
};

struct message_t{
	long mtype;
	char mtext[80];
};

// ---- PROTOTYPES ----

void allow();
void evict();
void evict_callback(void *);
void logger(const int, const char *);
void update_pf(const char *func, const char *ip);
void update_rrd(int,int);
void usage();

// ...for dealing with leases
struct lease_t * find_lease_by_ip(struct lease_t *, char *);
struct lease_t * add_lease(char *);
void free_lease(struct lease_t *);
void free_leases(struct lease_t *);

// ...for dealing with macs
int get_macs(struct mac_t **);
void add_mac(struct mac_t **, char *, char *);
struct mac_t *find_mac_by_ip(struct mac_t *,char *);
void free_macs(struct mac_t *);

// ...for catching signals
void catch_int_evict(int);
void catch_int_allow(int);

// ---- CONSTANTS ---- 

const int log_error = 1;
const int log_warn = 2;
const int log_info = 3;
const int log_debug = 4;
const unsigned int buffersize = 80;
const char *version = "Wicap-PHP [version: delta] Copyright (c) 2005 Caleb Phillips <cphillips@smallwhitecube.com>";
const char *license = "This is free software: GNU GPL (http://www.gnu.org/licenses/gpl.txt)";

// ---- GLOBALS (yuck) ----

char *leasefile = "/var/www/usr/local/wicap-php/var/leases";		// location of FIFO
char *logfile = "/var/www/usr/local/wicap-php/var/allow.log.0";		// location of logfile
char *rrdfile = "/var/www/usr/local/wicap-php/var/stats.rrd";		// location of rrd

char *ftokfile = "/bin/ls";			// file to seed ftok
char *pfctl = "/sbin/pfctl";			// location of pfctl binary
char *rrdtool = "/usr/local/bin/rrdtool";	// location of rrdtool binary
char *wiallowtable = "wiallow";			// name of pf table to update
int maxleasetime = 24*60*60;			// in seconds, determins length of lease
int evictinterval = 5*60;			// in seconds, interval evict_callback is run
int current_log_level = 4;			// how verbose to be
int rrdflag = 0;				// whether we should collect stats
int idleevictflag = 1;                          // whether we should evict idle leases (using the arp table)

struct lease_t *leases = NULL;		// will become a linked list of leases
int leases_count = 0;			// number of nodes in the leases list
struct event evictevent;		// the event structure for evict()
struct timeval evicteventint;		// the interval for evict()
int pid;				// pid of the child (if we are the parent), 0 otherwise
int msqid;				// the message-queue id
key_t key;				// the message-queue key

// ---- FUNCTIONS ----

int main(int argc, char *argv[]){
	
	// option handling
	extern char *optarg;
     	extern int optind;
     	int ch, daemonflag, status;

	daemonflag = 1;
	rrdflag = 0;
     	while ((ch = getopt(argc, argv, "l:o:k:p:t:m:i:e:r:d:fnhs")) != -1) {
             	switch (ch) {
             	case 'l':
 			leasefile = optarg;            
			break;
		case 'o':
			if(strcmp(optarg,"-") == 0){
				logfile = NULL;
			}else{
				logfile = optarg;
			}
			break;
		case 'k':
			ftokfile = optarg;
			break;
		case 'p':
			pfctl = optarg;
			break;
		case 't':
			wiallowtable = optarg;
			break;
		case 'm':
			maxleasetime = atoi(optarg);
			break;
		case 'i':
			evictinterval = atoi(optarg);
			break;
		case 'e':
			current_log_level = atoi(optarg);
			break;
		case 'r':
			rrdtool = optarg;
			break;
		case 'd':
			rrdfile = optarg;
			break;
		case 's':
			rrdflag = 1;
			break;
                case 'n':
                        idleevictflag = 0;
                        break;
		case 'f':
			daemonflag = 0;
			break; 	
		case '?':
		case 'h':
             	default:
                     	usage();
             	}
     	}

        // capitalize on lazy evaluation; daemonize us if desired 
	if((daemonflag) && (daemon(0,0) != 0)){
                logger(log_error,"could not daemonize!");
        }

        // generate a (maybe) unique key
        if((key = ftok(ftokfile,'q')) == -1){
                logger(log_error,"ftok failed");
                return 1;
        }

        // create an everyone-rw message queue
        if((msqid = msgget(key,0666|IPC_CREAT)) == -1){
                logger(log_error,"msget failed");
                return 1;
        }

	// become two processes with seperate tasks
	status = 0;
	if((pid = fork()) == 0){
		// child code (evict)
		logger(log_info,"START: woohoo, I am the child");	

		argv[0] = "allow: [child]";

		signal(SIGINT,catch_int_evict);
		signal(SIGTERM,catch_int_evict);

		evict();

	}else{
		// parent code (allow)
		logger(log_info,"START: hooray, I am the parent");
		logger(log_info,version);
		logger(log_info,license);

		argv[0] = "allow: [parent]";

		// setup some signal handling
		signal(SIGINT,catch_int_allow);
		signal(SIGTERM,catch_int_allow);

		allow();
		wait(&status);

		// destroy the message queue
		msgctl(msqid, IPC_RMID, NULL);

	}

	return 0;
}

/**
 * catch_int_evict()
 *
 * if the child process (or whomever is running evict()) catches
 * a SIGTERM or SIGINT, this method is called which cleans up
 * allocated heapspace and exits. under normal circumstances,
 * the parent process will signal us to exit.
 */
void catch_int_evict(int signum){
	logger(log_info,"END: evict process (child) caught INT, shutting down...");
	logger(log_debug,"END: Freeing lease list");
	free_leases(leases);
	exit(0);		
}

/**
 * catch_int_allow()
 * 
 * if the parent process catches a SIGTERM or SIGINT, this method
 * is called, it destroys the message queue and kills
 * the child process.
 */
void catch_int_allow(int signum){
	logger(log_info,"END: allow process (parent) caught INT, shutting down...");
	logger(log_info,"END: Killing my child too...");
	kill(pid,SIGTERM);
	// destroy message queue
	logger(log_debug,"END: Destroying message queue");
	msgctl(msqid,IPC_RMID,NULL);
	exit(0);
}

/**
 * allow()
 * 
 * this function blocks on a FIFO pipe, and when
 * lines arrive it uses update_pf to add them to
 * a firewall table, then sends them to the message
 * queue so that our child can see them too...
 *
 * this function runs forever (unless there is a severe problem)
 */
void allow(){
	char buffer[buffersize];
	FILE *fh = NULL;
	struct lease_t *found = NULL;
	int len = 0;
	struct message_t newmessage;
	
	while(1){
	        // open the FIFO pipe
		logger(log_debug,"Waiting for a writer...");
                if((fh = fopen(leasefile,"r")) == NULL){
                        logger(log_error,"Cannot open leasefile: ");
                        logger(log_error,leasefile);
                        return;
                }
 		logger(log_debug,"Got a writer...");               
		
		// read lines from the FIFO pipe
		while(fgets(buffer,buffersize,fh) != NULL){
			// chomp off newline
			len = strlen(buffer);
			buffer[len-1] = '\0';

			// update the firewall
			update_pf("add",buffer);

			// send the line to our child via message-queue
			newmessage.mtype = 1;
			strlcpy(newmessage.mtext,buffer,sizeof(newmessage.mtext));
			msgsnd(msqid,&newmessage,sizeof(newmessage),0);
		}

		// close the fifo
		fclose(fh);
	}
}

/**
 * evict()
 *
 * this function sets up an event-timer
 * to periodically call evict_callback().
 * it never returns.
 */
void evict(){
	// init the event engine
        event_init();
        // set up the timer
        event_set(&evictevent,-1,EV_TIMEOUT,(void *)&evict_callback,NULL);
        // add an schedule
        evicteventint.tv_sec = evictinterval; 
        evicteventint.tv_usec = 0;
        evtimer_add(&evictevent,&evicteventint);

        //  hand over control to the event dispatcher
        event_dispatch();
}

/**
 * evict_callback()
 *
 * this function is called periodically by
 * the event scheduler. it reads in new messages
 * from the message queue, adds these addresses
 * to the linked list of leases (least_t). It gets
 * a linked-list of mac-address/ip pairs and
 * checks evicts leases if they a) have expired,
 * b) are not in the arp-table, or c) have changed mac addresses
 */
void evict_callback(void *dc){
	int evictcount, authcount, now = 0;
	struct message_t incoming;
	struct lease_t *found = NULL;
	struct lease_t *cur = NULL;
	struct lease_t *pre = NULL;
	struct mac_t *rootmac = NULL;
	struct mac_t *found2 = NULL;

	// check for new messages
	logger(log_debug,"Lets see if any new leases have arrived");

	while(msgrcv(msqid, &incoming, sizeof(incoming), 1, IPC_NOWAIT) != -1){
		logger(log_info,incoming.mtext);
		
	        // add it properly
                found = find_lease_by_ip(leases,incoming.mtext);
                if(found != NULL){
                	// update
                        logger(log_warn,"Reauth by unexpired lease");
                        found->timestamp = time(NULL);
                }else{
                        // add
                        found = add_lease(incoming.mtext);
			authcount++;
                }
	}

	if(leases_count == 0){
		if(rrdflag) update_rrd(0,0);
		evtimer_add(&evictevent,&evicteventint);
		logger(log_debug,"Nothing to do, going back to sleep");
		return;
	}
	 
	// get a list of mac addresses
	logger(log_debug,"building mac list");
	// use lazy eval to avoid calling get_macs if there
	// are no leases
	if(!get_macs(&rootmac)){
		logger(log_error,"Failed to get macs");
		return;
	}

	// evict naughty leases
	logger(log_debug,"Yawn, time to evict old leases");
	now = time(NULL);
	cur = leases;
	pre = leases;
	found2 = NULL;
	while(cur != NULL){
		// if the lease is expired
		if((now - cur->timestamp) > maxleasetime){
			update_pf("delete",cur->ip);
			if(pre == cur){
				leases = cur->next;
				free_lease(cur);
				cur = leases;
				pre = leases;
			}else{
				pre->next = cur->next;
				free_lease(cur);				
				cur = pre->next;
			}
			logger(log_info,"Evicted Expired lease");
			evictcount++;
			continue;
		}

	 	// arpless macs
		found2 = find_mac_by_ip(rootmac,cur->ip);
		if(found2 == NULL){
			// if evict-on-idle is disabled, don't evict
			update_pf("delete",cur->ip);
			if(pre == cur){
				leases = cur->next;
				free_lease(cur);
				cur = leases;
				pre = leases;
			}else{
				pre->next = cur->next;
				free_lease(cur);				
				cur = pre->next;
			}
			evictcount++;
			logger(log_info,"Evicted Idle lease");                       
			continue;
		}
		// first-time macs
		else if(strcmp(cur->mac,"MAC") == 0){
			// delete the temp str 
			free(cur->mac);
			cur->mac = NULL;

			// copy the real addr
		 	if((cur->mac = strdup(found2->mac)) == NULL){
				logger(log_error,"strdup failed");
				return;	
			}
			logger(log_debug,"Added MAC for first time");
			logger(log_debug,cur->mac);	
		}
		// mac spoofing (or other, unacceptable goofiness)
		else if(strcmp(cur->mac,found2->mac) != 0){
                        update_pf("delete",cur->ip);

 			logger(log_debug,found2->mac);
			logger(log_debug,cur->mac);

			if(pre == cur){
				leases = cur->next;
				free_lease(cur);
				cur = leases;
				pre = leases;
			}else{
				pre->next = cur->next;
				free_lease(cur);				
				cur = pre->next;
			}
			evictcount++;
			logger(log_info,"Evicted Hijacked Lease");
			continue;
		}

		logger(log_info,"This one is a keeper");	
		pre = cur;
		cur = cur->next;
	}

	logger(log_debug,"freeing mac list");
	free_macs(rootmac);
	rootmac = NULL;

	// update the round-robin database	
	if(rrdflag) update_rrd(authcount,evictcount);

	logger(log_debug,"scheduling next call to evict");
	evtimer_add(&evictevent,&evicteventint);
}

/**
 * free_lease()
 *
 * this function consolodates the
 * function of freeing a heap-allocated
 * lease structure. it is called directly
 * by evict_callback() and by free_leases().
 */
void free_lease(struct lease_t *cur){
	cur->next = NULL;
	if(cur->ip != NULL){
		free(cur->ip);
		cur->ip = NULL;
	}
	if(cur->mac != NULL){
		free(cur->mac);
		cur->mac = NULL;
	}
	leases_count--;
	free(cur);
}

/**
 * add_lease()
 *
 * this function is given an ip, which it uses
 * to create a new lease_t node on the leases linked
 * list, returning a pointer to the newly added node.
 */
struct lease_t * add_lease(char *ip){
	// allocate and populate a new lease
        struct lease_t *newlease;
        newlease = (struct lease_t *)malloc(sizeof(struct lease_t));
        if(newlease == NULL){
        	logger(log_error,"Malloc failure while allocating new lease");
                return;
        }
        newlease->timestamp = time(NULL);
        // todo, should lookup mac
        if((newlease->mac = strdup("MAC")) == NULL){
        	logger(log_error,"Malloc failure while allocating mac for new lease");
                return;
        }
        if((newlease->ip = strdup(ip)) == NULL){
        	logger(log_error,"Malloc failure while allocating ip for new lease");
                return;
        }

        // if it's the first
        if(leases == NULL){
        	newlease->next = NULL;
                leases = newlease;
        }else{
        	struct lease_t *save = leases;
                leases = newlease;
                newlease->next = save;
        }
	leases_count++;
	return newlease;
}

/**
 * find_lease_by_ip()
 *
 * does a linear search of the leases linked-list
 * and returns a pointer to the found lease struct,
 * or NULL if no matching lease is found.
 */
struct lease_t * find_lease_by_ip(struct lease_t *cur, char *ip){
	while(cur != NULL){
		if(strcmp(ip,cur->ip) == 0) return cur;
		cur = cur->next;
	}
	return NULL;
}

/**
 * logger()
 *
 * the grand unified logging function. prints
 * a datestamped line to the logfile, but
 * only if the current_log_level is greater than
 * or equal to the given level.
 */
void logger(const int level, const char *msg){
	FILE *logfh = NULL;
	int newmsgsize;
	time_t t;
	struct tm *ltime;
	char *newmsg;

	if(level > current_log_level) return;

	if(logfile != NULL){
		if((logfh = fopen(logfile,"a")) == NULL){
			fprintf(stderr,"couldnt open logfile!");
			exit(1);
		}
	}
	
	t = time(NULL);
	ltime = localtime(&t);
	
	// add space for "(YYYY.MM.DD HH:MM:SS) -> " and "\n"
	newmsgsize = strlen(msg) + 2 + 25;
	newmsg = (char *)malloc(newmsgsize);
	
	snprintf(newmsg,newmsgsize,"(%04d.%02d.%02d %02d:%02d:%02d) -> %s\n",
		ltime->tm_year+1900,
		ltime->tm_mon+1,
		ltime->tm_mday,
		ltime->tm_hour,
		ltime->tm_min,
		ltime->tm_sec,
		msg);

	if(logfile != NULL){
		fprintf(logfh,newmsg);
		fclose(logfh);
	}else{
		fprintf(stdout,newmsg);
	}

	free(newmsg);
	newmsg = NULL;
}

/**
 * update_pf()
 *
 * modifies the firewall by doing a fork and execve to pfctl
 * given an ip and a function like "add" or "delete"
 */
void update_pf(const char *func, const char *ip){
	int pid = -1;
	int status = 0;

	logger(log_debug,"In Update_PF");
	logger(log_debug,func);
	logger(log_debug,ip);

	// if we are now the child
	if(fork() == 0){
		char *cmd[] = {"pfctl", "-t", (char *)wiallowtable, "-T", (char *)func, (char *)ip, (char *)0};
		char *env[] = {(char *)0};
		if(execve(pfctl,cmd,env) != 0){
			logger(log_error,"Call to pfctl failed");
			exit(1);
		}
		exit(0);
	}else{
		// wait for our child to finish
		pid = wait(&status);
	}
}

void update_rrd(int newauthcount, int newevictcount){
	int pid = -1;
	int status = 0;
        char *cmd[] = {"rrdtool", "update", rrdfile, NULL, (char *)0 };
	char *env[] = {(char *)0};

	logger(log_debug,"In Update_RRD");
	logger(log_debug,rrdfile);


	if(fork() == 0){
		const int updatestrsize = 11 + 11 + 3;
		char updatestr[updatestrsize];
		snprintf(updatestr,updatestrsize,"N:%d:%d",newauthcount,newevictcount); 
		cmd[3] = updatestr;
		if(execve(rrdtool,cmd,env) != 0){
			logger(log_error,"Call to rrdtool failed");
			exit(1);
		} 	
		exit(0);
	}else{
		// wait for our child to finish
		pid = wait(&status);
	}
}

/**
 * get_macs()
 *
 * most of this function was gleaned from the source for 'arp': 
 * Copyright (c) 1984, 1993
 * The Regents of the University of California.  All rights reserved.
 *
 * this function reads the arp table from sysctl and stores it in
 * a linked list whose root node is given as an argument. returns 0
 * on failure, 1 on success.
 */
int get_macs(struct mac_t **dest){
	in_addr_t ipaddr;
	size_t needed;
	char *buf, *lim, *next, *scp, *ip;
	struct rt_msghdr *rtm;
        struct sockaddr_inarp *sin;
        struct sockaddr_dl *sdl;
        char mac[18];
	u_char *cp;

	logger(log_debug,"in get_macs()");

	int mib[6];
	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
        mib[2] = 0;
        mib[3] = AF_INET;
        mib[4] = NET_RT_FLAGS;
        mib[5] = RTF_LLINFO;
	
	if(sysctl(mib, 6, NULL, &needed, NULL, 0) < 0){
		logger(log_error,"Failed during sysctl size approx");
		return 0;
	}
	if(needed == 0){
		logger(log_error,"Sysctl was uninformative");
		return 0; 
	}
	if((buf = malloc(needed)) == NULL){
		logger(log_error,"Malloc failed");
		return 0;
	}
	if(sysctl(mib, 6, buf, &needed, NULL, 0) < 0){
		logger(log_error,"Sysctl failed");
		return 0;
	}	
	

	lim = buf + needed;
	next = buf;
	while(next < lim){
		rtm = (struct rt_msghdr *)next;
		sin = (struct sockaddr_inarp *)(rtm + 1);
		sdl = (struct sockaddr_dl *)(sin + 1);
	 	
		// extract ip
		ip = inet_ntoa(sin->sin_addr);

		// extract mac
		scp = LLADDR(sdl);
		cp = (u_char *)scp;
		snprintf(mac,18,"%02x:%02x:%02x:%02x:%02x:%02x",
			cp[0],cp[1],cp[2],cp[3],cp[4],cp[5]);

		// add it to the linked list
		add_mac(dest,ip,mac);
	
		next += rtm->rtm_msglen;	
	}
	free(buf);
	return 1;
}

/**
 * add_mac()
 *
 * adds a new mac_t struct to the macs linked list
 * (heap allocated)
 */
void add_mac(struct mac_t **root, char *ip, char *mac){
	struct mac_t *newmac = NULL;
	if((newmac = malloc(sizeof(struct mac_t))) == NULL){
		logger(log_error,"malloc failed");
	} 
	if((newmac->ip = strdup(ip)) == NULL){
		logger(log_error,"strdup failed");
	}
	if((newmac->mac = strdup(mac)) == NULL){
		logger(log_error,"strdup failed");
	}
	if(*root == NULL){
		// first node
		*root = newmac;
		newmac->next = NULL;
	}else{
		// put at the beginning
		struct mac_t *tmp = *root;
		*root = newmac;
		newmac->next = tmp;	
	}
}

/**
 * find_mac_by_ip()
 *
 * does a linear search of the mac_t linked list
 * returning a pointer to a matching node, or NULL
 * if one is not found.
 */
struct mac_t *find_mac_by_ip(struct mac_t *root, char *ip){
        struct mac_t *tmp = root;
        while(tmp != NULL){
		if(strcmp(ip,tmp->ip) == 0){
			return tmp;
		}
                tmp = tmp->next;
        }
	return NULL;
}

/**
 * free_macs()
 *
 * walks the mac_t linked list
 * and deallocates all of the nodes
 */
void free_macs(struct mac_t *rootmac){
	struct mac_t *tmp = NULL;
	while(rootmac != NULL){
		tmp = rootmac->next;
		if(rootmac->ip != NULL){
			free(rootmac->ip);
			rootmac->ip = NULL;
		}
		if(rootmac->mac != NULL){
			free(rootmac->mac);
			rootmac->mac = NULL;
		}
		rootmac->next = NULL;
		free(rootmac);
		rootmac = tmp;
	}
}

/**
 * free_leases()
 *
 * walks the leases linked list and
 * deallocates all of the nodes along
 * the way.
 */
void free_leases(struct lease_t *rootlease){
        struct lease_t *tmp = NULL;
        while(rootlease != NULL){
                tmp = rootlease->next;
		free_lease(rootlease);
                rootlease = tmp;
        }
}

/**
 * usage()
 *
 * called by main if there is a problem processing
 * command line arguments, or if the arg '-h' is given.
 * prints information about options and then exits in failure.
 */
void usage(){
        fprintf(stdout,"allow [options]\n");
        fprintf(stdout,"    -l  fifo leasefile\n");
        fprintf(stdout,"        default: /var/www/usr/local/wicap-php/var/leases\n");
        fprintf(stdout,"    -o  log file (use '-' for stdout)\n");
        fprintf(stdout,"        default: /var/www/usr/local/wicap-php/var/allow.log.0\n");
        fprintf(stdout,"    -k  ftok 'entropy' file\n");
        fprintf(stdout,"        default: /bin/ls\n");
        fprintf(stdout,"    -p  pfctl location\n");
        fprintf(stdout,"        default: /sbin/pfctl\n");
        fprintf(stdout,"    -t  pf wi-allow table\n");
        fprintf(stdout,"        default: wiallow\n");
        fprintf(stdout,"    -m  max lease time (in seconds)\n");
        fprintf(stdout,"        default: 24*60*60 = 24 hours\n");
        fprintf(stdout,"    -i  evict interval (in seconds)\n");
        fprintf(stdout,"        default: 5*60 = 5 minutes\n");
        fprintf(stdout,"    -e  log verbosity level (1-4)\n");
        fprintf(stdout,"        default: 4 (show everything)\n");
	fprintf(stdout,"    -r  rrdtool location\n");
	fprintf(stdout,"        default: /bin/rrdtool\n");
	fprintf(stdout,"    -d  rrd file location\n");
	fprintf(stdout,"        default: /var/www/usr/local/wicap-php/var/stats.rrd\n");
	fprintf(stdout,"    -s  turn on rrd statistics collection (notice -d and -r)\n");
        fprintf(stdout,"    -f  run in foreground\n");
        fprintf(stdout,"    -n  disable eviction on idle (arp based eviction)\n");
        fprintf(stdout,"    -h  print usage (you are here)\n");
        exit(1);
}
