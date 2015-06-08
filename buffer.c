#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "netstream.h"
#include "buffer.h"

int buffer_insert(struct buffer * buf,char * data,ssize_t ndata){
	pthread_mutex_lock(&buf->lock);
	// Buffer is full, discard data 
	if ((buf->prod_pos+1)%buf->nitems == buf->cons_pos){
		pthread_mutex_unlock(&buf->lock);
		dprint(WARN,"Buffer %p overflow\n",buf);
		return -1;
	}
	if (ndata >= 0){
		memcpy(buf->buffer+buf->prod_pos*buf->it_size,
			data,ndata);
		buf->datalens[buf->prod_pos] = ndata;
			
	} else {
		buf->datalens[buf->prod_pos] = -1; 
	}
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
	pthread_mutex_lock(&buf->lock);
	// Buffer is empty, wait until is filled
	if ((buf->cons_pos+1)%buf->nitems == buf->prod_pos){
		pthread_cond_wait(&buf->empty_cv,&buf->lock);
	}
	buf->cons_pos = (buf->cons_pos+1)%buf->nitems;
	ssize_t ncons_data;
	ncons_data = buf->datalens[buf->cons_pos]; 
	pthread_mutex_unlock(&buf->lock);
	return ncons_data;
}

struct buffer * create_buffers(int nbuffers){
	struct buffer * buffers;
	buffers = malloc(sizeof(struct buffer)*nbuffers);
	if (buffers == NULL){
		dprint(WARN,"Can't allocate memory for buffers\n");
		return NULL;
	}
	for (int i=0; i<nbuffers;i++){
		char * buffer;
		ssize_t * datalens;
		buffer = malloc(sizeof(char)*WRITE_BUFFER_BLOCK_SIZE*WRITE_BUFFER_BLOCK_COUNT);
		datalens = malloc(sizeof(ssize_t)*WRITE_BUFFER_BLOCK_COUNT);
		if (buffer == NULL || datalens == NULL){
			dprint(WARN,"Can't allocate memory for buffers\n");
			free(buffer);
			free(datalens);
			while (i>=0){
				i--;
				free(buffers[i].buffer);
				free(buffers[i].datalens);
			}
			return NULL;
		}
		buffers[i].buffer = buffer;
		buffers[i].datalens = datalens;
		buffers[i].nitems = WRITE_BUFFER_BLOCK_COUNT;
		buffers[i].it_size = WRITE_BUFFER_BLOCK_SIZE;
		buffers[i].prod_pos = 0;
		buffers[i].cons_pos = buffers[i].nitems-1;
		buffers[i].nlast_data = -1;
		if (pthread_mutex_init(&buffers[i].lock,NULL)){
			dprint(WARN,"Error in mutex initialization\n");
			return NULL;
		}
		if (pthread_cond_init(&buffers[i].empty_cv,NULL)){
			dprint(WARN,"Error in conditional variable initialization\n");
			return NULL;
		}
	}	

	return buffers;
}

void free_buffers(struct buffer * buffers, int nbuffers){
	for (int i=0; i<nbuffers; i++){
		pthread_mutex_destroy(&buffers[i].lock);
		pthread_cond_destroy(&buffers[i].empty_cv);
		free(buffers[i].buffer);
		free(buffers[i].datalens);
	}
	free(buffers);
}
