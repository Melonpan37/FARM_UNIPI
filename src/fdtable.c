#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>       
#include <sys/stat.h>    
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include "fdtable.h"

struct fd_table fd_table_init(){
    struct fd_table fdtb;
    memset(&fdtb, 0, sizeof(struct fd_table));
    return fdtb;
}

static int fd_table_max_fd(struct fd_table fdtb){
    int max = 0;
    for(size_t i = 0; i < fdtb.size; i++){
        if(fdtb.ent[i].fd > max) max = fdtb.ent[i].fd;
    }
    return max;
}


//EFFECT : restituisce l'indice della entry in fdtb relativa a fd
//MODIFIES : null
//RETURNS : -1 se fd non trovato, indice altrimenti
ssize_t fd_table_find(struct fd_table fdtb, int fd){
    for(size_t i = 0; i < fdtb.size; i++) if(fdtb.ent[i].fd == fd) return i;
    return -1;
}

//EFFECT : aggiunge la entry relativa a fd a fdtb->ent con buffer vuoto (controlla anche fd duplicati)
//RETURNS : 0 se successo, 1 se fd duplicato, 2 errore allocazione
char fd_table_append(struct fd_table* fdtb, int fd){
    for(size_t i = 0; i < fdtb->size; i++) if(fdtb->ent[i].fd == fd) return 1;
    fdtb->size++;
    struct fd_entry* temp = realloc(fdtb->ent, sizeof(struct fd_entry)*fdtb->size);
    if(!temp){perror("fd_table_append -> realloc"); return 2;}
    fdtb->ent = temp;

    struct fd_entry fdet;
    fdet.buf = NULL;
    fdet.buf_size = 0;
    fdet.fd = fd;
    fdtb->ent[fdtb->size-1] = fdet;

    if(fd > fdtb->max_fd) fdtb->max_fd = fd;

    return 0;
}

//EFFECT : elimina la entry relativa a fd ridimensionando la memoria allocata per fdtb->ent, liberando anche il suo buffer
//NB : se fdtb->size == 1, rimuove l'unica entry, libera la memoria allocata fdtb->ent e lo setta a NULL
//REQUIRES : fdtb != NULL, fd appartenente a fdtb->ent
//RETURNS : 0 se successo, 1 se fd non trovato, 2 se errore allocazione
char fd_table_remove(struct fd_table* fdtb, int fd){
    for(size_t i = 0; i < fdtb->size; i++){
        if(fdtb->ent[i].fd != fd) continue;

        //fdtb ha un solo elemento
        if(fdtb->size == 1){ 
            if(fdtb->ent[0].buf_size) free(fdtb->ent[0].buf); 
            free(fdtb->ent); 
            fdtb->ent = NULL; 
            fdtb->size = 0; 
            fdtb->max_fd = 0; 
            return 0; 
        }
        //devo eliminare l'ultimo elemento di fdtb
        if(i == fdtb->size-1){
            if(fdtb->ent[i].buf_size) free(fdtb->ent[i].buf);
            struct fd_entry* temp = realloc(fdtb->ent, sizeof(struct fd_entry)*(fdtb->size-1));
            if(!temp){ perror("fd_table_remove -> realloc"); return 2; }
            fdtb->ent = temp;
            fdtb->size--;
            if(fd == fdtb->max_fd){fdtb->max_fd = fd_table_max_fd(*fdtb);}
            return 0;
        }
        

        //copia dell'ultimo elemento (per fallimento nella realloc)
        struct fd_entry tempentr = fdtb->ent[fdtb->size-1]; 
        //riallocazione array
        struct fd_entry* temp = realloc(fdtb->ent, sizeof(struct fd_entry)*(fdtb->size-1));
        if(!temp){ perror("fd_table_remove -> realloc"); return 2; }
        fdtb->ent = temp;

        //aggiornamento size
        fdtb->size--;
        //deallocazione del buffer della entry da eliminare
        if(fdtb->ent[i].buf_size){ free(fdtb->ent[i].buf); fdtb->ent[i].buf = NULL; fdtb->ent[i].buf_size = 0; }
        //shift left
        for(size_t j = i; j < fdtb->size-1; j++){
            fdtb->ent[j] = fdtb->ent[j+1];
        }
        //riassegnamento utlimo elemento
        fdtb->ent[fdtb->size-1] = tempentr;

        if(fd == fdtb->max_fd){fdtb->max_fd = fd_table_max_fd(*fdtb);}

        return 0;
    }
    return 1;
}

//EFFECT : scrive IN APPEND nel buffer della entry relativa a fd
//REQUIRES : fdtb != NULL, fd appartiene fdtb->ent
//RETURNS : 0 se successo, 1 se fd non trovato, 2 se errore allocazione
char fd_table_write_buffer(struct fd_table* fdtb, int fd, char* attachment){
    for(size_t i = 0; i < fdtb->size; i++){
        //cerca fd_entry relativa a fd
        if(fdtb->ent[i].fd != fd) continue;
        //se il buffer non è ancora inizializzato
        if( !fdtb->ent[i].buf_size ){
            size_t bufsize = strlen(attachment)+1;
            if( !(fdtb->ent[i].buf = malloc(sizeof(char)*bufsize)) ){perror("fd_table_write_buffer -> malloc"); return 2;}
            memset(fdtb->ent[i].buf, '\0', bufsize);
            strncpy(fdtb->ent[i].buf, attachment, bufsize-1);
            fdtb->ent[i].buf_size = bufsize;
            return 0;
        }
        //se il buffer è già inizializzato
        else{
            size_t bufsize = strlen(attachment) + fdtb->ent[i].buf_size;
            char* temp = realloc(fdtb->ent[i].buf, sizeof(char)*bufsize);
            if(!temp){ perror("fd_table_write_buffer -> realloc"); return 2; }
            fdtb->ent[i].buf = temp;
            memset(fdtb->ent[i].buf+strlen(fdtb->ent[i].buf), '\0', strlen(attachment)+1);
            strncat(fdtb->ent[i].buf, attachment, strlen(attachment));
            fdtb->ent[i].buf_size = bufsize;
            return 0;
        }
    }
    return 1;
}

//RETURNS : il buffer puntato dalla fd_entry relativa a fd in caso di successo, NULL in caso di fallimento
char* fd_table_get_buffer(struct fd_table* fdtb, int entry_fd){
    if(!fdtb->size) return NULL;
    size_t index = fd_table_find(*fdtb, entry_fd);
    if(index == -1) return NULL;
    return fdtb->ent[index].buf;
}

//EFFECT : libera la memoria associata ai buffer di tutte le entry e poi libera l'array delle entry stesso e lo setta a NULL
//REQUIRES : fdtb != NULL
//N.B. : fdtb non viene liberato
void fd_table_free(struct fd_table* fdtb){
    if(!fdtb->ent) return;
    //free buffers
    for(size_t i = 0; i < fdtb->size; i++) if(fdtb->ent[i].buf_size){free(fdtb->ent[i].buf);}
    //free ent
    free(fdtb->ent);
    fdtb->ent = NULL;
    fdtb->size = 0;
    fdtb->max_fd = 0;
}

//EFFECT : resetta il buffer della entry relativa a entry_fd a NULL (e lo dealloca) e la dimensione fd_entry.buf_size a 0
void fd_table_clear_buffer(struct fd_table* fdtb, int entry_fd){
    if(!fdtb->size) return;
    if(!fdtb->ent) return;
    size_t index = fd_table_find(*fdtb, entry_fd);
    if(index == -1) return;
    if(!fdtb->ent[index].buf_size) return;
    if(!fdtb->ent[index].buf) return;
    free(fdtb->ent[index].buf);
    fdtb->ent[index].buf = NULL;
    fdtb->ent[index].buf_size = 0;
}

void fd_table_print(struct fd_table fdtb){
    if(!fdtb.size){
        printf("fd table is empty\n");
        return;
    }
    printf("fd table size : %ld\nmax fd num : %d\n", fdtb.size, fdtb.max_fd);
    for(size_t i = 0; i < fdtb.size; i++){
        if(fdtb.ent[i].buf_size) printf("fd : %d, buf : %s\n", fdtb.ent[i].fd, fdtb.ent[i].buf);
        else printf("fd : %d\n", fdtb.ent[i].fd);
    }
}


//USAGE EXAMPLE
/*
int main(void){
    struct fd_table fdtb = fd_table_init();
    printf("RES %d\n", fd_table_append(&fdtb, 1));
    printf("RES %d\n", fd_table_append(&fdtb, 2));
    printf("RES %d\n", fd_table_remove(&fdtb, 2));
    exit(0);

    fd_table_append(&fdtb, 10);
    fd_table_append(&fdtb, 11);
    fd_table_append(&fdtb, 3);
    fd_table_append(&fdtb, 3); //append duplicato -> deve essere ignorato
    printf("CICLO APPEND INIZIALE\n");
    fd_table_print(fdtb);

    fd_table_write_buffer(&fdtb, 10, "sono il fd 10!");
    fd_table_write_buffer(&fdtb, 3, "io invece sono il fd 3...");
    fd_table_write_buffer(&fdtb, 10, " e a me piace essere appeso :/");
    fd_table_write_buffer(&fdtb, 36, "io non esisto");
    printf("CICLO WRITE\n");
    fd_table_print(fdtb);

    fd_table_remove(&fdtb, 11);
    fd_table_remove(&fdtb, 10);
    fd_table_remove(&fdtb, 10); //rimozione duplicata -> deve essere ignorata
    printf("CICLO RIMOZIONE PARZIALE\n");
    fd_table_print(fdtb);

    fd_table_append(&fdtb, 10);
    fd_table_append(&fdtb, 13);
    fd_table_append(&fdtb, 26);
    fd_table_write_buffer(&fdtb, 10, "guarda chi è risorto dalle ceneri >:)");
    fd_table_write_buffer(&fdtb, 26, "---26 è un bel numero---");
    printf("CICLO APPEND POST-RIMOZIONE\n");
    fd_table_print(fdtb);

    fd_table_free(&fdtb);
    fd_table_free(&fdtb); //doppia rimozione -> deve essere ignorata
    printf("RIMOZIONE TOTALE CON fd_table_free\n");
    fd_table_print(fdtb);


    fd_table_append(&fdtb, 10);
    fd_table_append(&fdtb, 13);
    fd_table_append(&fdtb, 26);
    fd_table_write_buffer(&fdtb, 10, "mi sa che questa è l'ultima volta che risorgo :c");
    printf("CICLO APPEND DOPO RIMOZIONE CON fd_table_free\n");
    fd_table_print(fdtb);

    fd_table_remove(&fdtb, 10);
    fd_table_remove(&fdtb, 13);
    fd_table_remove(&fdtb, 26);
    printf("CICLO REMOVE SINGOLI CON fd_table_remove\n");
    fd_table_print(fdtb);

    fd_table_append(&fdtb, 10);
    fd_table_write_buffer(&fdtb, 10, "scherzavo, sono tornato :D");
    printf("APPEND POST-REMOVE SINGOLI\n");
    fd_table_print(fdtb);

    fd_table_free(&fdtb);
    printf("FREE FINALE\n");
    fd_table_print(fdtb);
}
*/