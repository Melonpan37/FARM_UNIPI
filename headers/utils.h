#ifndef UTILS_H
#define UTILS_H

#include <sys/types.h>

#ifndef FREED
#define FREED

#define FREE1D(ptr) free(ptr);
#define ERROR1D(msg, ptr)       \
    perror(msg);                \
    FREE1D(ptr);                

#define FREE2D(arr, arrsize)                                    \
    for(size_t arrindex = 0; arrindex < arrsize; arrindex++)    \
        free(arr[arrindex]);                                    \
    free(arr);

#define ERROR2D(msg, arr, arrsize)  \
    perror(msg);                    \
    FREE2D(arr, arrsize)            
#endif     

size_t max_size(size_t, size_t);
char is_number(const char* string, long* num);
char file_is_readable(const char* path);
char file_has_extension(const char* path, const char* ext);
ssize_t merge(char*** dest, const unsigned long dest_size, char** src, const unsigned long src_size);

char is_dir(const char* path);

char is_reg(const char* path);

ssize_t writen(int fd, void *ptr, size_t n);

ssize_t readn(int fd, void *ptr, size_t n);

//EFFECT : converte milliseconds in un timespec equivalente (non superando il limite di 999999999 per ogni campo come richiesto da nanosleep) 
struct timespec milliseconds_timespec(long milliseconds);


#endif