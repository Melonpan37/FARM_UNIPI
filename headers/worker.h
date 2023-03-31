#ifndef _WORKER_H_
#define _WORKER_H_
 
//EFFECT    : segnala alla coda l'uscita del thread dalla pool.
//            se kill_process allora in caso di errore invia anche un segnale SIGINT al proprio processo
//MODIFIES  : cq->termination (cioè il numero di thread usciti dal pool)
//RETURNS   : nulla, perchè è best effort per la terminazione nel caso in cui si siano già verificati errori
void signal_termination(struct cqueue* cq, char kill_process);


//EFFECT    : reads a file as a collection of long int and sums the long values multiplied by their indices
//MODIFIES  : *res
//RETURNS   : 0 on success, x > 0 on system call errors
char decode_dat(const char* path, long* res);


void* worker(void* arg);


#endif