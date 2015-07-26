#ifndef BUFFER_H
#define BUFFER_H

#include <pthread.h>
#include "netstream.h"

#define BUF_END_DATA -1
#define BUF_KILL -2

int buffer_insert(struct buffer * buf,char * data,ssize_t ndata);
char * buffer_cons_data_pointer(struct buffer * buf);
int buffer_after_delete(struct buffer * buf);
struct buffer * create_buffers(int nbuffers);
void free_buffers(struct buffer * buffers, int nbuffers);

#endif
