#ifndef CQUEUE_H
#define CQUEUE_H

#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include "../headers/debug_flags.h"

//#define PTDB  //decommentare per log in stdout di ogni operazione
#define PTSN    //pthread_short_names (vedi long pthread_name())

//utility
#ifndef FREED
#define FREED
#define FREE2D(arr, arrsize)                                    \
    for(size_t arrindex = 0; arrindex < arrsize; arrindex++)    \
        free(arr[arrindex]);                                    \
    free(arr);
#endif          


#ifdef PTDB //permette la scrittura in stdout per ogni operazione significativa per la concorrenza
#define PT_DEBUG(message, arg1, arg2) fprintf(stderr, message, arg1, arg2);   
#endif
#ifndef PTDB
#define PT_DEBUG(message, arg1, arg2) ;
#endif
         


struct cqueue{
    char** items;               //oggetti nella coda
    size_t size;                //numero di oggetti nella coda (dimensione array di strighe items)
    size_t max_size;            //numero massimo di oggetti nella coda (non considera oggetti di terminazione)
    pthread_mutex_t mtx;        //mutua esclusione operazioni sulla coda
    pthread_cond_t empty_cond;  //condizione coda vuota
    pthread_cond_t full_cond;   //condizione coda piena
    unsigned int left_pool;     //contatore threads che hanno lasciato la coda
};

//EFFECT : restituisce il nome del thread(pthread_self) oppure le ultime quattro cifre di questo se PTSN è definito
long pthread_name();


//EFFECT : inizializza (alloca) una nuova cqueue*, e copia i valori di items in cq->items
//RETURNS : NULL se errore, cqueue* (da liberare con cqueue_free) se successo
struct cqueue* cqueue_init(size_t cqueue_size);

//add str element to cq->items
//MODIFIES : cq->items, cq->size
//EFFECT : alloca spazio per il nuovo elemento di cq->items, copia str in cq->items[nuova_size]
//CONCURRENT : acquisice e rilascia cq->mtx, segnala su cq->cond se la coda era vuota prima della chiamata della funzione
//REQUIRES : cq != NULL, str != NULL
//RETURNS : 0 se fallimento (errore fatale tipo allocazione o cq = NULL), 1 altrimenti
//!!!ATTENZIONE!!! : str viene copiata in fondo a items, quindi cq->items[size-1] deve essere deallocato (str non viene modificata)
char cqueue_append(struct cqueue* cq, const char* str);

//add str element to cq->items
//MODIFIES : cq->items, cq->size
//EFFECT : copia il contenuto di cq->items[cq->size-1] in output e lo rimuove (free) da items
//CONCURRENT : acquisice e rilascia cq->mtx, si blocca sulla condizione cq->cond se cq->size == 0
//REQUIRES : cq != NULL
//RETURNS : 0 se fallimento (errore fatale tipo allocazione o cq = NULL), 1 altrimenti
//!!!ATTENZIONE!!! deallocare *outstr
char cqueue_pop(struct cqueue* cq, char** outp);


//EFFECT        : appends an element containing the "\0" string to the queue, meaning any new pop to the queue will notify the termination
//MODIFIES      : cq
//CONCURRENT    : acquisisce il lock su cq->mtx, ma non controlla la condizione di coda piena, lo rilascia e notifica la condizione coda non vuota
//RETURNS       : 0 se fallimento (fatale), 1 altrimenti
//!!!ATTENZIONE!!! la stringa "\0" è comunque allocata dinamicamente -> deve essere deallocata
char cqueue_immediate_termination(struct cqueue* cq);

//EFFECT        : pushes an element containing the "\0" string to the queue, meaning the termination will be notified when all the other elements are popped
//MODIFIES      : cq
//CONCURRENT    : acquisisce il lock su cq->mtx, ma non controlla la condizione di coda piena, lo rilascia e notifica la condizione coda non vuota
//RETURNS       : 0 se fallimento (fatale), 1 altrimenti
//!!!ATTENZIONE!!! la stringa "\0" è comunque allocata dinamicamente -> deve essere deallocata
char cqueue_delayed_termination(struct cqueue* cq);

void cqueue_free(struct cqueue* cq);

void cqueue_print(struct cqueue* cq);


#endif