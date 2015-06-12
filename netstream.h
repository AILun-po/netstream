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

// Command line arguments
struct cmd_args {
	char * cfg_file; 		// Path to config file
	char daemonize;			// Run as a daemon?
	enum verbosity verbosity;	// Verbosity
	char testonly;			// Only test connections and exit
};

enum endpt_dir {DIR_INPUT,DIR_OUTPUT, DIR_INVAL};	// Endpoint direction
enum endpt_type {T_SOCKET, T_FILE, T_STD, T_INVAL};	// Endpoint type
enum endpt_retry {NO=0,YES=1,IGNORE};			// Retry if read/write failed?


// Configuration of endpoint
struct endpt_cfg {
	enum endpt_dir dir;	// Direction (input/output)
	enum endpt_type type;	// Type (socket, file, std)
	enum endpt_retry retry;	// Retry if read/write failed?
	char * name;		// Filename/hostname
	char * port;		// Port (only for socket)
	int protocol;		// Protocol (TCP/UDP)
	int keepalive;		// Keepalive interval in sec (0 - default)
	struct buffer * buf;	// Write buffer (only for output)
	int exit_status;	// Does the reading/writing thread ended normally?
};

// Write buffer for each output
struct buffer {
	size_t nitems;		// Number of items
	size_t it_size; 	// Size of item
	char * buffer;		// Buffer
	ssize_t * datalens;	// Length of data in each item
	int prod_pos;		// Producer position in buffer (guarded by the lock)
	int cons_pos;		// Consumer position in buffer (gaurded by the lock)
	pthread_mutex_t lock;	// Lock 
	pthread_cond_t empty_cv;// Conditional variable for empty buffer

};

// Configuration of all endpoints
struct io_cfg {
	int n_outs;			// Number of outputs
	struct endpt_cfg * outs;	// Array of output configurations
	struct endpt_cfg * input;	// Pointer to input configuration
};

#define READ_BUFFER_BLOCK_SIZE 32
#define WRITE_BUFFER_BLOCK_SIZE 32
#define WRITE_BUFFER_BLOCK_COUNT 8
#define MAX_OUTPUTS 100			// Maximum number of outputs
#define RETRY_DELAY 1			// Delay between retrying to connect/open file 

// Like printf, but with verbosity level
int dprint(enum verbosity verb,const char * format, ...);

#endif
