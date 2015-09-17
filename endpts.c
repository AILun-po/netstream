#define _XOPEN_SOURCE 700
#include <stdlib.h>

#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>

#include "netstream.h"
#include "buffer.h"
#include "endpts.h"

char poll_errs(void * id,struct pollfd * pollfds){
	char fail = 0; 
	if (pollfds[0].revents & POLLERR){
		fail = 1;
		tdprint(id,WARN,"POLLERR for read fd\n");
	}
	if (pollfds[0].revents & POLLHUP){
		fail = 1;
		tdprint(id,WARN,"POLLHUP for read fd\n");
	}
	if (pollfds[0].revents & POLLNVAL){
		fail = 1;
		tdprint(id,WARN,"POLLNVAL for read fd\n");
	}
	if (pollfds[1].revents & POLLERR){
		fail = 1;
		tdprint(id,WARN,"POLLERR for signal fd\n");
	}
	if (pollfds[1].revents & POLLHUP){
		fail = 1;
		tdprint(id,WARN,"POLLHUP for signal fd\n");
	}
	if (pollfds[1].revents & POLLNVAL){
		fail = 1;
		tdprint(id,WARN,"POLLNVAL for signal fd\n");
	}
	return fail;
}

void exit_thread(struct endpt_cfg * cfg, int status){
	cfg->exit_status = status;
	struct deadlist * dlist;
	dlist = cfg->dlist;
	pthread_mutex_lock(&(dlist->mtx));
	dlist->cfg_list[dlist->pos]=cfg;
	pthread_cond_broadcast(&(dlist->condv));
	dlist->pos++;
	pthread_mutex_unlock(&(dlist->mtx));
	pthread_exit(NULL);
} 


/* Endpoint for input. Gets pointer to I/O config in args */
void * read_endpt(void * args){
	struct io_cfg * cfg;
	cfg = (struct io_cfg *) args;
	struct endpt_cfg * read_cfg;
	read_cfg = cfg->input;
	read_cfg->exit_status=0;
	
	// Mask signals
	sigset_t sigset;
	if (sigfillset(&sigset)==-1)
	{
		tdprint((void *)read_cfg,WARN,"Error in signal setup\n");
		exit_thread(read_cfg,-1);
	}
	if(pthread_sigmask(SIG_BLOCK,&sigset,NULL)){
		tdprint((void *)read_cfg,WARN,"Error in signal setup\n");
		exit_thread(read_cfg,-1);
	}


	int listenfd;
	listenfd = -1;
	struct pollfd pollfds[2];
	pollfds[1].fd = signal_fds[0];
	pollfds[1].events = POLLIN;
	int readfd;
	readfd = -1;
	do {
		tdprint((void *)read_cfg,INFO,"Start reading\n");
		if (read_cfg->type == T_FILE){
			tdprint((void *)read_cfg,DEBUG,"File\n");
			readfd = open(read_cfg->name,O_RDONLY);
			if (readfd==-1){
				warn("Error while opening %s for reading: ",read_cfg->name);	
				read_cfg->exit_status = -1;
				goto read_repeat;
			} else if (read_cfg->test_only){
				close(readfd);
				exit_thread(read_cfg,0);
			}
		}else if (read_cfg->type == T_SOCKET && read_cfg->protocol == IPPROTO_TCP){
			tdprint((void *)read_cfg,DEBUG,"Socket\n");
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
					tdprint((void *)read_cfg,ERR,"Error when resolving %s: %s\n",read_cfg->name,gai_strerror(res));
					read_cfg->exit_status = -1;
					goto read_repeat;
				}
				for (struct addrinfo * aiptr = addrinfo; aiptr!=NULL; aiptr=aiptr->ai_next){
					listenfd = socket(aiptr->ai_family,aiptr->ai_socktype,aiptr->ai_protocol);
					if (listenfd == -1)
						continue;
					tdprint((void *)read_cfg,DEBUG,"Binding\n");
					if (bind(listenfd,aiptr->ai_addr,aiptr->ai_addrlen) == 0)
						break;
					close(listenfd);
					listenfd = -1;
				}
				freeaddrinfo(addrinfo);
				if (listenfd == -1){
					tdprint((void *)read_cfg,ERR,"Could not bind to port %s\n",read_cfg->port);
					read_cfg->exit_status = -1;
					goto read_repeat;
				}
				tdprint((void *)read_cfg,DEBUG,"Listening\n");
				if (listen(listenfd,1)){
					warn("Could not listen on port %s\n",read_cfg->port);
					read_cfg->exit_status = -1;
					goto read_repeat;
				}
			}
			
			if (read_cfg->test_only){
				close(listenfd);
				exit_thread(read_cfg,0);
			}

			tdprint((void *)read_cfg,DEBUG,"Accepting\n");
			do {
				pollfds[0].fd = listenfd;
				pollfds[0].events = POLLIN;
				if(poll(pollfds,2,-1)==-1){
					warn("Error when polling on read");
					close(listenfd);
					listenfd = -1;
					read_cfg->exit_status = -1;
					goto read_repeat;
				}
				poll_errs((void *)read_cfg,pollfds);
				if (pollfds[1].revents & POLLIN){
					tdprint((void *)read_cfg,INFO,"Signal received, interrupting accept\n");
					// Handle signals
					int8_t signum;
					int ret;
					errno = 0;
					ret = read(signal_fds[0],&signum,1);
					if (ret==-1){
						warn("Error occured when reading from signal pipe");
					}
					switch (signum){
						case SIGINT:
						case SIGTERM:
							tdprint((void *)read_cfg,INFO,"Received SIGINT\n");
							read_cfg->retry = KILL;
							goto read_repeat;
						default:
							tdprint((void *)read_cfg,INFO,"Received signal %d\n, ignoring\n",signum);
					}

				}
				if (!(pollfds[0].revents & POLLIN)){
					continue;
				}
			} while (0); 
			readfd = accept(listenfd,NULL,NULL);
			if (readfd == -1){
				warn("Could not accept on port %s:",read_cfg->port);
				read_cfg->exit_status = -1;
				goto read_repeat;
			}
			if (read_cfg->keepalive != 0){
				int optval;
				optval = read_cfg->keepalive;
				if (setsockopt(readfd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) < 0){
					tdprint((void *)read_cfg,WARN,"Could not set keepalive interval\n");
				}
#ifdef __linux__
				optval = read_cfg->keepalive;
				if (setsockopt(readfd, IPPROTO_TCP, TCP_KEEPIDLE, &optval, sizeof(optval)) < 0){
					tdprint((void *)read_cfg,WARN,"Could not set keepalive interval\n");
				}
				if (setsockopt(readfd, IPPROTO_TCP, TCP_KEEPINTVL, &optval, sizeof(optval)) < 0){
					tdprint((void *)read_cfg,WARN,"Could not set keepalive interval\n");
				}
#endif
			}
			tdprint((void *)read_cfg,DEBUG,"Reading\n");
		
		}else if (read_cfg->type == T_SOCKET && read_cfg->protocol == IPPROTO_UDP){
			tdprint((void *)read_cfg,DEBUG,"Socket\n");
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
				tdprint((void *)read_cfg,ERR,"Error when resolving %s: %s\n",read_cfg->name,gai_strerror(res));
				read_cfg->exit_status = -1;
				goto read_repeat;
			}
			for (struct addrinfo * aiptr = addrinfo; aiptr!=NULL; aiptr=aiptr->ai_next){
				readfd = socket(aiptr->ai_family,aiptr->ai_socktype,aiptr->ai_protocol);
				if (readfd == -1)
					continue;
				tdprint((void *)read_cfg,DEBUG,"Binding\n");
				if (bind(readfd,aiptr->ai_addr,aiptr->ai_addrlen) == 0)
					break;
				close(readfd);
			}
			freeaddrinfo(addrinfo);
			if (readfd == -1){
				tdprint((void *)read_cfg,ERR,"Could not bind to port %s\n",read_cfg->port);
				read_cfg->exit_status = -1;
				goto read_repeat;
			}
			tdprint((void *)read_cfg,DEBUG,"Reading\n");

			if (read_cfg->test_only){
				close(readfd);
				exit_thread(read_cfg,0);
			}

		}else if (read_cfg->type == T_STD){
			tdprint((void *)read_cfg,DEBUG,"Stdin\n");
			readfd=0;
			if (read_cfg->test_only){
				exit_thread(read_cfg,0);
			}

		}

		char * readbuf;
		readbuf = malloc(sizeof(char)*READ_BUFFER_BLOCK_SIZE);
		while (1){
			tdprint((void *)read_cfg,DEBUG,"Rereading\n");
			size_t toread;
			size_t nread;
			toread = READ_BUFFER_BLOCK_SIZE;
			nread = 0;
			while (nread < toread){
				ssize_t res;
				pollfds[0].fd = readfd;
				pollfds[0].events = POLLIN;
				if(poll(pollfds,2,-1)==-1){
					warn("Error when polling on read");
					close(readfd);
					free(readbuf);
					read_cfg->exit_status = -1;
					goto read_repeat;
				}
				poll_errs((void *)read_cfg,pollfds);
				if (pollfds[1].revents & POLLIN){
					tdprint((void *)read_cfg,INFO,"Signal received, interrupting read\n");
					// Handle signals
					int8_t signum;
					int ret;
					errno = 0;
					ret = read(signal_fds[0],&signum,1);
					if (ret==-1){
						warn("Error occured when reading from signal pipe");
					}
					switch (signum){
						case SIGINT:
						case SIGTERM:
							tdprint((void *)read_cfg,INFO,"Received SIGINT\n");
							read_cfg->retry = KILL;
							goto read_repeat;
						default:
							tdprint((void *)read_cfg,INFO,"Received signal %d\n, ignoring\n",signum);
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

					tdprint((void *)read_cfg,INFO,"Got message from socket\n");

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
		if (read_cfg->test_only){
			exit_thread(read_cfg,read_cfg->exit_status);
		}
		switch(read_cfg->retry){
			case YES:
				tdprint((void *)read_cfg,INFO,"Retrying read\n");
				break;
			case KILL:
				close(listenfd);
				tdprint((void *)read_cfg,INFO,"Ending read\n");
				for (int i=0;i<cfg->n_outs;i++){
					buffer_insert(cfg->outs[i].buf,NULL,BUF_KILL);
				}
				exit_thread(read_cfg,-2);
			case NO:
			case IGNORE:
				close(listenfd);
				tdprint((void *)read_cfg,INFO,"Ending read\n");
				for (int i=0;i<cfg->n_outs;i++){
					buffer_insert(cfg->outs[i].buf,NULL,BUF_END_DATA);
				}
				exit_thread(read_cfg,read_cfg->exit_status);
		
		}
		sleep(RETRY_DELAY);
	} while (1);
	// Should be unreachable
	exit_thread(read_cfg,read_cfg->exit_status);
} 

/* Output endpoint. Gets pointer to endpoint config in args*/
void * write_endpt(void * args){

	struct endpt_cfg * cfg;
	cfg = (struct endpt_cfg *)args;

	// Mask signals
	sigset_t sigset;
	if (sigfillset(&sigset)==-1)
	{
		tdprint(args,WARN,"Error in signal setup\n");
		exit_thread(cfg,-1);
	}
	if(pthread_sigmask(SIG_BLOCK,&sigset,NULL)){
		tdprint(args,WARN,"Error in signal setup\n");
		exit_thread(cfg,-1);
	}

	int writefd = -1;
	do {
		tdprint(args,INFO,"Start writing\n",args);
		// For use in sendto
		struct sockaddr * addr = NULL;
		socklen_t addrlen = 0;
		if (cfg->type == T_FILE){
			writefd = open(cfg->name, O_WRONLY | O_CREAT | O_TRUNC,
				S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
			if (writefd == -1){
				warn("Opening file %s to write failed\n",cfg->name);
				cfg->exit_status = -1;
				goto write_repeat;
			}
			if (cfg->test_only){
				close(writefd);
				exit_thread(cfg,0);
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
				tdprint(args,ERR,"Error when resolving %s: %s\n",cfg->name,gai_strerror(res));
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
				if (cfg->keepalive != 0){
					int optval;
					optval = 1;
					if (setsockopt(writefd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) < 0){
						tdprint(args,WARN,"Could not set keepalive interval\n");
					}
#ifdef __linux__
					optval = cfg->keepalive;
					if (setsockopt(writefd, IPPROTO_TCP, TCP_KEEPIDLE, &optval, sizeof(optval)) < 0){
						tdprint(args,WARN,"Could not set keepalive interval\n");
					}
					if (setsockopt(writefd, IPPROTO_TCP, TCP_KEEPINTVL, &optval, sizeof(optval)) < 0){
						tdprint(args,WARN,"Could not set keepalive interval\n");
					}
#endif
				}
				if (connect(writefd,addr,addrlen) != -1)
					break;
				close(writefd);
				writefd = -1;
			}
			freeaddrinfo(addrinfo);
			if (writefd == -1){
				tdprint(args,ERR,"Could not connect to %s\n",cfg->name);
				cfg->exit_status = -1;
				goto write_repeat;
			}
			if (cfg->test_only){
				close(writefd);
				exit_thread(cfg,0);
			}
		
		} else if (cfg->type == T_STD){
			writefd = 1;	
			if (cfg->test_only){
				exit_thread(cfg,0);
			}
		}

		char * writebuf;
		while (1) {
			size_t towrite;
			towrite = buffer_after_delete(cfg->buf);
			if (towrite == BUF_END_DATA){
				cfg->exit_status = 0;
				tdprint(args,INFO,"End of data\n",args);
				close(writefd);
				exit_thread(cfg,cfg->exit_status);
			}
			if (towrite == BUF_KILL){
				cfg->exit_status = -2;
				tdprint(args,INFO,"End required by signal\n",args);
				close(writefd);
				exit_thread(cfg,cfg->exit_status);
			}
			writebuf = buffer_cons_data_pointer(cfg->buf);
			if (cfg->type == T_SOCKET && cfg->protocol == IPPROTO_UDP){
				ssize_t res;
				res = sendto(writefd,writebuf,towrite,0,addr,addrlen);
				if (res == -1)
					warn("Error in sending data\n");
			} else {
				int nwritten;
				nwritten = 0;
				while (nwritten < towrite){
					ssize_t res;
					res = write(writefd,(void *)(writebuf+nwritten),towrite-nwritten);
					if (res < 0){
						warn("Error in sending data");
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
				tdprint(args,INFO,"Retrying\n",args);
				break;
			case NO:
				tdprint(args,INFO,"Terminating\n",args);
				exit_thread(cfg,cfg->exit_status);
			case IGNORE:
			case KILL:
				tdprint(args,INFO,"Terminating\n",args);
				exit_thread(cfg,0);
		}
		sleep(RETRY_DELAY);

	} while (1);

	// Unreachable
	exit_thread(cfg,cfg->exit_status);
}
