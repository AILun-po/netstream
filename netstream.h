#ifndef NETSTREAM_H
#define NETSTREAM_H

#include <pthread.h>
#include <netinet/in.h>

enum verbosity {QUIET = 0,
	ALERT = 1,
	CRIT = 2,
	ERR = 3,
	WARN = 4,
	NOTICE = 5,
	INFO = 6,
	DEBUG = 7};

struct cmd_args {
	char * cfg_file;
	char daemonize;
	enum verbosity verbosity;
	char testonly;
};

enum endpt_dir {DIR_INPUT,DIR_OUTPUT, DIR_INVAL};
enum endpt_type {T_SOCKET, T_FILE, T_STD, T_INVAL};
enum endpt_retry {NO=0,YES=1,IGNORE};

struct endpt_cfg {
	enum endpt_dir dir;
	enum endpt_type type;
	enum endpt_retry retry;
	char * name;
	char * port;
	int protocol;
	int keepalive;
	struct buffer * buf;
	int exit_status;

};

struct buffer {
	size_t nitems;
	size_t it_size; 
	char * buffer;
	ssize_t * datalens;
	int prod_pos;
	int cons_pos;
	ssize_t nlast_data;
	pthread_mutex_t lock;
	pthread_cond_t empty_cv;

};

struct io_cfg {
	int n_outs;
	struct endpt_cfg * outs;
	struct endpt_cfg * input;
};

#define READ_BUFFER_BLOCK_SIZE 32
#define WRITE_BUFFER_BLOCK_SIZE 32
#define WRITE_BUFFER_BLOCK_COUNT 8
#define MAX_OUTPUTS 100
#define RETRY_DELAY 1

int dprint(enum verbosity verb,const char * format, ...);

#endif
