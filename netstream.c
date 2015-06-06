#define _XOPEN_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <yaml.h>
#include <netinet/in.h>
#include "netstream.h"

#define READ_BUFFER_BLOCK_SIZE 32
#define WRITE_BUFFER_BLOCK_SIZE 32
#define WRITE_BUFFER_BLOCK_COUNT 8


struct cmd_args args;
struct io_cfg config;

void usage(char * name){
	printf("Usage: %s [-c <config_file>] [-d] [-v [level]] [-t]\n",name);
}

void help(void){
	printf("Available options:\n");
	printf("	-c <file>	- use <file> as a config (default netstream.conf)\n");
	printf("	-d		- run as a daemon\n");
	printf("	-v [level]	- set verbosity (0 quiet, 7 maximum)\n");
	printf("	-t		- only load config and test neigbours reachability\n");
}

int buffer_insert(struct buffer * buf,char * data){
	pthread_mutex_lock(&buf->lock);
	// Buffer is full, discard data 
	if ((buf->prod_pos+1)%buf->nitems == buf->cons_pos){
		pthread_mutex_unlock(&buf->lock);
		printf("Buffer %p overflow\n",buf);
		return -1;
	}
	memcpy(buf->buffer+buf->prod_pos*buf->it_size,
		data,buf->it_size);
	// Buffer was empty, signal a condition variable
	if ((buf->cons_pos+1)%buf->nitems == buf->prod_pos){
		pthread_cond_broadcast(&buf->empty_cv);
	}
	buf->prod_pos = (buf->prod_pos+1)%buf->nitems;
	pthread_mutex_unlock(&buf->lock);
	return 0;
}

char * buffer_cons_data_pointer(struct buffer * buf){
	pthread_mutex_lock(&buf->lock);
	char * result;
	result = buf->buffer+buf->cons_pos*buf->it_size;
	pthread_mutex_unlock(&buf->lock);	
	return result;

}

int buffer_after_delete(struct buffer * buf){
	int result;
	result = 0;
	pthread_mutex_lock(&buf->lock);
	// Buffer is empty, wait until is filled
	if ((buf->cons_pos+1)%buf->nitems == buf->prod_pos){
		pthread_cond_wait(&buf->empty_cv,&buf->lock);
		result = -1;
	}
	buf->cons_pos = (buf->cons_pos+1)%buf->nitems;
	pthread_mutex_unlock(&buf->lock);
	return result;

}

void endpt_config_init(struct endpt_cfg * config){
	config->dir = DIR_INVAL;
	config->type = T_INVAL;
	config->retry = NO;
	config->name = NULL;
	config->port = -1;
	config->protocol = -1;
	config->keepalive = 0; 
}

int io_config_init(struct io_cfg * config,int nitems){
	config->n_outs = nitems;
	config->outs = malloc(sizeof(struct endpt_cfg)*nitems);
	config->input = malloc(sizeof(struct endpt_cfg));
	if (config->outs == NULL || (config->input == NULL)){
		printf("Failed to allocate memory in %s\n",__FUNCTION__);
		free(config);
		return -1;
	}
	return 0;
} 

int endpt_config_set_item(struct endpt_cfg * config, char * key, char * value){
	// Direction
	if (strcmp(key,"Direction")==0){	
		if(strcmp(value,"input")==0){
			config->dir = DIR_INPUT;
		}else if(strcmp(value,"output")==0){
			config->dir = DIR_OUTPUT;
		}else {
			printf("Invalid value \"%s\" for key \"%s\"\n",value,key);
			return -1;
		}
	//Type
	}else if(strcmp(key,"Type")==0){
		if(strcmp(value,"socket")==0){
			config->type = T_SOCKET;
		}else if(strcmp(value,"file")==0){
			config->type = T_FILE;
		}else if(strcmp(value,"std")==0){
			config->type = T_STD;
		}else {
			printf("Invalid value \"%s\" for key \"%s\"\n",value,key);
			return -1;
		}
	// Retry
	}else if(strcmp(key,"Retry")==0){
		if(strcmp(value,"yes")==0){
			config->retry = YES;
		}else if(strcmp(key,"no")==0){
			config->retry = NO;
		}else if(strcmp(key,"ignore")==0){
			config->retry = IGNORE;
		}else {
			printf("Invalid value \"%s\" for key \"%s\"\n",value,key);
			return -1;
		}
	// Name
	}else if(strcmp(key,"Name")==0){
		size_t len = strlen(value);
		char * name;
		name = malloc(sizeof(char)*len);
		strcpy(name,value);
		config->name = name;
	// Port
	}else if(strcmp(key,"Port")==0){
		int port = strtol(value,NULL,10);
		if (port > 65535 || port <= 0){
			printf("Port number %d out of range\n",port);
			return -1;
		}
		config->port = port;
	// Protocol
	}else if(strcmp(key,"Protocol")==0){
		if(strcmp(value,"TCP")==0){
			config->protocol = IPPROTO_TCP;
		}else if(strcmp(key,"UDP")==0){
			config->protocol = IPPROTO_UDP;
		}else {
			printf("Invalid value \"%s\" for key \"%s\"\n",value,key);
			return -1;
		}
	// Keepalive
	}else if(strcmp(key,"Keepalive")==0){
		int keepalive = strtol(value,NULL,10);
		if (keepalive < 0){
			printf("Keepalive couldn't be negative \n");
			return -1;
		}
		config->keepalive = keepalive;
		
	} else {
		printf("Invalid key \"%s\"\n",key);
		return -1;
	}
	return 0;
}

int parse_args(int argc, char ** argv, struct cmd_args * cfg){
	char * optstring="c:dv::t";
	int opt;

	cfg->cfg_file = "netstream.conf";
	cfg->verbosity = 2;
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
					cfg->verbosity = 3;
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
				printf("Unrecognized switch %c\n",optopt);
				return -1;
			}
	}
	return 0;
		
}

struct endpt_cfg * get_read_endpt(struct io_cfg * cfg){
	for (int i=0;i<cfg->n_outs;i++){
		if (cfg->outs[i].dir == DIR_INPUT)
			return &cfg->outs[i];
	}
	return NULL;
	
}

// TODO dealokace pri nepovedenem cteni konfigurace
int parse_config_file(struct io_cfg * config,char * filename){
	yaml_parser_t parser;
	yaml_document_t document;
	
	FILE * cfg_file;
	cfg_file = fopen(filename,"r");
	if (cfg_file == NULL){
		printf("Could not open config file \"%s\"\n",filename);
		return -1;
	}

	yaml_parser_initialize(&parser);
	yaml_parser_set_input_file(&parser,cfg_file);
	
	if (!yaml_parser_load(&parser,&document)){
		printf("Could not load config file to YAML parser (probably syntax error)\n");
		return -1;
	}
	fclose(cfg_file);

	yaml_node_t * root;
	root = yaml_document_get_root_node(&document);
	if (root->type != YAML_SEQUENCE_NODE){
		printf("Wrong type of YAML root node (must be sequence)\n");
		return -1;
	}
	
	size_t items;
	items = (root->data.sequence.items.top - 
		root->data.sequence.items.start);
	if (io_config_init(config,items)==-1){
		printf("Error while initializing config structure\n");
		return -1;
	}
	yaml_node_item_t * endpt = root->data.sequence.items.start;
	
	for (int i=0; i<items; i++){
		yaml_node_t * ep_params;
		ep_params = yaml_document_get_node(&document,*endpt);
		if (ep_params->type != YAML_MAPPING_NODE){
			printf("Wrong type of YAML sequence node (must be mapping)\n");
			return -1;
		}

		endpt_config_init(&config->outs[i]);
		
		for (yaml_node_pair_t * ep_par = ep_params->data.mapping.pairs.start;
			ep_par < ep_params->data.mapping.pairs.top; ep_par++){
			
			yaml_node_t * key;
			yaml_node_t * value;
			key = yaml_document_get_node(&document,ep_par->key);
			value = yaml_document_get_node(&document,ep_par->value);
			if (endpt_config_set_item(&config->outs[i],
				(char *)key->data.scalar.value,
				(char *)value->data.scalar.value)==-1){
				printf("Error when parsing config\n");
				return -1;
			}
		}
		endpt++;

	}
	
	struct endpt_cfg * read_endpt;
	read_endpt = get_read_endpt(config);
	if (read_endpt == NULL){
		printf("No input defined\n");
		return -1;
	}
	*(config->input) = *(read_endpt);
	for (int i=0;i<config->n_outs-1;i++){
		if (config->outs+i >= read_endpt){	
			config->outs[i] = config->outs[i+1];
		}
	}
	config->n_outs--;
	
	return 0;
	

}

void print_args(struct cmd_args * cfg){
	printf("Command line arguments:\n");
	printf("	config file: %s\n",cfg->cfg_file);
	printf("	daemonize:	%d\n",cfg->daemonize);
	printf("	verbosity: %d\n",cfg->verbosity);
	printf("	only test: %d\n",cfg->testonly);
}

void print_config(struct io_cfg * cfg){
	printf("Config:\n");
	for (int i=0;i<cfg->n_outs;i++){
		printf("Output %d:\n",i);
		printf("	Direction: ");
		switch (cfg->outs[i].dir){
			case DIR_INPUT:
				printf("input\n");
				break;
			case DIR_OUTPUT:
				printf("output\n");
				break;
			case DIR_INVAL:
				printf("-\n");
				break;
		}
		printf("	Type: ");
		switch (cfg->outs[i].type){
			case T_SOCKET:
				printf("socket\n");
				break;
			case T_FILE:
				printf("file\n");
				break;
			case T_STD:
				printf("stdin/stdout\n");
				break;
			case T_INVAL:
				printf("-\n");
				break;
		}
		printf("	Retry: ");
		switch (cfg->outs[i].retry){
			case YES:
				printf("yes\n");
				break;
			case NO:
				printf("no\n");
				break;
			case IGNORE:
				printf("ignore\n");
				break;
		}
		printf("	Name: %s\n",cfg->outs[i].name);
		printf("	Port: %d\n",cfg->outs[i].port);
		printf("	Protocol: ");
		switch (cfg->outs[i].protocol){
			case IPPROTO_TCP:
				printf("TCP\n");
				break;
			case IPPROTO_UDP:
				printf("UDP\n");
				break;
			case -1:
				printf("-\n");
				break;
		}
		printf("	Keepalive: %d\n",cfg->outs[i].keepalive);
		printf("\n");		
		

	}
	printf("Input:\n");
	printf("	Direction: ");
	switch (cfg->input->dir){
		case DIR_INPUT:
			printf("input\n");
			break;
		case DIR_OUTPUT:
			printf("output\n");
			break;
		case DIR_INVAL:
			printf("-\n");
			break;
	}
	printf("	Type: ");
	switch (cfg->input->type){
		case T_SOCKET:
			printf("socket\n");
			break;
		case T_FILE:
			printf("file\n");
			break;
		case T_STD:
			printf("stdin/stdout\n");
			break;
		case T_INVAL:
			printf("-\n");
			break;
	}
	printf("	Retry: ");
	switch (cfg->input->retry){
		case YES:
			printf("yes\n");
			break;
		case NO:
			printf("no\n");
			break;
		case IGNORE:
			printf("ignore\n");
			break;
	}
	printf("	Name: %s\n",cfg->input->name);
	printf("	Port: %d\n",cfg->input->port);
	printf("	Protocol: ");
	switch (cfg->input->protocol){
		case IPPROTO_TCP:
			printf("TCP\n");
			break;
		case IPPROTO_UDP:
			printf("UDP\n");
			break;
		case -1:
			printf("-\n");
			break;
	}
	printf("	Keepalive: %d\n",cfg->input->keepalive);
	printf("\n");		
}
int check_config(struct io_cfg * config){
	// TODO: napsat
	return 1;
}


int read_endpt(struct io_cfg * cfg){
	int readfd;
	struct endpt_cfg * read_cfg;
	read_cfg = cfg->input;
	if (read_cfg->type == T_FILE){
		printf("File\n");
		readfd = open(read_cfg->name,O_RDONLY);
		if (readfd==-1){
			err(1,"Error while opening %s for reading",read_cfg->name);	
		}
	}else if (read_cfg->type == T_SOCKET){
	
	}else if (read_cfg->type == T_STD){
		printf("Stdin\n");
		readfd=0;
	}

	char * readbuf;
	readbuf = malloc(sizeof(char)*READ_BUFFER_BLOCK_SIZE);
	while (1){
		size_t toread;
		size_t nread;
		toread = READ_BUFFER_BLOCK_SIZE;
		nread = 0;
		while (nread < toread){
			ssize_t res;
			res = read(readfd,(void *)(readbuf+nread),(toread-nread));
			if (res == 0){ // EOF
				close(readfd);
				free(readbuf);
				return 0;
			}else if (res == -1){ //Error
				warn("Error while reading from %s",read_cfg->name);
				close(readfd);
				free(readbuf);
				return -1;
			}
			nread += res;
		}
		for (int i=0;i<cfg->n_outs;i++){

			buffer_insert(cfg->outs[i].buf,readbuf);
		}
	}
	// Should be unreachable
	return -2;
} 

void * write_endpt(void * args){
	printf("Thread %p started\n",args);
	struct endpt_cfg * cfg;
	cfg = (struct endpt_cfg *)args;
	int writefd;
	if (cfg->type == T_FILE){
		writefd = open(cfg->name, O_WRONLY | O_CREAT | O_TRUNC,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (writefd == -1){
			warn("Opening file %s to write failed\n",cfg->name);
			return NULL;
		}
	
	} else if (cfg->type == T_SOCKET){
	
	} else if (cfg->type == T_STD){
		writefd = 1;	
	}

	buffer_after_delete(cfg->buf);
	char * writebuf;
	while (1) {
		writebuf = buffer_cons_data_pointer(cfg->buf);
		size_t towrite;
		int nwritten;

		towrite = cfg->buf->it_size;
		nwritten = 0;
		while (nwritten < towrite){
			ssize_t res;
			res = write(writefd,(void *)(writebuf+nwritten),towrite-nwritten);
		 	if (res < 0){
				warn("Error while writing buffer:");
				// TODO: exit cycle, handle repeating
			}
			nwritten += res;
		}
		buffer_after_delete(cfg->buf);
	}

	return NULL;
}

struct buffer * create_buffers(int nbuffers){
	struct buffer * buffers;
	buffers = malloc(sizeof(struct buffer)*nbuffers);
	if (buffers == NULL){
		printf("Can't allocate memory for buffers\n");
		return NULL;
	}
	for (int i=0; i<nbuffers;i++){
		char * buffer;
		buffer = malloc(sizeof(char)*WRITE_BUFFER_BLOCK_SIZE*WRITE_BUFFER_BLOCK_COUNT);
		if (buffer == NULL){
			printf("Can't allocate memory for buffers\n");
			while (i>=0)
				free(buffers[i].buffer);
			return NULL;
		}
		buffers[i].buffer = buffer;
		buffers[i].nitems = WRITE_BUFFER_BLOCK_COUNT;
		buffers[i].it_size = WRITE_BUFFER_BLOCK_SIZE;
		buffers[i].prod_pos = 0;
		buffers[i].cons_pos = buffers[i].nitems-1;
		if (pthread_mutex_init(&buffers[i].lock,NULL)){
			printf("Error in mutex initialization\n");
			return NULL;
		}
		if (pthread_cond_init(&buffers[i].empty_cv,NULL)){
			printf("Error in conditional variable initialization\n");
			return NULL;
		}
	}	

	return buffers;
}


int main(int argc, char ** argv){
	if (parse_args(argc,argv,&args)==-1){
		usage(argv[0]);
		return 1;
	}
	print_args(&args);
	if (parse_config_file(&config,args.cfg_file)){
		printf("Error while parsing config file\n");
		return 1;
	}
	print_config(&config);
	if (! check_config(&config)){
		printf("Config check failed\n");
		return 1;
	}
	struct buffer * buffers;
	buffers = create_buffers(config.n_outs);
	if (buffers == NULL)
	{
		printf("Error while initializing buffers\n");
		return 1;
	}
	for (int i=0;i<config.n_outs;i++){
		config.outs[i].buf = &buffers[i];	
	}
	pthread_t * threads;
	threads = malloc(sizeof(pthread_t)*config.n_outs);
	if (threads == NULL){
		printf("Failed to allocate space for threads\n");
		return 1;
	}
	for (int i=0; i<config.n_outs;i++){
		int res;
		res = pthread_create(&threads[i],NULL,write_endpt,(void *)(&config.outs[i]));
		if (res){
			printf("Failed to start thread\n");
			return 1;
		}
	}
	read_endpt(&config);

	for (int i=0;i<config.n_outs;i++){
		int res;
		res = pthread_join(threads[i],NULL);
		if (res){
			printf("Failed to join thread\n");
			return 1;
		}
	}

	return 0;
}
