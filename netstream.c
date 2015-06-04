#define _XOPEN_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <yaml.h>
#include <netinet/in.h>
#include "netstream.h"

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
	config->n_endpts = nitems;
	config->endpts = malloc(sizeof(struct endpt_cfg)*nitems);
	if (config->endpts == NULL){
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
		printf("Could not load config file to YAML parser\n");
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

		endpt_config_init(&config->endpts[i]);
		
		for (yaml_node_pair_t * ep_par = ep_params->data.mapping.pairs.start;
			ep_par < ep_params->data.mapping.pairs.top; ep_par++){
			
			yaml_node_t * key;
			yaml_node_t * value;
			key = yaml_document_get_node(&document,ep_par->key);
			value = yaml_document_get_node(&document,ep_par->value);
			if (endpt_config_set_item(&config->endpts[i],
				(char *)key->data.scalar.value,
				(char *)value->data.scalar.value)==-1){
				printf("Error when parsing config\n");
				return -1;
			}
		}
		endpt++;

	}
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
	for (int i=0;i<cfg->n_endpts;i++){
		printf("Endpoint %d:\n",i);
		printf("	Direction: ");
		switch (cfg->endpts[i].dir){
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
		switch (cfg->endpts[i].type){
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
		switch (cfg->endpts[i].retry){
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
		printf("	Name: %s\n",cfg->endpts[i].name);
		printf("	Port: %d\n",cfg->endpts[i].port);
		printf("	Protocol: ");
		switch (cfg->endpts[i].protocol){
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
		printf("	Keepalive: %d\n",cfg->endpts[i].keepalive);
		printf("\n");		
		

	}
}
int check_config(struct io_cfg * config){
	// TODO: napsat
	return 1;
}

int main(int argc, char ** argv){
	if (parse_args(argc,argv,&args)==-1){
		usage(argv[0]);
		return 1;
	}
	print_args(&args);
	parse_config_file(&config,args.cfg_file);
	print_config(&config);

	return 0;
}
