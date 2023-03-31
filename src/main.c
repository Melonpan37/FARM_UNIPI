#define _XOPEN_SOURCE 700
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <error.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "../headers/opts.h"
#include "../headers/utils.h"

#include "../headers/cqueue.h"

#include "../headers/master.h"
#include "../headers/worker.h"

#include "../headers/debug_flags.h"

int main(int argc, char** argv){
    int ppid = getpid();
    printf("MAIN PID : %d\n", ppid);
    int err;

    #ifdef DEBUG
    printf("MAIN : parsing opts\n");
    #endif

    //====PARSING OPTS A RIGA DI COMANDO====
    //inizializzazione valori di default delle opt
    int nthread             = 4;        //dimensione pool di threads
    int qlen                = 8;        //dimensione coda concorrente
    char** dnames           = NULL;     //filepaths
    int dnames_size         = 0;        //dimensione filepaths
    struct timespec delay;              //delay invio richieste nella coda concorrente
    memset(&delay, 0, sizeof(struct timespec));
    //parsing delle opts
    err = (int) master_getopt(argc, argv, &nthread, &qlen, &dnames, &dnames_size, &delay);
    //controllo errori nelle opts (fallimento immediato)
    if(err){ master_getopt_error(err, argv); FREE_DNAMES(&dnames, dnames_size); exit(EXIT_FAILURE); }
    //controllo che siano stati passati dei filepath come opts
    if(!dnames_size){ fprintf(stderr, "%s requires at least one file path \n", argv[0]); exit(EXIT_FAILURE); }

    if(delay.tv_sec >= 1) fprintf(stderr, "\n(warning) MAIN : ritardo di invio richieste ha un valore elevato di %ld secondo/i.\n\n", delay.tv_sec);

    #ifdef DEBUG
    printf("MAIN : master_getopt results :\n");
    printf("number of threads : %d\n", nthread);
    printf("queue len : %d\n", qlen);
    printf("delay : %ld\n", delay.tv_nsec);
    printf("number of file paths : %d\n", dnames_size);
    printf("files:\n");
    for(int i = 0; i < dnames_size; i++) printf("dnames[%d] : %s\n", i, dnames[i]);
    printf("\n");
    #endif

    //====MASCHERAMENTO SEGNALI====
    sigset_t sigset;
    if( sigemptyset(&sigset) )      { fprintf(stderr, "MAIN -> sigemptyset error.\n"); FREE_DNAMES(&dnames, dnames_size); exit(EXIT_FAILURE); }
    if( sigaddset(&sigset, SIGINT) ){ fprintf(stderr, "MAIN -> sigaddset error.\n"); FREE_DNAMES(&dnames, dnames_size); exit(EXIT_FAILURE); }
    if( sigaddset(&sigset, SIGHUP) ){ fprintf(stderr, "MAIN -> sigaddset error.\n"); FREE_DNAMES(&dnames, dnames_size); exit(EXIT_FAILURE); }
    if( sigaddset(&sigset, SIGQUIT) ){ fprintf(stderr, "MAIN -> sigaddset error.\n"); FREE_DNAMES(&dnames, dnames_size); exit(EXIT_FAILURE); }
    if( sigaddset(&sigset, SIGTERM) ){ fprintf(stderr, "MAIN -> sigaddset error.\n"); FREE_DNAMES(&dnames, dnames_size); exit(EXIT_FAILURE); }
    if( sigaddset(&sigset, SIGUSR1) ){ fprintf(stderr, "MAIN -> sigaddset error.\n"); FREE_DNAMES(&dnames, dnames_size); exit(EXIT_FAILURE); }
    if( sigaddset(&sigset, SIGPIPE) ){ fprintf(stderr, "MAIN -> sigaddset error.\n"); FREE_DNAMES(&dnames, dnames_size); exit(EXIT_FAILURE); }
    if( (err = pthread_sigmask(SIG_SETMASK, &sigset, NULL)) ){ fprintf(stderr, "MAIN -> pthread_sigmask : error %d\n", err); FREE_DNAMES(&dnames, dnames_size); exit(EXIT_FAILURE); }
    
    //====CREAZIONE PROCESSO COLLECTOR====
    pid_t collector_pid = fork();
    switch(collector_pid){
        //controllo errori fork
        case(-1) : { perror("MAIN -> fork"); FREE_DNAMES(&dnames, dnames_size); exit(EXIT_FAILURE); }
        //fork eseguita
        case(0) : {
            //lancio del processo collector
            execl("./collector", "./collector", (char*) NULL);
            //errore execl
            perror("(unexecuted) SERVER -> execl");
            //invio segnale terminazione al processo padre
            if(kill(ppid, SIGINT) == -1) perror("(unexecuted) SERVER -> kill");
            //uscita dal processo figlio
            exit(EXIT_FAILURE);
        }
    }


    //====CREAZIONE THREAD WORKER====
    //creazione coda concorrente
    struct cqueue* cq = cqueue_init(qlen);
    //controllo errore (allocazione) coda concorrente
    if(!cq){ fprintf(stderr, "MAIN : can't initialize cqueue\n"); kill(collector_pid, SIGUSR2); FREE_DNAMES(&dnames, dnames_size); return -1; }

    //inizializzazione thread workers
    pthread_t workers[nthread];
    for(size_t i = 0; i < nthread; i++) if( (err = pthread_create(&workers[i], NULL, worker, (void*)cq)) ){
        //errore inizializzazione thread worker
        fprintf(stderr, "MAIN -> pthread_create #%ld : error %d\n", i, err);
        //terminazione thread worker già inizializzati
        for(size_t j = 0; j < i; j++) if(!cqueue_immediate_termination(cq)){
            fprintf(stderr, "MAIN -> error on cqueue_immediate_termination\n"); 
            kill(collector_pid, SIGUSR2);
            FREE_DNAMES(&dnames, dnames_size);
            exit(EXIT_FAILURE);
        } 
        for(size_t j = 0; j < i; j++){ pthread_join(workers[j], NULL); }
        //liberazione memoria e uscita
        kill(collector_pid, SIGUSR2);
        FREE_DNAMES(&dnames, dnames_size);
        cqueue_free(cq);
        exit(EXIT_FAILURE);
    }
    
    //====CREAZIONE THREAD MASTER (rimane sul main thread)====
    struct master_args margs;
    memset(&margs, 0, sizeof(struct master_args));
    margs.collector_pid     = collector_pid;    //pid di collector
    margs.cq                = cq;               //coda concorrente condivisa con workers
    margs.paths             = dnames;           //lista file paths 
    margs.paths_size        = dnames_size;      //dimensione lista file paths
    margs.pool_size         = nthread;          //dimensione pool di workers
    margs.delay             = delay;            //ritardo richieste ai workers
    margs.worker_ids        = workers;          //pthread_t degli worker (la dimensione è pool_size)

    int* res = (int*) master((void*)&margs);
    if(res == (int*)1){
        fprintf(stderr, "MAIN : fatal error on master thread\n");
        FREE_DNAMES(&dnames, dnames_size);
        cqueue_free(cq);
        return -1;
    }
    

    cqueue_free(cq);
    FREE_DNAMES(&dnames, dnames_size);

    while(waitpid(collector_pid, NULL, 0) == -1){
        if(errno == EINTR) continue;
        perror("MAIN : wait"); 
        exit(EXIT_FAILURE);
    }
    #ifdef DEBUG
    fprintf(stdout, "terminato con successo\n");
    #endif
    exit(EXIT_SUCCESS);
}
