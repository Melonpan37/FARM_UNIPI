#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include <error.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <time.h>
#include <fcntl.h>
#include "../headers/cqueue.h"
#include "../headers/utils.h"

#include "../headers/debug_flags.h"

/* 
* EFFECT    : Segnala alla coda di priotrità la terminazione di un thread (più precisamente segnala la sua uscita dalla pool dei thread)
*             Se specificato kill_process, se qualcosa fallisce allora un segnale viene inviato al master per la terminazione del processo
*/
void signal_termination(struct cqueue* cq, char kill_process){
    int err;
    if( (err = pthread_mutex_lock(&(cq->mtx))) ){ 
        fprintf(stderr, "(thread %ld) WORKER -> signal_termination : mutex_lock error", pthread_name());
        if(kill_process) if(kill(getpid(), SIGINT)) fprintf(stderr, "(thread %ld) WORKER -> signal_termination : kill error", pthread_name());
        return; 
    }
    cq->left_pool++;
    if( (err = pthread_mutex_unlock(&(cq->mtx))) ){
        fprintf(stderr, "(thread %ld) WORKER -> signal_termination : mutex_unlock error", pthread_name());
        if(kill_process) if(kill(getpid(), SIGINT)) fprintf(stderr, "(thread %ld) WORKER -> signal_termination : kill error", pthread_name());
    } 
}

//EFFECT : reads a file as a collection of long int and sums the long values multiplied by their indices
//MODIFIES : *res
//RETURNS : 0 on success, x > 0 on system call errors
char decode_dat(const char* path, long* res){
   int fd = open(path, O_RDONLY);
   if(fd == -1){ fprintf(stderr, "WORKER : open"); return 1; }

   *res = 0;
   long i = 0; 
   long buf = 0;
   size_t read_res = 0;
   while( (read_res = readn(fd, (long*) &buf, sizeof(long))) > 0 ){
      *res += i*buf;
      ++i;
   }
   if(read_res == -1){ fprintf(stderr, "WORKER : read"); return 2; }
   if(close(fd) == -1){ fprintf(stderr, "WORKER : close"); return 3; }
   return (char) 0;
}


void* worker(void* arg){
    int err;

    //recupero la coda
    struct cqueue* cq = (struct cqueue*) arg;
    
    //====CONNESSIONE COL SERVER====
    //inizializzazione socket
    int sock_fd;
    struct sockaddr_un sa;
    strncpy(sa.sun_path, "./farm.sck", 108);
    sa.sun_family = AF_UNIX;
    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    //contatore attesa socket non inizializzata
    int count = 0;      //numero tentativi di connessione
    struct timespec t;  //attesa fra tentativi
    t.tv_sec = 1;
    t.tv_nsec = 0;

    //si effettua una nanosleep per ragioni puramente estetiche, altrimenti è altamente probabile che la scoket non sia inizializzata
    //e quindi si produrrebbero stampe di errore
    nanosleep(&t, NULL);

    //connessione
    while( (connect(sock_fd, (struct sockaddr*) &sa, sizeof(sa))) == -1 ){
        err = errno;
        //errore socket non inizializzata
        if(err == ENOENT){printf("(thread %ld) WORKER : socket uninitialized\n", pthread_name()); nanosleep(&t, NULL);}
        //errore di altro tipo (grave)
        else{ fprintf(stderr, "(thread %ld) WORKER : connect error %d\n", pthread_name(), err); signal_termination(cq, 1); return (void*) 1; }
        count++;
        if(count == 10){ //limite tentativi di connessione raggiunto
            fprintf(stderr, "(thread %ld) WORKER : reached limit to connection attempts with server\n", pthread_name());
            kill(getpid(), SIGINT);     //segnala a master che deve terminare
            signal_termination(cq, 0);  //segnala l'uscita dalla coda
            return (void*) 1;         
        } 
    }
    #ifdef DEBUG
    printf("(pthread %ld) WORKER : connected to server\n", pthread_name());
    #endif

    //====COMUNICAZIONE COL SERVER====
    while(1){        
        char* path = NULL;
        if(!cqueue_pop(cq, &path)){ close(sock_fd); kill(getpid(), SIGINT); signal_termination(cq, 0); return (void*) 1; }
        //controlla che non sia stato inserito l'elemento di terminazione in coda
        if(!strncmp(path, "\0", 1)){
            #ifdef DEBUG
            printf("(pthread %ld) WORKER : got termination element from queue\n", pthread_name());
            #endif
            free(path);
            break; 
        }
        //initialize write buffer
        char buf[256];
        memset(buf, '\0', 256);

        //-----SEND LONG----
        //leggo long dal .dat
        long long_data = 0;
        if(decode_dat(path, &long_data)){ free(path); close(sock_fd); kill(getpid(), SIGINT); signal_termination(cq, 0); return (void*) 1; }
        //scrittura nel buffer (un long non può avere più di 254 cifre)
        snprintf(buf, 254, "%ld", long_data);
        strncat(buf, " ", 1); //il protocollo di comunicazione usa ' ' come indicazione di fine long
        //invio del long
        size_t write_res;
        write_res = writen(sock_fd, buf, strlen(buf)*sizeof(char));
        //controllo errore scrittura
        if(write_res == -1){ 
            fprintf(stderr, "(thread %ld) WORKER : write", pthread_name());
            //errore (SIGPIPE)
            if(errno == EPIPE){free(path); close(sock_fd); signal_termination(cq, 1); return (void*) 1;} //non richiede terminazione generale
            //altro errore
            else {free(path); close(sock_fd); kill(getpid(), SIGINT); signal_termination(cq, 0); return (void*) 1; } //richiede terminazione generale
        }
        //-----SEND PATH----
        memset(buf, '\0', 256);
        strncpy(buf, path, 255); //max path len = 255
        write_res = writen(sock_fd, (void*) buf, strlen(buf)*sizeof(char));
        //controllo errore scrittura
        if(write_res == -1){ 
            fprintf(stderr, "(thread %ld) WORKER : write", pthread_name());
            //errore (SIGPIPE)
            if(errno == EPIPE){free(path); close(sock_fd); signal_termination(cq, 1); return (void*) 1;} //non richiede terminazione generale
            //altro errore
            else {free(path); close(sock_fd); kill(getpid(), SIGINT); signal_termination(cq, 0); return (void*) 1; } //richiede terminazione generale
        }

        //-----SEND MESSAGE COMPLETITION----
        memset(buf, '\0', 256);
        write_res = writen(sock_fd, (void*) "\n", sizeof(char));
        //controllo errore scrittura
        if(write_res == -1){ 
            fprintf(stderr, "(thread %ld) WORKER : write", pthread_name());
            //errore (SIGPIPE)
            if(errno == EPIPE){free(path); close(sock_fd); signal_termination(cq, 1); return (void*) 1;} //non richiede terminazione generale
            //altro errore
            else {free(path); close(sock_fd); kill(getpid(), SIGINT); signal_termination(cq, 0); return (void*) 1; } //richiede terminazione generale
        }

        free(path);
    }

    //terminazione con successo
    err = 0;
    if(close(sock_fd)){fprintf(stderr, "(thread %ld) WORKER : error closing server socket\n", pthread_name()); err = 1; }
    signal_termination(cq, 1);
    if(err) return (void*) 1;
    else return (void*) 0;

}
