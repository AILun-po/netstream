#define	_DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "netstream.h"
#include "buffer.h"
#include "conffile.h"
#include "endpts.h"


struct cmd_args cmd_args;
struct io_cfg config;
int * signal_fds;

/*
 * Debug print. If program verbosity is greater or equal to verb, printf-like
 * format string is printed to stderr.
 *
 * Returns result of vfprintf or 0 if verbosity is too small
 */
int dprint(enum verbosity verb, const char * format, ...) {
	if (cmd_args.verbosity >= verb) {
		va_list args;
		va_start(args, format);
		return (vfprintf(stderr, format, args));
	}
	return (0);
}

/*
 * Thread debug print. If program verbosity is greater or equal to verb,
 * printf-like format string is printed to stderr. Thread id is printed first.
 *
 * Returns result of vfprintf or 0 if verbosity is too small
 */
int tdprint(void * id, enum verbosity verb, const char * format, ...) {
	if (cmd_args.verbosity >= verb) {
		fprintf(stderr, "[Thread %p] ", id);
		va_list args;
		va_start(args, format);
		return (vfprintf(stderr, format, args));
	}
	return (0);
}


/* Prints short usage */
void usage(char * name) {
	printf("Usage: %s [-c < config_file>] [-d] [-v [level]] [-t]\n", name);
}

/* Prints long usage help */
void help(void) {
	printf("Available options:\n");
	printf(
"	-c < file>	- use < file> as a config (default netstream.conf)\n");
	printf(
"	-d		- run as a daemon\n");
	printf(
"	-v [level]	- set verbosity (0 quiet, 7 maximum)\n");
	printf(
"	-t		- only load config and test neigbours reachability\n");
}

/*
 * Handle a signal and add it to the signal queue
 *
 * Catches a signal and write its number to the signal pipe
 */

void handle_signal(int signum) {
	int8_t bytesig;
	bytesig = signum;
	// Only for suppress warning of unused result
	if (write(signal_fds[1], &bytesig, 1)) {
	}
}

/*
 * Parse command line arguments into structure cfg.
 *
 * Returns 0 on success, -1 if unrecognized switch is found
 */
int parse_args(int argc, char ** argv, struct cmd_args * cfg) {
	char * optstring = "c:dv::t";
	int opt;

	cfg->cfg_file = "netstream.conf";
	cfg->verbosity = 3;
	cfg->daemonize = 0;
	cfg->testonly = 0;

	while ((opt = getopt(argc, argv, optstring)) != -1) {
		switch (opt) {
			case 'c':
				cfg->cfg_file = optarg;
				break;
			case 'd':
				cfg->daemonize = 1;
				break;
			case 'v':
				if (optarg == NULL) {
					cfg->verbosity = 4;
				} else  {
					cfg->verbosity = atoi(optarg);
					if (cfg->verbosity > 7)
						cfg->verbosity = 7;
				}
				break;
			case 't':
				cfg->testonly = 1;
				break;
			default:
				dprint(ERR, "Unrecognized switch %c\n", optopt);
				return (-1);
			}
	}
	return (0);

}

/* Print command line arguments from structure cfg to stderr. */
void print_args(struct cmd_args * cfg) {
	fprintf(stderr, "Command line arguments:\n");
	fprintf(stderr, "	config file: %s\n", cfg->cfg_file);
	fprintf(stderr, "	daemonize:	%d\n", cfg->daemonize);
	fprintf(stderr, "	verbosity: %d\n", cfg->verbosity);
	fprintf(stderr, "	only test: %d\n", cfg->testonly);
}


int main(int argc, char ** argv) {
	if (parse_args(argc, argv, &cmd_args) == -1) {
		usage(argv[0]);
		return (1);
	}
	if (cmd_args.verbosity >= DEBUG)
		print_args(&cmd_args);
	if (parse_config_file(&config, cmd_args.cfg_file)) {
		dprint(CRIT, "Error while parsing config file\n");
		return (1);
	}
	if (cmd_args.verbosity >= DEBUG)
		print_config(&config);
	if (! check_config(&config)) {
		dprint(CRIT, "Config check failed\n");
		return (1);
	}

	// Listen to signals
	signal_fds = malloc(sizeof (int)*2);
	if (pipe(signal_fds) != 0) {
		dprint(CRIT, "Error when creating signal pipe\n");
		return (1);
	}
	if (fcntl(signal_fds[0], F_SETFL, O_NONBLOCK) != 0) {
		dprint(CRIT, "Error when creating signal pipe\n");
		return (1);
	}
	if (fcntl(signal_fds[1], F_SETFL, O_NONBLOCK) != 0) {
		dprint(CRIT, "Error when creating signal pipe\n");
		return (1);
	}

	struct sigaction act;
	memset(&act, 0, sizeof (struct sigaction));
	if (sigfillset(&act.sa_mask) == -1)
	{
		dprint(ERR, "Error in signal setup\n");
		return (1);
	}
	act.sa_handler = *(handle_signal);
	if (sigaction(SIGINT, &act, NULL) == -1) {
		dprint(CRIT, "Error when setting signal handler\n");
		return (1);
	}
	if (sigaction(SIGPIPE, &act, NULL) == -1) {
		dprint(CRIT, "Error when setting signal handler\n");
		return (1);
	}
	if (sigaction(SIGTERM, &act, NULL) == -1) {
		dprint(CRIT, "Error when setting signal handler\n");
		return (1);
	}

	for (int i = 0; i < config.n_outs; i++) {
		config.outs[i].test_only = !!cmd_args.testonly;
	}
	config.input->test_only = !!cmd_args.testonly;



	struct buffer * buffers;
	buffers = create_buffers(config.n_outs);
	if (buffers == NULL)
	{
		dprint(CRIT, "Error while initializing buffers\n");
		return (1);
	}
	for (int i = 0; i < config.n_outs; i++) {
		config.outs[i].buf = &buffers[i];
	}

	if (cmd_args.daemonize) {
		int res;
		res = daemon(1, 0);
		if (res == -1) {
			dprint(CRIT, "Daemonization failed");
			return (1);
		}
	}


	struct deadlist * dlist;
	dlist = malloc(sizeof (struct deadlist));
	if (dlist == NULL) {
		dprint(CRIT, "Failed to allocate space for threads\n");
		return (1);
	}
	pthread_mutex_init(&(dlist->mtx), NULL);
	pthread_cond_init(&(dlist->condv), NULL);
	dlist->cfg_list = calloc(sizeof (struct endpt_cfg *), config.n_outs+1);
	if (dlist->cfg_list == NULL) {
		dprint(CRIT, "Failed to allocate space for threads\n");
		return (1);
	}
	dlist->pos = 0;
	pthread_mutex_lock(&(dlist->mtx));

	pthread_t read_thr;
	int res;
	config.input->dlist = dlist;
	res = pthread_create(&read_thr, NULL, read_endpt, (void *)(&config));

	pthread_t * threads;
	threads = malloc(sizeof (pthread_t)*config.n_outs);
	if (threads == NULL) {
		dprint(CRIT, "Failed to allocate space for threads\n");
		return (1);
	}
	for (int i = 0; i < config.n_outs; i++) {
		int res;
		config.outs[i].dlist = dlist;
		res = pthread_create(&threads[i],
			NULL,
			write_endpt,
			(void *)(&config.outs[i]));
		if (res) {
			dprint(ERR, "Failed to start thread\n");
			return (1);
		}
	}

	int retval;
	retval = 0;

	while (dlist->pos < config.n_outs + 1) {
		pthread_cond_wait(&(dlist->condv), &(dlist->mtx));
		dprint(DEBUG, "Thread died\n");
		for (int i = 0; i < dlist->pos; i++) {
			if (dlist->cfg_list[i]->exit_status != 0) {
				dprint(WARN, "There was error in thread %p,"
					" cancelling other threads\n",
					dlist->cfg_list[i]);
				retval = 1;
				pthread_cancel(read_thr);
				for (int i = 0; i < config.n_outs; i++) {
					pthread_cancel(threads[i]);
				}
				break;


			}
		}
		if (retval == 1) {
			break;
		}
	}

	res = pthread_join(read_thr, NULL);
	if (res) {
		dprint(ERR, "Failed to join read thread:%s\n", strerror(res));
	}

	for (int i = 0; i < config.n_outs; i++) {
		res = pthread_join(threads[i], NULL);
		if (res) {
			retval = 1;
			dprint(ERR, "Failed to join write thread\n");
		}
		if (config.outs[i].exit_status != 0) {
			retval = 1;
		}
	}

	return (retval);
}
