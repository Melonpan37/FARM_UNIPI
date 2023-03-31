#ifndef FD_TABLE
#define FD_TABLE

#include <sys/types.h>

struct fd_entry{
    int fd;
    unsigned long buf_size;
    char* buf;
};

struct fd_table{
    int max_fd;
    unsigned long size;
    struct fd_entry* ent;
};

struct fd_table fd_table_init();

//EFFECT : restituisce l'indice della entry in fdtb relativa a fd
//MODIFIES : null
//RETURNS : -1 se fd non trovato, indice altrimenti
ssize_t fd_table_find(struct fd_table fdtb, int fd);

//EFFECT : aggiunge la entry relativa a fd a fdtb->ent con buffer vuoto (controlla anche fd duplicati)
//RETURNS : 0 se successo, 1 se fd duplicato, 2 errore allocazione
char fd_table_append(struct fd_table* fdtb, int fd);

//EFFECT : elimina la entry relativa a fd ridimensionando la memoria allocata per fdtb->ent, liberando anche il suo buffer
//NB : se fdtb->size == 1, rimuove l'unica entry, libera la memoria allocata fdtb->ent e lo setta a NULL
//REQUIRES : fdtb != NULL, fd appartenente a fdtb->ent
//RETURNS : 0 se successo, 1 se fd non trovato, 2 se errore allocazione
char fd_table_remove(struct fd_table* fdtb, int fd);

//EFFECT : resetta il buffer della entry relativa a entry_fd a NULL (e lo dealloca) e la dimensione fd_entry.buf_size a 0
void fd_table_clear_buffer(struct fd_table* fdtb, int entry_fd);

//RETURNS : il buffer puntato dalla fd_entry relativa a fd in caso di successo, NULL in caso di fallimento
char* fd_table_get_buffer(struct fd_table* fdtb, int entry_fd);

//EFFECT : scrive IN APPEND nel buffer della entry relativa a fd
//REQUIRES : fdtb != NULL, fd appartiene fdtb->ent
//RETURNS : 0 se successo, 1 se fd non trovato, 2 se errore allocazione
char fd_table_write_buffer(struct fd_table* fdtb, int fd, char* attachment);

//EFFECT : libera la memoria associata ai buffer di tutte le entry e poi libera l'array delle entry stesso e lo setta a NULL
//REQUIRES : fdtb != NULL
//N.B. : fdtb non viene liberato
void fd_table_free(struct fd_table* fdtb);

void fd_table_print(struct fd_table fdtb);


#endif