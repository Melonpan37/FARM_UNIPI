#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <cqueue.h> 

#include "../headers/debug_flags.h"

//EFFECT : restituisce il nome del thread(pthread_self) oppure le ultime quattro cifre di questo se PTSN è definito
long pthread_name(){
    #ifdef PTSN
    return pthread_self()%10000;
    #endif
    return pthread_self();
}

//EFFECT : inizializza (alloca) una nuova cqueue* di dimensione max_size
//REQUIRES : max_size != 0
//RETURNS : cqueue inizializzata on success, NULL on failure
struct cqueue* cqueue_init(size_t max_size){
    if(!max_size) return NULL;
    //initialize return value
    struct cqueue* cq = malloc(sizeof(struct cqueue));
    //check malloc
    if(cq == NULL){ fprintf(stderr, "queue_init -> malloc\n"); return NULL; }
    
    //inizializza valore campi
    memset(cq, 0, sizeof(struct cqueue));
    cq->max_size = max_size;

    //inizializzazione meccanismi di concorrenza
    int error;
    if( (error = pthread_mutex_init(&(cq->mtx), NULL)) ){
        //sono per lo più errori fatali
        fprintf(stderr, "pthread_mutex_init : error %d\n", error);  //error msg
        FREE2D(cq->items, cq->size);                                //free items
        free(cq);                                                   //free queue
        return NULL;                                            
    }
    if( (error = pthread_cond_init(&(cq->empty_cond), NULL)) ){
        //sono per lo più errori fatali
        fprintf(stderr, "pthread_cond_init : error %d\n", error);    //error msg
        FREE2D(cq->items, cq->size);                                 //free items
        pthread_mutex_destroy(&(cq->mtx));                           //free lock
        free(cq);                                                    //free queue
        return NULL;    
    }
    if( (error = pthread_cond_init(&(cq->full_cond), NULL)) ){
        //sono per lo più errori fatali
        fprintf(stderr, "pthread_cond_init : error %d\n", error);    //error msg
        FREE2D(cq->items, cq->size);                                 //free items
        pthread_mutex_destroy(&(cq->mtx));                           //free lock
        pthread_cond_destroy(&(cq->empty_cond));                     //free cond
        free(cq);                                                    //free queue
        return NULL;    
    }

    //andato a buon fine
    return cq;
}

//add str element to cq->items
//MODIFIES : cq->items, cq->size
//EFFECT : alloca spazio per il nuovo elemento di cq->items, copia str in cq->items[nuova_size]
//REQUIRES : cq != NULL, str != NULL
//RETURNS : 0 se fallimento (errore fatale tipo allocazione o cq = NULL), 1 se successo, 2 se timeout attesa condizione
//!!!ATTENZIONE!!! : str viene copiata in fondo a items, quindi cq->items[size-1] deve essere deallocato (str non viene modificata)
char cqueue_append(struct cqueue* cq, const char* str){
    if(!cq){ fprintf(stderr, "Error : trying to add %s to a NULL cqueue\n", str); return (char) 0; }

    //attesa condizione coda piena
    PT_DEBUG("pthread %ld trying to acquire lock on cqueue_append -> args : %s\n", pthread_name(), str);
    if(pthread_mutex_lock(&(cq->mtx))){ fprintf(stderr, "cqueue_append error -> mutex lock\n"); return 0; }
    while(cq->max_size == cq->size){
        PT_DEBUG("pthread %ld waiting condition (full) -> args : %s\n", pthread_name(), str);
        struct timespec timeout;
        memset(&timeout, 0, sizeof(struct timespec));
        timeout.tv_sec = 2;
        int err;
        //attesa condizione
        if( (err = pthread_cond_timedwait(&(cq->full_cond), &(cq->mtx), &timeout)) ) {
            //timeout
            if(err == ETIMEDOUT) {
                PT_DEBUG("pthread %ld timeout on condition wait (full) -> args : %s\n", pthread_name(), str); 
                if(pthread_mutex_unlock(&(cq->mtx))) {fprintf(stderr, "cqueue_append -> pthread_mutex_unlock : fatal error\n");}
                return 2;
            }
            //errore
            fprintf(stderr, "cqueue_append : error on cond wait (full)\n"); 
            return 0;
        }
        PT_DEBUG("pthread %ld condition verified (full) -> args : %s\n", pthread_name(), str);
    }
    PT_DEBUG("pthread %ld acquired lock on cqueue_append -> args : %s\n", pthread_name(), str);

    //copia str in un nuovo array
    size_t str_len = strlen(str)+1;
    char* new_item = malloc(sizeof(char)*str_len);
    if( !new_item ){ pthread_mutex_unlock(&(cq->mtx)); fprintf(stderr, "cqueue_add error -> malloc"); return (char)0; }
    new_item = memset(new_item, '\0', str_len);
    new_item = strncpy(new_item, str, strlen(str));
    
    //riallocazione memoria della queue
    char** temp = realloc(cq->items, sizeof(char*)*(++cq->size));
    if(!temp){ fprintf(stderr, "cqueue_add error -> realloc\n"); free(new_item); pthread_mutex_unlock(&(cq->mtx)); return (char) 0; }
    cq->items = temp;
    //assegnamento nuovo elemento nella queue
    cq->items[cq->size-1] = new_item;

    //segnale condizione coda non vuota
    if(pthread_cond_signal(&(cq->empty_cond))){ //controllo errore (fatale) 
        fprintf(stderr, "cqueue_append -> cond signal (empty)\n"); 
        pthread_mutex_unlock(&(cq->mtx));
        return (char) 0; 
    }

    //rilascio lock
    PT_DEBUG("pthread %ld about to release lock on cqueue_append -> args : %s\n", pthread_name(), str);
    if(pthread_mutex_unlock(&(cq->mtx))){ fprintf(stderr, "cqueue_append -> pthread_mutex_unlock : fatal error\n"); return (char) 0; }
    PT_DEBUG("pthread %ld released lock on cqueue_append -> args : %s\n", pthread_name(), str);

    return (char) 1;
}

//add str element to cq->items
//MODIFIES : cq->items, cq->size
//EFFECT : copia il contenuto di cq->items[cq->size-1] in output e lo rimuove (free) da items
//REQUIRES : cq != NULL
//RETURNS : 0 se fallimento (errore fatale tipo allocazione o cq = NULL), 1 altrimenti
//!!!ATTENZIONE!!! *outstr viene allocato, quindi deve essere liberato
char cqueue_pop(struct cqueue* cq, char** outp){
    if(!cq){ fprintf(stderr, "Error : trying to pop from an NULL cqueue\n"); return (char) 0; }

    PT_DEBUG("pthread %ld trying to acquire lock on cqueue_pop %s\n", pthread_name(), " ");
    if(pthread_mutex_lock(&(cq->mtx))){fprintf(stderr, "cqueue_pop -> pthread_mutex_lock : fatal error\n"); return (char) 0; }
    while(!cq->size) {
        PT_DEBUG("pthread %ld entering condition wait (empty) %s\n", pthread_name(), " ");
        //condition wait su coda vuota
        if(pthread_cond_wait(&(cq->empty_cond), &(cq->mtx))){
            //errore pthread_cond_wait (considerato fatale) 
            fprintf(stderr, "cqueue_pop -> cond wait (empty) : fatal error\n"); 
            pthread_mutex_unlock(&(cq->mtx)); 
            return (char) 0; 
        }
        //TODO(?) : add ETIMEDOUT -> gestione timeout (?)
        PT_DEBUG("pthread %ld returned from condition wait %s\n", pthread_name(), " ");
    }
    PT_DEBUG("pthread %ld acquired lock on cqueue_pop %s\n", pthread_name(), " ");

    //compute length of items[last_item]
    size_t str_len = strlen(cq->items[cq->size-1])+1;
    //allocate memory for output string
    *outp = malloc(sizeof(char)*str_len);
    //check alloc fail (release mutex) -> considerato come errore fatale, quindi non controllo il valore di ritorno della unlock
    if(!(*outp)){ pthread_mutex_unlock(&(cq->mtx)); return (char) 0; }
    //copy items[last_item] into output string
    *outp = memset(*outp, '\0', sizeof(char)*str_len);
    *outp = strncpy(*outp, cq->items[cq->size-1], str_len-1);

    //free cq->items[cq->size-1]
    free(cq->items[cq->size-1]);

    //se la coda è vuota setto cq->items a NULL
    if(cq->size == 1){
        free(cq->items); 
        cq->items = NULL; 
        cq->size = 0;
    }
    //altrimenti
    else{
        //resize items from cq->size to --cq->size
        char** temp = realloc( cq->items, sizeof(char*) * (--cq->size) );
        //realloc fail (free outp and mutex release) -> anche qui è considerato errore fatale, quindi non controllo il valore di ritorno della unlock
        if(!temp){ pthread_mutex_unlock(&(cq->mtx)); free(*outp); *outp = NULL; return (char) 0; }
        //copy temp into items
        cq->items = temp;
    }
 
    if(pthread_cond_signal(&(cq->full_cond))){
        fprintf(stderr, "cqueue_pop -> cond signal (full)\n");
        pthread_mutex_unlock(&(cq->mtx));
        return (char) 0;
    }

    PT_DEBUG("pthread %ld is about to release lock on cqueue_pop -> return value : %s\n", pthread_name(), *outp);
    if(pthread_mutex_unlock(&(cq->mtx))){ fprintf(stderr, "cqueue_pop -> pthread_mutex_unlock : fatal error\n"); return (char) 0; }
    PT_DEBUG("pthread %ld released lock on cqueue_pop -> return value : %s\n", pthread_name(), *outp);


    return (char) 1;
}


//EFFECT : pushes an element containing the "\0" string to the queue, meaning the termination will be notified when all the other elements are popped
//MODIFIES : cq
//CONCURRENT : acquisisce il lock su cq->mtx, ma non controlla la condizione di coda piena, lo rilascia e notifica la condizione coda non vuota
//RETURNS : 0 se fallimento (fatale), 1 altrimenti
//!!!ATTENZIONE!!! la stringa "\0" è comunque allocata dinamicamente -> deve essere deallocata
char cqueue_delayed_termination(struct cqueue* cq){
    if(!cq){ fprintf(stderr, "Error : trying to add TERMINATION to a NULL cqueue\n"); return (char) 0; }

    //attesa condizione coda piena
    PT_DEBUG("pthread %ld trying to acquire lock on cqueue_append -> args : %s\n", pthread_name(), "");
    if(pthread_mutex_lock(&(cq->mtx))){ fprintf(stderr, "cqueue_append error -> mutex lock\n"); return 0; }
    PT_DEBUG("pthread %ld acquired lock on cqueue_append -> args : %s\n", pthread_name(), "");

    //copia str in un nuovo array
    size_t str_len = 1;
    char* new_item = malloc(sizeof(char)*str_len);
    if( !new_item ){ pthread_mutex_unlock(&(cq->mtx)); fprintf(stderr, "cqueue_add error -> malloc\n"); return (char)0; }
    new_item = memset(new_item, '\0', str_len);
    
    //riallocazione memoria della queue
    char** temp = realloc(cq->items, sizeof(char*)*(++cq->size));
    if(!temp){ fprintf(stderr, "cqueue_add -> realloc\n"); free(new_item); pthread_mutex_unlock(&(cq->mtx)); return (char) 0; }
    cq->items = temp;

    //shift elementi della queue
    for(size_t i = cq->size-1; i >= 1; i--){
        cq->items[i] = cq->items[i-1];
    }
    //assegnamento nuovo elemento nella queue
    cq->items[0] = new_item;

    //segnale condizione coda non vuota
    if(pthread_cond_signal(&(cq->empty_cond))){ //controllo errore (fatale) 
        fprintf(stderr, "cqueue_append -> cond signal (empty)\n"); 
        pthread_mutex_unlock(&(cq->mtx));
        return (char) 0; 
    }

    //rilascio lock
    PT_DEBUG("pthread %ld about to release lock on cqueue_append -> args : %s\n", pthread_name(), "");
    if(pthread_mutex_unlock(&(cq->mtx))){ fprintf(stderr, "cqueue_append -> pthread_mutex_unlock : fatal error\n"); return (char) 0; }
    PT_DEBUG("pthread %ld released lock on cqueue_append -> args : %s\n", pthread_name(), "");

    return (char) 1;
}

//inserisce l'elemento di terminazione in cima alla coda
char cqueue_immediate_termination(struct cqueue* cq){
    if(!cq){ fprintf(stderr, "Error : trying to add TERMINATION to a NULL cqueue\n"); return (char) 0; }

    //attesa condizione coda piena
    PT_DEBUG("pthread %ld trying to acquire lock on cqueue_append -> args : %s\n", pthread_name(), "");
    if(pthread_mutex_lock(&(cq->mtx))){ fprintf(stderr, "cqueue_append -> mutex lock\n"); return 0; }
    PT_DEBUG("pthread %ld acquired lock on cqueue_append -> args : %s\n", pthread_name(), "");

    //copia str in un nuovo array
    size_t str_len = 1;
    char* new_item = malloc(sizeof(char)*str_len);
    if( !new_item ){ pthread_mutex_unlock(&(cq->mtx)); fprintf(stderr, "cqueue_add -> malloc\n"); return (char)0; }
    new_item = memset(new_item, '\0', str_len);
    
    //riallocazione memoria della queue
    char** temp = realloc(cq->items, sizeof(char*)*(++cq->size));
    if(!temp){ fprintf(stderr, "cqueue_add -> realloc\n"); free(new_item); pthread_mutex_unlock(&(cq->mtx)); return (char) 0; }
    cq->items = temp;
    //assegnamento nuovo elemento nella queue
    cq->items[cq->size-1] = new_item;

    //segnale condizione coda non vuota
    if(pthread_cond_signal(&(cq->empty_cond))){ //controllo errore (fatale) 
        fprintf(stderr, "cqueue_append -> cond signal (empty)\n"); 
        pthread_mutex_unlock(&(cq->mtx));
        return (char) 0; 
    }

    //rilascio lock
    PT_DEBUG("pthread %ld about to release lock on cqueue_append -> args : %s\n", pthread_name(), "");
    if(pthread_mutex_unlock(&(cq->mtx))){ fprintf(stderr, "cqueue_append -> pthread_mutex_unlock : fatal error\n"); return (char) 0; }
    PT_DEBUG("pthread %ld released lock on cqueue_append -> args : %s\n", pthread_name(), "");

    return (char) 1;
}

void cqueue_free(struct cqueue* cq){
    for(size_t i = 0; i < cq->size; i++)
        free(cq->items[i]);
    if(cq->items) free(cq->items);
    pthread_mutex_destroy(&(cq->mtx));
    pthread_cond_destroy(&(cq->empty_cond));
    pthread_cond_destroy(&(cq->full_cond));
    free(cq);
}

void cqueue_print(struct cqueue* cq){
    if(pthread_mutex_lock(&(cq->mtx))){ fprintf(stderr, "cqueue_print -> mutex_lock error\n"); return; }
    printf("queue size : %ld\nqueue elements :\n", cq->size);
    for(size_t i = 0; i < cq->size; i++)printf("%s\n", cq->items[i]);
    if(pthread_mutex_unlock(&(cq->mtx))){ fprintf(stderr, "cqueue_print -> mutex_unlock error\n"); return; }
}

//USAGE EXAMPLE
/*
struct ptarg{
    size_t num;
    struct cqueue* cq;
    int kill;
};

void* producer(void* args){
    struct timespec t;
    t.tv_nsec = 3000;
    t.tv_sec = 0;

    struct ptarg* arg = (struct ptarg*) args;
    for(size_t i = 0; i < arg->num; i++){
        char msg[128];
        memset(msg, '\0', 128*sizeof(char));
        char strno[16];
        memset(strno, '\0', 16*sizeof(char));
        strncpy(msg, "msg ", 4);
        sprintf(strno, "%ld", i);
        strncat(msg, strno, strlen(strno));
        strncat(msg, " : ciao from ", 14);
        memset(strno, 0, sizeof(char)*16);
        sprintf(strno, "%ld", pthread_name());
        strncat(msg, strno, strlen(strno));

        printf("producer %ld produced : %s\n", pthread_name(), msg);
        cqueue_append(arg->cq, msg);
        //nanosleep(&t, NULL);
    }
    
    return (void*)0;
}

void* consumer(void* args){
    struct timespec t;
    t.tv_nsec = 10000;
    t.tv_sec = 0;

    struct ptarg* arg = (struct ptarg*) args;
    for(size_t i = 0; i < arg->num; i++){
        char* msg = NULL;

        cqueue_pop(arg->cq, &msg);
        printf("consumer %ld received : %s\n", pthread_name(), msg);
        free(msg);
        nanosleep(&t, NULL);
    }

    return (void*)0;
}

void* cqueue_monitor(void* args){
    struct timespec t;
    t.tv_nsec = 5000;
    t.tv_sec = 0;

    struct ptarg* arg = (struct ptarg*) args;
    while(1){
        nanosleep(&t, NULL);
        printf("\n\n======monitor wake=====\n\n");
        cqueue_print(arg->cq);
    }

    return (void*)0;
}

const size_t CQ_SIZE = 4;
const size_t POOL_SIZE = 32;
const size_t MSG_NO = 8;

int main(void){
    struct cqueue* cq = cqueue_init(8);
    struct ptarg arg;
    arg.cq = cq;
    arg.num = MSG_NO;
    arg.kill = 0;

    //pthread_t monitor = 0;
    //if(pthread_create(&monitor, NULL, cqueue_monitor, &arg)){ fprintf(stderr, "pthread create\n"); exit(1); }

    pthread_t producers[POOL_SIZE], consumers[POOL_SIZE];
    memset(producers, 0, sizeof(pthread_t)*POOL_SIZE);
    memset(consumers, 0, sizeof(pthread_t)*POOL_SIZE);
    for(size_t i = 0; i < POOL_SIZE; i++){
        if(pthread_create(&consumers[i], NULL, consumer, &arg)){ fprintf(stderr, "pthread create\n"); exit(1); }
        if(pthread_create(&producers[i], NULL, producer, &arg)){ fprintf(stderr, "pthread create\n"); exit(1); }
    }
    for(size_t i = 0; i < POOL_SIZE; i++){
        pthread_join(producers[i], NULL);
        pthread_join(consumers[i], NULL);
    }
    fprintf(stderr, "\n\n\n==========ALL DONE=========\n\n\n");

    //pthread_cancel(monitor);
    //pthread_join(monitor, NULL);

    cqueue_print(cq);
    cqueue_free(cq);

    return 0;
}

*/
