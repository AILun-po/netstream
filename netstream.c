#define _XOPEN_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>

#include "netstream.h"
#include "buffer.h"
#include "conffile.h"
#include "endpts.h"


struct cmd_args args;
struct io_cfg config;

/* Debug print. If program verbosity is greater or equal to verb, printf-like format string is
 * printed to stderr.
 *
 * Returns result of vfprintf or 0 if verbosity is too small
 */
int dprint(enum verbosity verb,const char * format, ...){
	if (args.verbosity >= verb){
		va_list args;
		va_start(args,format);
		return vfprintf(stderr,format,args);
	}
	return 0;
}

/* Prints short usage */
void usage(char * name){
i	printf("Usage: %s [-c <config_file>] [-d] [-v [level]] [-t]\n",name);
}

/* Prints long usage help */
void help(void){
	printf("Available options:\n");
	printf("	-c <file>	- use <file> as a config (default netstream.conf)\n");
	printf("	-d		- run as a daemon\n");
	printf("	-v [level]	- set verbosity (0 quiet, 7 maximum)\n");
	printf("	-t		- only load config and test neigbours reachability\n");
}

/* Parse command line arguments into structure cfg.
 *
 * Returns 0 on success, -1 if unrecognized switch is found
 */
int parse_args(int argc, char ** argv, struct cmd_args * cfg){
	char * optstring="c:dv::t";
	int opt;

	cfg->cfg_file = "netstream.conf";
	cfg->verbosity = 3;
	cfg->daemonize = 0;
	cfg->testonly = 0;

	while((opt = getopt(argc,argv,optstring))!=-1){
		switch (opt){
			case 'c':
				cfg->cfg_file = optarg;
				break;
			case 'd':
				cfg->daemonize = 1;
				break;
			case 'v':	
				if (optarg==NULL){
					cfg->verbosity = 4;
				}else {
					cfg->verbosity = atoi(optarg);
					if (cfg->verbosity >7)
						cfg->verbosity = 7;
				}
				break;
			case 't':
				cfg->testonly = 1;
				break;
			default:
				dprint(ERR,"Unrecognized switch %c\n",optopt);
				return -1;
			}
	}
	return 0;
		
}

/* Print command line arguments from structure cfg to stderr. */
void print_args(struct cmd_args * cfg){
	fprintf(stderr,"Command line arguments:\n");
	fprintf(stderr,"	config file: %s\n",cfg->cfg_file);
	fprintf(stderr,"	daemonize:	%d\n",cfg->daemonize);
	fprintf(stderr,"	verbosity: %d\n",cfg->verbosity);
	fprintf(stderr,"	only test: %d\n",cfg->testonly);
}


int main(int argc, char ** argv){
	signal(SIGPIPE,SIG_IGN);
	if (parse_args(argc,argv,&args)==-1){
		usage(argv[0]);
		return 1;
	}
	if (args.verbosity >= DEBUG)
		print_args(&args);
	if (parse_config_file(&config,args.cfg_file)){
		dprint(CRIT,"Error while parsing config file\n");
		return 1;
	}
	if (args.verbosity >= DEBUG)
		print_config(&config);
	if (! check_config(&config)){
		dprint(CRIT,"Config check failed\n");
		return 1;
	}
	struct buffer * buffers;
	buffers = create_buffers(config.n_outs);
	if (buffers == NULL)
	{
		dprint(CRIT,"Error while initializing buffers\n");
		return 1;
	}
	for (int i=0;i<config.n_outs;i++){
		config.outs[i].buf = &buffers[i];	
	}
	pthread_t * threads;
	threads = malloc(sizeof(pthread_t)*config.n_outs);
	if (threads == NULL){
		dprint(CRIT,"Failed to allocate space for threads\n");
		return 1;
	}
	for (int i=0; i<config.n_outs;i++){
		int res;
		res = pthread_create(&threads[i],NULL,write_endpt,(void *)(&config.outs[i]));
		if (res){
			dprint(ERR,"Failed to start thread\n");
			return 1;
		}
	}
	pthread_t read_thr;
	int res;
	res = pthread_create(&read_thr,NULL,read_endpt,(void *)(&config));
	read_endpt(&config);
	
	// Not useful now, need to rewrite main
	/*
	for (int i=0;i<config.n_outs;i++){
		if (pthread_kill(threads[i],0)){
			void * retval;
			pthread_join(threads[i],&retval);
			if (config.outs[i].retry == YES){
				res = pthread_create(&threads[i],NULL,write_endpt,
						(void *)(&config.outs[i]));
				if (res){
					dprint(WARN,"Failed to restart thread\n");
					return 1;
				}
			
			} else if ((*((int *)retval) < 0 ) && 
					config.outs[i].retry == NO){
					dprint(WARN,"Thread failed, exitting\n");
					//TODO: Exit all, then return.
			}
		
		} 
	}
	*/

	//


	for (int i=0;i<config.n_outs;i++){
		int res;
		res = pthread_join(threads[i],NULL);
		if (res){
			dprint(ERR,"Failed to join thread\n");
			return 1;
		}
	}

	return 0;
}
