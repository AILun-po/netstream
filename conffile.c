#include <stdlib.h>
#include <yaml.h>

#include "netstream.h"
#include "conffile.h"

/* Initialize endpoint config structure */
void endpt_config_init(struct endpt_cfg * config) {
	config->dir = DIR_INVAL;
	config->type = T_INVAL;
	config->retry = NO;
	config->name = NULL;
	config->port = NULL;
	config->protocol = -1;
	config->keepalive = 0;
	config->exit_status = -255;
}

/*
 * Initialize I/O config structure with nouts outputs
 *
 * Returns 0 on success, -1 if allocation fails.
 */
int io_config_init(struct io_cfg * config, int nitems) {
	config->n_outs = nitems;
	config->outs = malloc(sizeof (struct endpt_cfg)*nitems);
	config->input = malloc(sizeof (struct endpt_cfg));
	if (config->outs == NULL || (config->input == NULL)) {
		dprint(WARN, "Failed to allocate memory in %s\n", __FUNCTION__);
		free(config);
		return (-1);
	}
	return (0);
}

static void inv_val_warn(char * val, char * key) {
	dprint(WARN, "Invalid value \"%s\" for key \"%s\"\n", val, key);
}

/*
 * Set item with name key to value value in endpoint config config.
 *
 * Returns 0 on success, -1 if value is invalid for given key.
 */
int endpt_config_set_item(struct endpt_cfg * config, char * key, char * value) {
	// Direction
	if (strcmp(key, "Direction") == 0) {
		if (strcmp(value, "input") == 0) {
			config->dir = DIR_INPUT;
		} else if (strcmp(value, "output") == 0) {
			config->dir = DIR_OUTPUT;
		} else  {
			inv_val_warn(value, key);
			return (-1);
		}
	// Type
	} else if (strcmp(key, "Type") == 0) {
		if (strcmp(value, "socket") == 0) {
			config->type = T_SOCKET;
		} else if (strcmp(value, "file") == 0) {
			config->type = T_FILE;
		} else if (strcmp(value, "std") == 0) {
			config->type = T_STD;
		} else  {
			inv_val_warn(value, key);
			return (-1);
		}
	// Retry
	} else if (strcmp(key, "Retry") == 0) {
		if (strcmp(value, "yes") == 0) {
			config->retry = YES;
		} else if (strcmp(value, "no") == 0) {
			config->retry = NO;
		} else if (strcmp(value, "ignore") == 0) {
			config->retry = IGNORE;
		} else  {
			inv_val_warn(value, key);
			return (-1);
		}
	// Name
	} else if (strcmp(key, "Name") == 0) {
		size_t len = strlen(value)+1;
		char * name;
		name = malloc(sizeof (char)*len);
		strcpy(name, value);
		config->name = name;
	// Port
	} else if (strcmp(key, "Port") == 0) {
		size_t len = strlen(value)+1;
		char * port;
		port = malloc(sizeof (char)*len);
		strcpy(port, value);
		config->port = port;
	// Protocol
	} else if (strcmp(key, "Protocol") == 0) {
		if (strcmp(value, "TCP") == 0) {
			config->protocol = IPPROTO_TCP;
		} else if (strcmp(value, "UDP") == 0) {
			config->protocol = IPPROTO_UDP;
		} else  {
			inv_val_warn(value, key);
			return (-1);
		}
	// Keepalive
	} else if (strcmp(key, "Keepalive") == 0) {
		int keepalive = strtol(value, NULL, 10);
		if (keepalive < 0) {
			dprint(WARN, "Keepalive couldn't be negative \n");
			return (-1);
		}
		config->keepalive = keepalive;

	} else  {
		dprint(NOTICE, "Unknown key \"%s\"\n", key);
		return (0);
	}
	return (0);
}

/*
 * Find the input in config. In config file input is not needed to be first,
 * this function returns pointer to first input defined in config file or NULL
 * if none is defined.
 */
static struct endpt_cfg * get_read_endpt(struct io_cfg * cfg) {
	for (int i = 0; i < cfg->n_outs; i++) {
		if (cfg->outs[i].dir == DIR_INPUT)
			return (&cfg->outs[i]);
	}
	return (NULL);

}

/*
 * Parse config file from given filename into given config structure
 *
 * Returns 0 on success, -1 on error.
 */
// TODO dealokace pri nepovedenem cteni konfigurace
int parse_config_file(struct io_cfg * config, char * filename) {
	yaml_parser_t parser;
	yaml_document_t document;

	FILE * cfg_file;
	cfg_file = fopen(filename, "r");
	if (cfg_file == NULL) {
		dprint(CRIT, "Could not open config file \"%s\"\n", filename);
		return (-1);
	}

	yaml_parser_initialize(&parser);
	yaml_parser_set_input_file(&parser, cfg_file);

	if (!yaml_parser_load(&parser, &document)) {
		dprint(ERR, "Could not load config file to YAML parser "
			"(probably syntax error)\n");
		return (-1);
	}
	fclose(cfg_file);

	yaml_node_t * root;
	root = yaml_document_get_root_node(&document);
	if (root->type != YAML_SEQUENCE_NODE) {
		dprint(ERR, "Wrong type of YAML root node "
			"(must be sequence)\n");
		return (-1);
	}

	size_t items;
	items = (root->data.sequence.items.top -
		root->data.sequence.items.start);
	if (io_config_init(config, items) == -1) {
		dprint(CRIT, "Error while initializing config structure\n");
		return (-1);
	}
	yaml_node_item_t * endpt = root->data.sequence.items.start;

	for (int i = 0; i < items; i++) {
		yaml_node_t * ep_params;
		ep_params = yaml_document_get_node(&document, *endpt);
		if (ep_params->type != YAML_MAPPING_NODE) {
			dprint(ERR, "Wrong type of YAML sequence node "
				"(must be mapping)\n");
			return (-1);
		}

		endpt_config_init(&config->outs[i]);

		for (yaml_node_pair_t * ep_par = ep_params->data.mapping.pairs.start;
			ep_par < ep_params->data.mapping.pairs.top; ep_par++) {

			yaml_node_t * key;
			yaml_node_t * value;
			key = yaml_document_get_node(&document, ep_par->key);
			value = yaml_document_get_node(&document,
				ep_par->value);
			if (endpt_config_set_item(&config->outs[i],
				(char *)key->data.scalar.value,
				(char *)value->data.scalar.value) == -1) {
				dprint(ERR, "Error when parsing config\n");
				return (-1);
			}
		}
		endpt++;

	}

	struct endpt_cfg * read_endpt;
	read_endpt = get_read_endpt(config);
	if (read_endpt == NULL) {
		dprint(CRIT, "No input defined\n");
		return (-1);
	}
	*(config->input) = *(read_endpt);
	for (int i = 0; i < config->n_outs-1; i++) {
		if (config->outs+i >= read_endpt) {
			config->outs[i] = config->outs[i+1];
		}
	}
	config->n_outs--;

	return (0);
}

/* Printf I/O config cfg to stdout */
void print_config(struct io_cfg * cfg) {
	printf("Config:\n");
	for (int i = 0; i < cfg->n_outs; i++) {
		printf("Output %d:\n", i);
		printf("	Direction: ");
		switch (cfg->outs[i].dir) {
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
		switch (cfg->outs[i].type) {
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
		switch (cfg->outs[i].retry) {
			case YES:
				printf("yes\n");
				break;
			case NO:
				printf("no\n");
				break;
			case IGNORE:
				printf("ignore\n");
				break;
			case KILL:
				printf("kill\n");
				break;
		}
		printf("	Name: %s\n", cfg->outs[i].name);
		printf("	Port: %s\n", cfg->outs[i].port);
		printf("	Protocol: ");
		switch (cfg->outs[i].protocol) {
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
		printf("	Keepalive: %d\n", cfg->outs[i].keepalive);
		printf("\n");


	}
	printf("Input:\n");
	printf("	Direction: ");
	switch (cfg->input->dir) {
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
	switch (cfg->input->type) {
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
	switch (cfg->input->retry) {
		case YES:
			printf("yes\n");
			break;
		case NO:
			printf("no\n");
			break;
		case IGNORE:
			printf("ignore\n");
			break;
		case KILL:
			printf("kill\n");
			break;
	}
	printf("	Name: %s\n", cfg->input->name);
	printf("	Port: %s\n", cfg->input->port);
	printf("	Protocol: ");
	switch (cfg->input->protocol) {
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
	printf("	Keepalive: %d\n", cfg->input->keepalive);
	printf("\n");
}

static void endpt_undef_err(char num, char * name) {
	dprint(ERR, "Endpoint %d %s not defined\n");
}

/*
 * Check endpoint configuration. Parameter num is order of the endpoint and it
 * is used only for error prints.
 *
 * Returns 1 on success, 0 if there is an error in configuration.
 */
static int  check_endpt(struct endpt_cfg * cfg, char num) {
	if (cfg->dir == DIR_INVAL) {
		dprint(ERR, "Endpoint %d dir not defined\n", num);
		return (0);
	}
	switch (cfg->type) {
		case T_INVAL:
			endpt_undef_err(num, "type");
			return (0);
		case T_FILE:
			if (cfg->name == NULL) {
				endpt_undef_err(num, "name");
				return (0);
			}
			break;
		case T_SOCKET:
			if (cfg->name == NULL) {
				endpt_undef_err(num, "name");
				return (0);
			}
			if (cfg->port == NULL) {
				endpt_undef_err(num, "port");
				return (0);
			}
			if (cfg->protocol == -1) {
				endpt_undef_err(num, "protocol");
				return (0);
			}
			if (cfg->keepalive == -1) {
				endpt_undef_err(num, "keepalive");
				return (0);
			}
			break;
		case T_STD:
			break;
	}
	return (1);

}

/*
 * Check I/O config.
 *
 * Returns 1 on success, 0 if there is an error in configuration
 */
int check_config(struct io_cfg * config) {
	if (config->n_outs > MAX_OUTPUTS) {
		dprint(ERR, "Defined more outputs than MAX_OUTPUTS\n");
		return (0);
	}
	if (!check_endpt(config->input, 0))
		return (0);
	for (int i = 0; i < config->n_outs; i++) {
		if (config->outs[i].dir != DIR_OUTPUT) {
			dprint(ERR, "More inputs defined\n");
			return (0);
		}
		if (!check_endpt(&config->outs[i], i+1)) {
			return (0);
		}
	}
	return (1);
}
