#ifndef OPTS_H
#define OPTS_H

#include <dirent.h>
#include <time.h>

#define FREE_DNAMES(dnames, dnames_size) \
    for(int k = 0; k < dnames_size; k++){\
        free((*dnames)[k]);              \
    }                                    \
    free(*dnames);                       \


char file_is_valid(char* path);
char master_getopt(int argc, char** argv, int* nthreads, int* qlen, char*** dnames, int* dnames_size, struct timespec* delay);
void master_getopt_error(short unsigned int error, char** argv);

char filter_dat(struct dirent* entry, const char* path);

#endif