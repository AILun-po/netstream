#ifndef CONFFILE_H
#define	CONFFILE_H

#include "netstream.h"

void endpt_config_init(struct endpt_cfg * config);
int io_config_init(struct io_cfg * config, int nitems);
int endpt_config_set_item(struct endpt_cfg * config, char * key, char * value);
int parse_config_file(struct io_cfg * config, char * filename);
void print_config(struct io_cfg * cfg);
int check_config(struct io_cfg * config);


#endif
