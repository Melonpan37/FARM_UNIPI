#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "../headers/utils.h"
#include "../headers/rdd.h"

enum MASTER_OPT_ERR {
    MASTER_OPT_ERR_OK,                          //nessun errore nelle opts
    MASTER_OPT_ERR_BLANK_OPT,                   //errore nessuna opt (deve essere specificato almeno un file)
    MASTER_OPT_ERR_FILE_NOT_FILEPATH,           //errore filepath argomento non è valido
    MASTER_OPT_ERR_FILE_UNSUPPORTED_EXTENSION,  //errore file non .dat
    MASTER_OPT_ERR_FILE_NOT_READABLE,           //errore file non leggibile (no permessi)
    MASTER_OPT_ERR_FILE_LONG_PATH,              //errore path troppo lungo
    MASTER_OPT_ERR_N_NO_PARAMETER,              //errore no argomento a -n
    MASTER_OPT_ERR_N_NAN,                       //errore argomento -n non è un numero
    MASTER_OPT_ERR_N_ZERO,                      //errore argomento -n è zero
    MASTER_OPT_ERR_Q_NO_PARAMETER,              //stesso ma per -q
    MASTER_OPT_ERR_Q_NAN,                       //
    MASTER_OPT_ERR_Q_ZERO,                      //
    MASTER_OPT_ERR_T_NO_PARAMETER,              //stesso ma per -t
    MASTER_OPT_ERR_T_NAN,                       //
    MASTER_OPT_ERR_UNRECOGNIZED_OPT,            //opt inesistente
    MASTER_OPT_ERR_ALLOC,                       //errore allocazione :c
    MASTER_OPT_ERR_D_NO_PARAMETER               //errore non è stata passata una directory a -d
};

//indice che punta all'elemento di argv (del main) per il quale si è scatenato un errore
//serve per messaggio di errore in alcuni casi
int master_opterr_argv_index = -1;

//controlla che un file sia .dat, leggibile e che il path non superi 255 caratteri
static char file_is_valid(char* path){
    if(strlen(path) > 255) return MASTER_OPT_ERR_FILE_LONG_PATH;
    char* last_occurrence;
    //controllo che sia un path a un file
    if(!(last_occurrence = strchr(path, '.'))) return MASTER_OPT_ERR_FILE_NOT_FILEPATH;    
    //controllo che sia un file '.dat'
    if(!file_has_extension(path, "dat")) return MASTER_OPT_ERR_FILE_UNSUPPORTED_EXTENSION;         
    //controllo che il file esiste e che abbia i permessi di lettura 
    if(!file_is_readable(path)) return MASTER_OPT_ERR_FILE_NOT_READABLE;                  
    return 0;
}

//NOTA : si comporta in modo del tutto analogo a file_is_valid, ma non ritorna un codice errore (è usata da funzioni di ordine superiore)
//EFFECT : controlla che un file sia .dat, che sia non più lungo di 255 caratteri, e che sia leggibile
//RETURNS : 1 se il file è un .dat conforme ai requisiti (#path < 255, accessibile in lettura), 0 altrimenti
char filter_dat(const char* path){
    size_t path_len = strlen(path)+1;
    //controllo lunghezza massima del path
    if(path_len > 255) return 0;
    //controllo tipo del file
    if(is_dir(path)) return (char) 1;
    if(is_reg(path)){
        //controllo se il file ha un estensione
        char* last_dot = NULL;
        if( !(last_dot = strrchr(path, '.')) ){ return 0; }
        //controllo se l'estensione è .dat
        if( strncmp(last_dot, ".dat", max_size(strlen(last_dot), 4)) ){ return 0; }
        //controllo permessi di lettura
        if(access(path, R_OK)){ perror("Access"); return (char) 0; }
        return 1;
    }
    //ignoro altri tipi di file
    return 0;
}

//gestisce l'opzione -d
//EFFECT : esamina ricorsivamente directory_path e tutte le directory contenute al suo interno
//         alla ricerca di files .dat conformi ai requisiti e infine alloca su ***files l'array
//         di stringhe contenenti tutti i path ai files trovati nelle directories. La dimensione di **files è *files_size
//RETURNS : 0 on success, error code otherwise
int master_directory_opt(char* directory_path, char*** files, size_t* files_size){
    //creazione array di descrittori di directory di dimensione 1
    struct r_dirdesc** rdd = malloc(sizeof(struct r_dirdesc*));
    size_t rdd_size = 1;
    if(!rdd) return MASTER_OPT_ERR_ALLOC; //errore malloc
    //inserimento del descrittore directory della directory argomento in rdd
    rdd[0] = rdd_filter(directory_path, filter_dat); //costruisce un descrittore di directory filtrando i files usando filter_dat
    if(!rdd[0]){ free(rdd); return -1; } //errore rdd_filter

    //controllo che il decrittore della directory rdd[0] contenga subdirectories (e poi itero su queste)
    size_t counter = 0;
    do{
        //se l'elemento corrente di rdd non ha subdirectories
        if(!rdd[counter]->subdirs_size){
            counter++;
            continue;
        }
        
        //se ci sono, si itera su esse
        for(size_t i = 0; i < rdd[counter]->subdirs_size; i++){
            //puntatore temporaneo a r_dirdesc
            struct r_dirdesc* rdd_temp = NULL;
            //costruisco la dirdesc della subdir i-esima e controllo che restituisca un risultato
            if( !(rdd_temp = rdd_filter(rdd[counter]->subdirs[i], filter_dat)) ) continue; //filter_dat impedirà di proseguire con path più lunghi di 255

            //se restituisce un risultato allora salvo il puntatore temporaneo nell'array rdd
            rdd_size++; //incremento rdd_size
            struct r_dirdesc** temp = NULL; //puntatore per controllare il risultato della realloc
            temp = realloc(rdd, sizeof(struct r_dirdesc*) * (rdd_size)); //incremento size di rdd
            //controllo realloc
            if( !temp ){ for(size_t k = 0; k < rdd_size-1; k++) rdd_free(rdd[k]); free(rdd); return MASTER_OPT_ERR_ALLOC; }
            rdd = temp; //riassegnamento della nuova memoria allocata a rdd
            rdd[rdd_size-1] = rdd_temp; //assegnamento del nuovo elemento a rdd
        }
        counter++;
    } while(counter < rdd_size);

    //a questo punto rdd[] contiene descrittori di directory per ogni sottodirectory di directory_path
    //adesso bisogna estrarre tutti i files contenuti in ogni descrittore di directory

    //raggruppamento dei path dei file.dat
    *files_size = 0;
    for(size_t i = 0; i < rdd_size; i++){
        //se una cartella non ha files.dat
        if(!rdd[i]->files_size) continue;
        
        //unione dei paths in un unico char** array
        //NOTA : vedi merge -> le stringhe non sono copiate (allocate) in files
        //       ma solo dereferenziate, quindi deallocando rdd[i]->files
        //       vengono deallocati anche gli elementi di (*files)[]
        *files_size = merge(files, *files_size, rdd[i]->files, rdd[i]->files_size);
        //controllo errore merge (sempre errori di allocazione)
        if(!(*files_size)){ for(size_t k = 0; k < rdd_size; k++) rdd_free(rdd[i]); free(rdd); free(files); return MASTER_OPT_ERR_ALLOC;}

        //*files_size è aggiornato alla size giusta dalla merge    
    }

    //liberazione manuale della memoria allocata per r_dirdesc -> non bisogna deallocare rdd[i]->files, sennò si dealloca (*files)[k]
    for(size_t i = 0; i < rdd_size; i++){
        //liberazione subdirs
        for(size_t j = 0; j < rdd[i]->subdirs_size; j++) free(rdd[i]->subdirs[j]);
        //liberazione array subdirs
        free(rdd[i]->subdirs);
        //liberazione stringa del path
        free(rdd[i]->dir_rpath);
        //liberazione puntatore a r_dirdesc
        free(rdd[i]);
    }
    //liberazione array di r_dirdesc
    free(rdd);

    return 0;
}

//returns 0 on success, error number on failure
//EFFECT : parses farm opts
char master_getopt(int argc, char** argv, int* nthreads, int* qlen, char*** dnames, int* dnames_size, struct timespec* delay){
    *dnames_size = 0;

    //controllo argv per opts
    for(int i = 1; i < argc; i++){
        
        //usato - da solo
        if(argv[i][0] == '-' && strlen(argv[i]) < 2) return 1;
        
        //argv[i] è un file passato come argomento
        if(argv[i][0] != '-'){
            int file_error;
            if( (file_error = file_is_valid(argv[i])) ) {master_opterr_argv_index = i; return file_error;}
            
            *dnames = realloc(*dnames, sizeof(char*)*(++(*dnames_size)));
            //realloc fail
            if( !(*dnames) ){ perror("Realloc"); return MASTER_OPT_ERR_ALLOC; }
            (*dnames)[(*dnames_size)-1] = malloc(sizeof(char)*strlen(argv[i])+1);
            //malloc fail
            if(!((*dnames)[(*dnames_size)-1])){ perror("Malloc"); return MASTER_OPT_ERR_ALLOC; }
            (*dnames)[(*dnames_size)-1] = strncpy((*dnames)[(*dnames_size)-1], argv[i], strlen(argv[i])+1);
            continue;
        }

        //argv[i] è un opzione
        switch (argv[i][1])
        {
            case 'n' :
            {   
                if(
                    //non ci sono altri argomenti del programma
                    i+1 >= argc
                    ||
                    //il prossimo argomento è un'altra opt
                    argv[i+1][0] == '-'
                ) return MASTER_OPT_ERR_N_NO_PARAMETER;
                
                long value = 0;
                //controllo che l'argomento sia un numero
                if(!is_number(argv[i+1], &value)) return MASTER_OPT_ERR_N_NAN;
                //controllo che n non sia zero
                if(!value) return MASTER_OPT_ERR_N_ZERO;

                //assegna il valore di "ritorno"
                *nthreads = (unsigned int) value;
                
                //skip next argument
                i++;
                break;
            }
            case 'q' : 
            {
                if(
                    //non ci sono altri argomenti del programma
                    i+1 >= argc
                    ||
                    //il prossimo argomento è un'altra opt
                    argv[i+1][0] == '-'
                ) return MASTER_OPT_ERR_Q_NO_PARAMETER;
                

                long value = 0;
                if(!is_number(argv[i+1], &value)) return MASTER_OPT_ERR_Q_NAN;
                //controllo che non sia zero
                if(!value) return MASTER_OPT_ERR_Q_ZERO;

                //assegna il valore di "ritorno"
                *qlen = (unsigned int) value;

                //skip next argument
                i++;
                break; 
            } 
            case 't' :
            {
                if(
                    //non ci sono altri argomenti del programma
                    i+1 >= argc
                    ||
                    //il prossimo argomento è un'altra opt
                    argv[i+1][0] == '-'
                ) return MASTER_OPT_ERR_T_NO_PARAMETER;


                long value = 0;
                switch(is_number(argv[i+1], &value)){
                    case 0 : return MASTER_OPT_ERR_T_NAN;
                    case 1 : break;
                    case 2 : fprintf(stderr, "FARM : overflow in opt -t\n");
                }
                *delay = milliseconds_timespec(value);

                //skip next arg
                i++;
                break;
            }
            case 'd' :
            {

                if(
                    //non ci sono altri argomenti del programma
                    i+1 >= argc
                    ||
                    //il prossimo argomento è un'altra opt
                    argv[i+1][0] == '-'
                ) return MASTER_OPT_ERR_D_NO_PARAMETER;

                //aggiornamento dei files
                char** files = NULL;
                size_t files_size = 0;
                //navigazione ricorsiva directory
                int ret = master_directory_opt(argv[i+1], &files, &files_size);
                if(ret){ return ret; } //errori in master_directory_opt

                //unione precedente lista dei files e nuova
                size_t dnames_cpy = *dnames_size;
                *dnames_size = merge(dnames, dnames_cpy, files, files_size);
                
                i++;
                break;
            }
            default : {master_opterr_argv_index = i; return MASTER_OPT_ERR_UNRECOGNIZED_OPT;}
        }
    }
    return MASTER_OPT_ERR_OK;
}

//stampa messaggio di errore relativo alle opts
void master_getopt_error(short unsigned int error, char** argv){
    fprintf(stderr, "Error in program options : \n");
    switch(error) {
        case(MASTER_OPT_ERR_BLANK_OPT) : 
        {
            fprintf(stderr, "- : is not a valid option.\n");
            break;    
        }
        case(MASTER_OPT_ERR_FILE_NOT_FILEPATH) : 
        {
            fprintf(stderr, "%s : is not a filepath.\n", argv[master_opterr_argv_index]);
            break;    
        }
        case(MASTER_OPT_ERR_FILE_UNSUPPORTED_EXTENSION) :
        {
            fprintf(stderr, "%s : uses an unsupported extension. Use .dat files only.\n", argv[master_opterr_argv_index]);
            break;
        }
        case(MASTER_OPT_ERR_FILE_NOT_READABLE) :
        {
            perror(argv[master_opterr_argv_index]);
            break;
        }
        case(MASTER_OPT_ERR_FILE_LONG_PATH) :
        {
            fprintf(stderr, "%s : filepath excedes maximum length allowed of 255 characters.\n", argv[master_opterr_argv_index]);
        }
        case(MASTER_OPT_ERR_ALLOC) : 
        {
            perror("Malloc");
            break;
        }
        case(MASTER_OPT_ERR_N_NO_PARAMETER) : 
        {
            fprintf(stderr, "-n : requires a parameter.\n");
            break;
        }
        case(MASTER_OPT_ERR_N_NAN) : 
        {
            fprintf(stderr, "-n : requires a number as parameter.\n");
            break;
        }
        case(MASTER_OPT_ERR_N_ZERO) : {
            fprintf(stderr, "-n : cannot be zero.\n");
            break;
        }
        
        case(MASTER_OPT_ERR_Q_NO_PARAMETER) : 
        {
            fprintf(stderr, "-q : requires a parameter.\n");
            break;
        }
        case(MASTER_OPT_ERR_Q_NAN) : 
        {
            fprintf(stderr, "-q : requires a number as parameter.\n");
            break;
        }
        case(MASTER_OPT_ERR_Q_ZERO) : 
        {
            fprintf(stderr, "-q : cannot be zero.\n");
            break;
        }
        case(MASTER_OPT_ERR_T_NO_PARAMETER) : 
        {
            fprintf(stderr, "-t : requires a parameter.\n");
            break;
        }
        case(MASTER_OPT_ERR_T_NAN) : 
        {
            fprintf(stderr, "-t : requires a number as parameter.\n");
            break;
        }
        case(MASTER_OPT_ERR_UNRECOGNIZED_OPT) : 
        {
            fprintf(stderr, "%s : unrecognized option.\n", argv[master_opterr_argv_index]);
            break;
        }
    }
}


