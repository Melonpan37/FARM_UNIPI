#ifndef _MASTER_H_
#define _MASTER_H_

#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>

struct master_args {
    struct cqueue* cq;
    pid_t collector_pid;
    char** paths;
    size_t paths_size;
    size_t pool_size;
    struct timespec delay;
    pthread_t* worker_ids;
};

//EFFECT : controlla che i worker siano ancora attivi
//RETURNS : 0 if success, 1 if error
char check_worker_pool(struct cqueue* cq, size_t pool_size);

//EFFECT : controlla se i segnali presenti in siglist sono pendenti e li rimuove dalla lista pendenti
//RETURNS : 0 se non ci sono segnali pendenti, 1 se ci sono, 2 se errore
char check_pending_signals(int siglist[], size_t siglist_size);

//richiede la terminazione al server
//RETURNS : 0 on success, 1 on error
char close_communication(int server_sock_fd, pid_t server_pid);

void* master(void* arg);

#endif