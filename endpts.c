#define _XOPEN_SOURCE 600
#include <stdlib.h>

#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>

#include "netstream.h"
#include "buffer.h"
#include "endpts.h"


/* Endpoint for input. Gets pointer to I/O config in args */
void * read_endpt(void * args){
	// Mask signals
	// TODO report error codes
	sigset_t sigset;
	sigfillset(&sigset);
	pthread_sigmask(SIG_BLOCK,&sigset,NULL);


	struct io_cfg * cfg;
	cfg = (struct io_cfg *) args;
	int listenfd;
	listenfd = -1;
	do {
	dprint(INFO,"Start reading\n");
	int readfd;
	struct endpt_cfg * read_cfg;
	read_cfg = cfg->input;
	if (read_cfg->type == T_FILE){
		dprint(DEBUG,"File\n");
		readfd = open(read_cfg->name,O_RDONLY);
		if (readfd==-1){
			warn("Error while opening %s for reading",read_cfg->name);	
			read_cfg->exit_status = -1;
			goto read_repeat;
		}
	}else if (read_cfg->type == T_SOCKET && read_cfg->protocol == IPPROTO_TCP){
		dprint(DEBUG,"Socket\n");
		if (listenfd==-1){
			struct addrinfo hints;
			memset(&hints,0,sizeof(struct addrinfo));
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_flags = AI_PASSIVE;
			hints.ai_protocol = 0;

			int res;
			struct addrinfo * addrinfo;
			res = getaddrinfo(NULL,read_cfg->port,&hints,&addrinfo);
			if (res){
				dprint(ERR,"Error when resolving %s: %s\n",read_cfg->name,gai_strerror(res));
				read_cfg->exit_status = -1;
				goto read_repeat;
			}
			for (struct addrinfo * aiptr = addrinfo; aiptr!=NULL; aiptr=aiptr->ai_next){
				listenfd = socket(aiptr->ai_family,aiptr->ai_socktype,aiptr->ai_protocol);
				if (listenfd == -1)
					continue;
				dprint(DEBUG,"Binding\n");
				if (bind(listenfd,aiptr->ai_addr,aiptr->ai_addrlen) == 0)
					break;
				close(listenfd);
				listenfd = -1;
			}
			freeaddrinfo(addrinfo);
			if (listenfd == -1){
				dprint(ERR,"Could not bind to port %s\n",read_cfg->port);
				read_cfg->exit_status = -1;
				goto read_repeat;
			}
			dprint(DEBUG,"Listening\n");
			if (listen(listenfd,1)){
				warn("Could not listen on port %s\n",read_cfg->port);
				read_cfg->exit_status = -1;
				goto read_repeat;
			}
		}
		dprint(DEBUG,"Accepting\n");
		readfd = accept(listenfd,NULL,NULL);
		if (readfd == -1){
			warn("Could not accept on port %s:",read_cfg->port);
			read_cfg->exit_status = -1;
			goto read_repeat;
		}
		dprint(DEBUG,"Reading\n");
	
	}else if (read_cfg->type == T_SOCKET && read_cfg->protocol == IPPROTO_UDP){
		dprint(DEBUG,"Socket\n");
		struct addrinfo hints;
		memset(&hints,0,sizeof(struct addrinfo));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_flags = AI_PASSIVE;
		hints.ai_protocol = 0;

		int res;
		struct addrinfo * addrinfo;
		res = getaddrinfo(NULL,read_cfg->port,&hints,&addrinfo);
		if (res){
			dprint(ERR,"Error when resolving %s: %s\n",read_cfg->name,gai_strerror(res));
			read_cfg->exit_status = -1;
			goto read_repeat;
		}
		for (struct addrinfo * aiptr = addrinfo; aiptr!=NULL; aiptr=aiptr->ai_next){
			readfd = socket(aiptr->ai_family,aiptr->ai_socktype,aiptr->ai_protocol);
			if (readfd == -1)
				continue;
			dprint(DEBUG,"Binding\n");
			if (bind(readfd,aiptr->ai_addr,aiptr->ai_addrlen) == 0)
				break;
			close(readfd);
		}
		freeaddrinfo(addrinfo);
		if (readfd == -1){
			dprint(ERR,"Could not bind to port %s\n",read_cfg->port);
			read_cfg->exit_status = -1;
			goto read_repeat;
		}
		dprint(DEBUG,"Reading\n");
	}else if (read_cfg->type == T_STD){
		dprint(DEBUG,"Stdin\n");
		readfd=0;
	}

	char * readbuf;
	readbuf = malloc(sizeof(char)*READ_BUFFER_BLOCK_SIZE);
	while (1){
		dprint(DEBUG,"Rereading\n");
		size_t toread;
		size_t nread;
		toread = READ_BUFFER_BLOCK_SIZE;
		nread = 0;
		while (nread < toread){
			ssize_t res;
			struct pollfd pollfds[2];
			pollfds[0].fd = readfd;
			pollfds[0].events = POLLIN;
			pollfds[1].fd = signal_fds[0];
			pollfds[1].events = POLLIN;
			if(poll(pollfds,2,-1)==-1){
				warn("Error when polling on read");
				close(readfd);
				free(readbuf);
				read_cfg->exit_status = -1;
				goto read_repeat;
			}
			// TODO: Handle poll error stats
			if (pollfds[0].revents & POLLERR){
				dprint(WARN,"POLLERR for read fd\n");
			}
			if (pollfds[0].revents & POLLHUP){
				dprint(WARN,"POLLHUP for read fd\n");
			}
			if (pollfds[0].revents & POLLNVAL){
				dprint(WARN,"POLLNVAL for read fd\n");
			}
			if (pollfds[1].revents & POLLERR){
				dprint(WARN,"POLLERR for signal fd\n");
			}
			if (pollfds[1].revents & POLLHUP){
				dprint(WARN,"POLLHUP for signal fd\n");
			}
			if (pollfds[1].revents & POLLNVAL){
				dprint(WARN,"POLLNVAL for signal fd\n");
			}
			if (pollfds[1].revents & POLLIN){
				dprint(INFO,"Signal received, interrupting read");
				// Handle signals
				int8_t signum;
				int ret;
				errno = 0;
				dprint(DEBUG,"signal_fds[0]=%d\n",signal_fds[0]);
				ret = read(signal_fds[0],&signum,1);
				if (ret==-1){
					warn("Error occured when reading from signal pipe");
				}
				switch (signum){
					case SIGINT:
						dprint(INFO,"Received SIGINT\n");
						read_cfg->retry = KILL;
						goto read_repeat;
					default:
						dprint(INFO,"Received signal %d\n, ignoring\n",signum);
				}

			}
			if (!(pollfds[0].revents & POLLIN)){
				continue;
			}
			if (read_cfg->type == T_SOCKET && read_cfg->protocol == IPPROTO_UDP){
				struct sockaddr from_addr;
				socklen_t from_addrlen;
				from_addrlen = 14; // From sys/socket.h, is there more correct system?
				res = recvfrom(readfd,(void *)readbuf,READ_BUFFER_BLOCK_SIZE,0,&from_addr,&from_addrlen);

				dprint(INFO,"Got message from socket\n");

			} else {
				res = read(readfd,(void *)(readbuf+nread),(toread-nread));
			}
			if (res == 0){ // EOF
				close(readfd);
				for (int i=0;i<cfg->n_outs;i++){
					buffer_insert(cfg->outs[i].buf,readbuf,nread);
				}
				free(readbuf);
				read_cfg->exit_status = 0;
				goto read_repeat;
			}else if (res == -1){ //Error
				warn("Error while reading from %s",read_cfg->name);
				close(readfd);
				free(readbuf);
				read_cfg->exit_status = -1;
				goto read_repeat;
			}
			nread += res;
			if (read_cfg->type == T_SOCKET && read_cfg->protocol == IPPROTO_UDP){
				break;
			}
		}
		for (int i=0;i<cfg->n_outs;i++){
			buffer_insert(cfg->outs[i].buf,readbuf,nread);
		}
	}
read_repeat:
	switch(read_cfg->retry){
		case YES:
			dprint(INFO,"Retrying read\n");
			break;
		case KILL:
			close(listenfd);
			dprint(INFO,"Ending read\n");
			for (int i=0;i<cfg->n_outs;i++){
				buffer_insert(cfg->outs[i].buf,NULL,BUF_KILL);
			}
			cfg->input->exit_status=-2;
			return NULL;
		case NO:
		case IGNORE:
			close(listenfd);
			dprint(INFO,"Ending read\n");
			for (int i=0;i<cfg->n_outs;i++){
				buffer_insert(cfg->outs[i].buf,NULL,BUF_END_DATA);
			}
			cfg->input->exit_status=0;
			return NULL;
	
	}
	sleep(RETRY_DELAY);
	} while (1);
	// Should be unreachable
	return NULL;
} 

/* Output endpoint. Gets pointer to endpoint config in args*/
void * write_endpt(void * args){
	// Mask signals
	// TODO report error codes
	sigset_t sigset;
	sigfillset(&sigset);
	pthread_sigmask(SIG_BLOCK,&sigset,NULL);
	do {
	dprint(INFO,"Thread %p: Start writing\n",args);
	struct endpt_cfg * cfg;
	cfg = (struct endpt_cfg *)args;
	int writefd;
	// For use in sendto
	struct sockaddr * addr;
	socklen_t addrlen;
	if (cfg->type == T_FILE){
		writefd = open(cfg->name, O_WRONLY | O_CREAT | O_TRUNC,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (writefd == -1){
			warn("Opening file %s to write failed\n",cfg->name);
			cfg->exit_status = -1;
			goto write_repeat;
		}
	
	} else if (cfg->type == T_SOCKET){
		struct addrinfo hints;
		memset(&hints,0,sizeof(struct addrinfo));
		hints.ai_family = AF_UNSPEC;
		if (cfg->protocol == IPPROTO_TCP)
			hints.ai_socktype = SOCK_STREAM;
		else if (cfg->protocol == IPPROTO_UDP)
			hints.ai_socktype = SOCK_DGRAM;
		hints.ai_flags = 0;
		hints.ai_protocol = 0;

		int res;
		struct addrinfo * addrinfo;
		res = getaddrinfo(cfg->name,cfg->port,&hints,&addrinfo);
		if (res){
			dprint(ERR,"Error when resolving %s: %s\n",cfg->name,gai_strerror(res));
			cfg->exit_status = -1;
			goto write_repeat;
		}
		for (struct addrinfo * aiptr = addrinfo; aiptr!=NULL; aiptr=aiptr->ai_next){
			addr = aiptr->ai_addr;
			addrlen = aiptr->ai_addrlen;
			writefd = socket(aiptr->ai_family,aiptr->ai_socktype,aiptr->ai_protocol);
			if (writefd == -1)
				continue;
			if (cfg->protocol == IPPROTO_UDP)
				break;
			if (connect(writefd,addr,addrlen) != -1)
				break;
			close(writefd);
			writefd = -1;
		}
		freeaddrinfo(addrinfo);
		if (writefd == -1){
			dprint(ERR,"Could not connect to %s\n",cfg->name);
			cfg->exit_status = -1;
			goto write_repeat;
		}
	
	} else if (cfg->type == T_STD){
		writefd = 1;	
	}

	char * writebuf;
	while (1) {
		size_t towrite;
		towrite = buffer_after_delete(cfg->buf);
		if (towrite == BUF_END_DATA){
			cfg->exit_status = 0;
			dprint(INFO,"Thread %p: End of data\n",args);
			close(writefd);
			return NULL;
		}
		if (towrite == BUF_KILL){
			cfg->exit_status = -2;
			dprint(INFO,"Thread %p: End required by signal\n",args);
			close(writefd);
			return NULL;
		}
		writebuf = buffer_cons_data_pointer(cfg->buf);
		if (cfg->type == T_SOCKET && cfg->protocol == IPPROTO_UDP){
			ssize_t res;
			res = sendto(writefd,writebuf,towrite,0,addr,addrlen);
			if (res == -1)
				warn("Thread %p: Error while writing buffer",args);
		} else {
			int nwritten;
			nwritten = 0;
			while (nwritten < towrite){
				ssize_t res;
				res = write(writefd,(void *)(writebuf+nwritten),towrite-nwritten);
				if (res < 0){
					warn("Thread %p: Error while writing buffer:",args);
					cfg->exit_status = -1;
					close(writefd);
					goto write_repeat;
				}
				nwritten += res;
			}
		}
	}
write_repeat:
	switch (cfg->retry){
		case YES:
			dprint(INFO,"Thread %p: Retrying\n",args);
			break;
		case NO:
			dprint(INFO,"Thread %p: Terminating\n",args);
			return NULL;
		case IGNORE:
		case KILL:
			dprint(INFO,"Thread %p: Terminating\n",args);
			cfg->exit_status = 0;
			return NULL;
	}
	sleep(RETRY_DELAY);

	} while (1);

	// Unreachable
	return NULL;
}
