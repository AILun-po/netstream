#ifndef NETSTREAM_H
#define NETSTREAM_H

#include <netinet/in.h>

struct cmd_args {
	char * cfg_file;
	char daemonize;
	char verbosity;
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
	int port;
	int protocol;
	int keepalive;
};

struct io_cfg {
	int n_endpts;
	struct endpt_cfg * endpts;
};

#endif
