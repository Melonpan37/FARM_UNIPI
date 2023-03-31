#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

size_t max_size(const size_t size1, const size_t size2){
    if(size1>size2) return size1;
    else return size2;
}

//RETURNS : 1 if "string" represents a long, 0 otherwise, 2 if overflow
//if num != NULL : stores the value of string (as a long) into num
char is_number(const char* string, long* num){
    char* endptr;
    long res = strtol(string, &endptr, 10);
    char overflow = 0;
    if(errno == ERANGE){overflow = 1;}
    if
    (
        //endptr è null
        !endptr 
        ||
        //string non è un numero
        !strncmp(string, endptr, strlen(string))
        ||
        //string contiene sia numeri che caratteri
        !*endptr == '\0'
    ){
        if(num) *num = 0;
        return 0;
    }
    if(num) *num = res;
    if(overflow) return 2;
    return 1;
}

//RETURNS : 1 if "path" is the path of a readable file, 0 otherwise
char file_is_readable(const char* path){
    if(access(path, R_OK)) return 0;
    return 1;
}

//RETURNS : 1 if "path" is the path of a file that ends with '.ext', 0 otherwise
char file_has_extension(const char* path, const char* ext){
    char* last_occurrence;
    if(!(last_occurrence = strchr(path, '.'))) return 0;
    //controllo che il path non termini con . e basta
    if(strlen(last_occurrence) <= 1) return 0; 
    //controllo che la sezione di stringa successiva a . nel path abbia la stessa lunghezza di ext
    if(strlen(last_occurrence) != strlen(ext)+1) return 0;
    //uso strcmp invece di strncmp perchè ho già controllato che le due stringhe hanno lunghezza uguale
    if(strcmp(last_occurrence+1, ext)) return 0;
    return 1;
}

//merges two string arrays in one
//*dest deve essere deallocato successivamente, ma src no
//MODIFIES : dest, src : 
//RETURNS : the new size of dest if success, -1 otherwise
//!!!ATTENZIONE!!! src viene deallocato, ma i suoi elementi no, vengono solo dereferenziati per essere puntati dalle celle di dest
ssize_t merge(char*** dest, const size_t dest_size, char** src, const size_t src_size){
    size_t new_size = dest_size+src_size;
    
    char** temp = NULL;
    temp = realloc(*dest, sizeof(char*)*new_size);
    if(!temp){perror("merge -> realloc"); return -1;}
    *dest = temp;
    for(size_t i = 0; i < src_size; i++)
        (*dest)[i+dest_size] = src[i];

    free(src);
    src = NULL;
    return new_size;
}


char is_dir(const char* path){
    struct stat sb;
    if(stat(path, &sb)){perror("is_dir -> stat"); return (char) 0; }
    if(S_ISDIR(sb.st_mode)) return (char) 1;
    return (char) 0; 
}

char is_reg(const char* path){
    struct stat sb;
    if(stat(path, &sb)){perror("is_reg -> stat"); return (char) 0; }
    if(S_ISREG(sb.st_mode)) return (char) 1;
    return (char) 0; 
}

ssize_t writen(int fd, void *ptr, size_t n) {  
   size_t   nleft;
   ssize_t  nwritten;
 
   nleft = n;
   while (nleft > 0) {
     if((nwritten = write(fd, ptr, nleft)) < 0) {
        if (nleft == n) return -1; /* error, return -1 */
        else break; /* error, return amount written so far */
     } else if (nwritten == 0) break; 
     nleft -= nwritten;
     ptr   += nwritten;
   }
   return(n - nleft); /* return >= 0 */
}

ssize_t readn(int fd, void *ptr, size_t n) {  
   size_t   nleft;
   ssize_t  nread;
 
   nleft = n;
   while (nleft > 0) {
     if((nread = read(fd, ptr, nleft)) < 0) {
        if (nleft == n) return -1; /* error, return -1 */
        else break; /* error, return amount read so far */
     } else if (nread == 0) break; /* EOF */
     nleft -= nread;
     ptr   += nread;
   }
   return(n - nleft); /* return >= 0 */
}


struct timespec milliseconds_timespec(unsigned long milliseconds){
    struct timespec t;
    memset(&t, 0, sizeof(struct timespec));
    while(milliseconds >= 1000){
        t.tv_sec++;
        milliseconds -= 1000;
    }
    unsigned long nanosec = milliseconds*1000000L;
    if(nanosec > 999999999){nanosec = 999999999L;}
    t.tv_nsec = (long) nanosec;
    return t;
}
