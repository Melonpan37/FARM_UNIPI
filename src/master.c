#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <error.h>

#include "../headers/cqueue.h" 
#include "../headers/master.h"
#include "../headers/utils.h"

#include "../headers/debug_flags.h"

//HANDLER SEGNALI DI TERMINAZIONE
volatile sig_atomic_t sig_term_received = 0;
void sighandler(int sig){
    #ifdef DEBUG
    write(2, "MASTER : received termination signal while sleeping\n", 53*sizeof(char));
    #endif
    sig_term_received = 1;
}

//EFFECT : controlla che i worker siano ancora attivi
//RETURNS : 0 if success, 1 if error
char check_worker_pool(struct cqueue* cq, size_t pool_size){
    int err;
    //errore lock
    if( (err = pthread_mutex_lock(&(cq->mtx))) ){
        fprintf(stderr, "MASTER -> check_worker_pool : mutex_lock error\n");
        return 1;
    }
    //worker tutti terminati
    if(cq->left_pool >= pool_size){
        //errore unlock
        if( (err = pthread_mutex_unlock(&(cq->mtx))) ) fprintf(stderr,  "MASTER -> check_worker_pool : mutex_unlock error\n");
        return 1;
    }
    //errore unlock
    if( (err = pthread_mutex_unlock(&(cq->mtx))) ) fprintf(stderr,  "MASTER -> check_worker_pool : mutex_unlock error\n");
    //ci sono worker ancora attivi
    return 0;
}

//rimuove i segnali pendenti di siglist (se ci sono) dalla lista pendenti
//RETURNS : 0 se non ci sono segnali pendenti, 1 se ci sono, 2 se errore
char check_pending_signals(int siglist[], size_t siglist_size){
    int err;
    //costruzione set per segnali pendenti
    sigset_t pending;
    if( (err = sigemptyset(&pending)) == -1){ fprintf(stderr,  "MASTER -> sigemptyset"); return 2; }
    //recupero set dei segnali pendenti
    if( (err = sigpending(&pending) ) == -1){ fprintf(stderr,  "MASTER -> sigpending"); return 2; }
    char res = 0;
    //controllo se il set dei segnali pendenti contiene elementi si siglist
    for(size_t i = 0; i < siglist_size; i++){
        //controllo se il set di segnali pendenti contiene l'i-esimo segnale di siglist
        switch(sigismember(&pending, siglist[i]) ){
            case(0)     : break; //not pending : continue
            case(1)     : {      //pending     : remove it from pending
                int ignore;
                res = 1; 
                //rimozione dai pending
                sigset_t removeset;
                if( (err = sigemptyset(&removeset))           == -1){ fprintf(stderr,  "MASTER -> sigemptyset"); return 2; }
                if( (err = sigaddset(&removeset, siglist[i])) == -1){ fprintf(stderr,  "MASTER -> sigaddset"); return 2; }
                if( (err = sigwait(&removeset, &ignore))           ){ fprintf(stderr,  "MASTER -> sigwait"); return 2; } 
                break;
            }
            //errore sigismember
            case(-1)    : { fprintf(stderr, "MASTER -> sigismember"); return 2; }
        }
    }
    //ritorna 1 se c'è un segnale pending, 0 altrimenti
    return res;
}

//da usare in caso di errore (best effort per la terminazione immediata)
void terminate_workers_immediate(struct cqueue* cq, pthread_t pool[], size_t pool_size){
    int err;
    for(size_t i = 0; i < pool_size; i++) if(!cqueue_immediate_termination(cq)){
        fprintf(stderr, "MASTER -> terminate_workers_immediate : error on cqueue_immediate_termination\n");
        //meglio non procedere alla join se non si riesce a garantire la terminazione dei thread
        return;
    }
    for(size_t i = 0; i < pool_size; i++) if( (err = pthread_join(pool[i], NULL)) ) {
        fprintf(stderr, "MASTER -> terminate_workers_immediate : error on pthread_join %d\n", err);
        continue;
    }
}


//RETURNS : 0 on success, 1 on error (si assicura la terminazione degli worker)
void* master(void* arg){
    int err = 0;
    //recupero argomento
    struct master_args* args = (struct master_args*) arg;
    struct cqueue* cq = args->cq;
    
    //====INIZIALIZZAZIONE MASCHERA DEI SEGNALI====
    int sigtermlist[4] = {SIGHUP, SIGINT, SIGQUIT, SIGTERM};
    int sigprintlist[1] = {SIGUSR1};
    sigset_t sig_term_set;
    err += sigemptyset(&sig_term_set);
    err += sigaddset(&sig_term_set, SIGINT);
    err += sigaddset(&sig_term_set, SIGHUP);
    err += sigaddset(&sig_term_set, SIGQUIT);
    err += sigaddset(&sig_term_set, SIGTERM);
    if(err){ //controllo errori sigaddset
        fprintf(stderr, "MASTER : error on sigemptyset/sigaddset\n"); 
        terminate_workers_immediate(cq, args->worker_ids, args->pool_size); 
        kill(args->collector_pid, SIGUSR2);
        return (void*)1;
    }

    //====ISTALLAZIONE SIGHANDLER PER SEGNALI DURANTE NANOSLEEP====
    struct sigaction sigact;
    memset(&sigact, 0, sizeof(struct sigaction));
    sigact.sa_handler = sighandler;
    err = 0;
    err += sigaction(SIGINT, &sigact, NULL);
    err += sigaction(SIGHUP, &sigact, NULL);
    err += sigaction(SIGQUIT, &sigact, NULL);
    err += sigaction(SIGTERM, &sigact, NULL);
    if(err){
        fprintf(stderr, "MASTER : error on sigaction\n"); 
        terminate_workers_immediate(cq, args->worker_ids, args->pool_size);
        if(kill(args->collector_pid, SIGUSR2))fprintf(stderr, "MASTER : kill error");
        return (void*) 1;
    }

    //====RIEMPIMENTO DELLA CODA====
    for(int i = 0; i < args->paths_size; i++){
        //====CHECK PENDING TERMINATION SIGNALS====
        if(check_pending_signals(sigtermlist, 4)){
            printf("MASTER : received termination signal. Initiating delayed termination.\n");
            break;                
        }
        //====CHECK PENDING PRINT SIGNALS====
        if(check_pending_signals(sigprintlist, 1)){
            printf("MASTER : received print signal.\n");
            if(kill(args->collector_pid, SIGUSR1) == -1){
                fprintf(stderr, "MASTER : error on kill\n");
                terminate_workers_immediate(cq, args->worker_ids, args->pool_size);
                if(kill(args->collector_pid, SIGUSR2))fprintf(stderr, "MASTER : kill error");
                return (void*) 1;
            }
        }
        //====CONTROLLO SE CI SONO THREAD ATTIVI====
        if(check_worker_pool(cq, args->pool_size)){break;} //master restituirà errore perchè ci sono errori nei valori di ritorno dei thread

        //====DELAY INVIO RICHIESTE====
        if(args->delay.tv_sec || args->delay.tv_nsec){
            //smascheramento segnali terminazione
            if(pthread_sigmask(SIG_UNBLOCK, &sig_term_set, NULL)){
                fprintf(stderr, "MASTER : error on pthread_sigmask\n");
                terminate_workers_immediate(cq, args->worker_ids, args->pool_size);
                if(kill(args->collector_pid, SIGUSR2))fprintf(stderr, "MASTER : kill error");
            }
            //ricevuto segnale tra smascheramento e nanosleep
            if(sig_term_received) break;
            //sleep
            nanosleep(&(args->delay), NULL);
            //mascheramento segnali
            if(pthread_sigmask(SIG_BLOCK, &sig_term_set, NULL)){
                fprintf(stderr, "MASTER : error on pthread_sigmask\n");
                terminate_workers_immediate(cq, args->worker_ids, args->pool_size);
                if(kill(args->collector_pid, SIGUSR2))fprintf(stderr, "MASTER : kill error");
            }
            //master ha ricevuto un segnale di terminazione mentre era in sleep
            if(sig_term_received) break;
        }

        //====INVIO RICHIESTA====
        switch( (err = cqueue_append(cq, args->paths[i])) ){
            case(0) : {         //errore fatale
                fprintf(stderr, "MASTER : error on cqueue_append\n");
                terminate_workers_immediate(cq, args->worker_ids, args->pool_size);
                if(kill(args->collector_pid, SIGUSR2))fprintf(stderr, "MASTER : kill error");
                return (void*)1;
            }
            case(1) : break;    //operazione riuscita
            case(2) : --i;      //timeout
        }
    }

    //====TERMINAZIONE ESECUZIONE====
    //invio elementi di terminazione in testa alla coda
    #ifdef DEBUG
    printf("\nMASTER : begginning delayed termination\n");
    #endif
    
    //notifica la terminazione agli worker
    for(int i = 0; i < args->pool_size; i++) if(!cqueue_delayed_termination(cq)){ //errore notifica terminazione
        fprintf(stderr, "MASTER : error on cqueue_delayed_termination");
        if(kill(args->collector_pid, SIGUSR2))fprintf(stderr, "MASTER : kill error");
        return (void*) 1;
    }
    //attesa terminazione workers 
    for(int i = 0; i < args->pool_size; i++){
        int* join_error = NULL;
        err = pthread_join(args->worker_ids[i], (void*) join_error);
        if(err){ fprintf(stderr,  "MASTER : pthread_join error\n"); continue; }
        if(join_error == (void*) 1 ) fprintf(stderr, "MASTER : WORKER returned with error\n");
    }
    //segnala chiusura a COLLECTOR
    if(kill(args->collector_pid, SIGUSR2)) {fprintf(stderr, "MASTER : error on kill :%d\n", errno); return (void*) 1;}
    return (void*) 0;
}
