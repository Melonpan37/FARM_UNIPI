#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../headers/utils.h"
#include "../headers/rdd.h"
        
//EFFECT : alloca una stringa che contenga il path e il nome del file separati da "/"
//!!!ATTENZIONE!!! il valore di ritorno deve essere deallocato
char* path_to_file(const char* path, const char* filename){
    size_t ptf_size = strlen(path)+strlen(filename)+2;
    char* ptf = malloc(sizeof(char)*ptf_size);
    if(!ptf){ perror("path_to_file -> malloc"); return NULL; }
    ptf = memset(ptf, '\0', sizeof(char)*ptf_size);
    ptf = strncpy(ptf, path, strlen(path));
    ptf = strncat(ptf, "/", 1);
    ptf = strncat(ptf, filename, strlen(filename));
    return ptf;
}

void rdd_print(struct r_dirdesc* rdd){
    if(!rdd){printf("rdd = null.\n"); return;}
    if(!rdd->dir_rpath){printf("directory path: NULL.\n"); return;}
    printf("directory path: %s\n", rdd->dir_rpath);
    printf("files number: %ld\n", rdd->files_size);
    for(size_t i = 0; i < rdd->files_size; i++) printf("file %ld: %s\n", i, rdd->files[i]);
    printf("subdirectories number : %ld\n", rdd->subdirs_size);
    for(size_t i = 0; i < rdd->subdirs_size; i++) printf("subdir %ld: %s\n", i, rdd->subdirs[i]);
}

//inizializza un puntatore a r_dirdesc
//rdd deve essere deallocato
//returns : NULL se errore, rdd altrimenti
struct r_dirdesc* rdd_init(){
    struct r_dirdesc* rdd = NULL;
    if( !(rdd = malloc(sizeof(struct r_dirdesc))) ) return NULL;
    rdd->dir_rpath = NULL;
    rdd->files = NULL;
    rdd->subdirs = NULL;
    rdd->subdirs_size = 0;
    rdd->files_size = 0;
    return rdd;
}

//assegna il valore di path a rdd->dir_rpath
//rdd->dir_rpath deve essere deallocato 
//REQUIRES : rdd->dir_rpath = NULL
//RETURNS : 0 se errore (allocazione o dir_rpath già esistente), 1 altrimenti
char rdd_rpath(struct r_dirdesc* rdd, const char* path){
    if(rdd->dir_rpath){ return (char) 0; } //è meglio non consentire la modifica di dir_rpath già esistente
    if(path[strlen(path)-1] == '/'){
        if( !(rdd->dir_rpath = malloc( sizeof(char) * (strlen(path)) )) ){ perror("Malloc"); return (char) 0; }
        memset(rdd->dir_rpath, '\0', sizeof(char)*strlen(path));
        rdd->dir_rpath = strncpy(rdd->dir_rpath, path, strlen(path)-1);
    }
    else{
        if( !(rdd->dir_rpath = malloc( sizeof(char) * (strlen(path)+1) )) ){ perror("Malloc"); return (char) 0; }
        memset(rdd->dir_rpath, '\0', sizeof(char)*strlen(path)+1);
        rdd->dir_rpath = strncpy(rdd->dir_rpath, path, strlen(path)+1);
    }
    return (char) 1;
}

//aggiunge subdir a rdd->subdirs e aggiorna rdd->subdirs_size
//rdd->subdirs[i] e rdd->subdirs devono essere deallocati
//REQUIRES : rdd->dir_rpath != NULL
//returns : 0 se errore (allocazione), 1 altrimenti
char rdd_add_subdir(struct r_dirdesc* rdd, char* subdir){
    if(!rdd->dir_rpath) return (char) 0;

    //creazione path nuova subdirectory    
    //NOTA : eseguito prima almeno non si chiama realloc se dovesse fallire malloc interna
    char* new_dir_path = path_to_file(rdd->dir_rpath, subdir);
    if(!new_dir_path){ return (char) 0; } //fallimento path_to_file (malloc)

    //riallocazione rdd->subdirs (char**)
    char** temp = NULL;
    temp = realloc(rdd->subdirs, sizeof(char*)*(rdd->subdirs_size+1));
    if( !temp ){ perror("rdd_add_subdir -> realloc"); return (char) 0; } //fallimento realloc
    rdd->subdirs = temp;

    //assegnamento nuovo path subdirectory all'array di subdirs
    rdd->subdirs[rdd->subdirs_size] = new_dir_path;
    //incremento size
    rdd->subdirs_size++;

    return (char) 1;
}

//aggiunge subdir a rdd->files e aggiorna rdd->files_size
//rdd->files[i] e rdd->files devono essere deallocati
//REQUIRES : rdd->dir_rpath != NULL
//returns : 0 se errore (allocazione), 1 altrimenti
char rdd_add_file(struct r_dirdesc* rdd, char* file){
    if(!rdd->dir_rpath) return (char) 0;

    //crezione nuovo file path
    //NOTA : eseguito prima della realloc almeno se fallisce malloc interna non ho allocato spazio non-riempibile
    char* new_file_path = path_to_file(rdd->dir_rpath, file);
    if(!new_file_path){ return (char) 0; }

    //reallocazione array rdd->files
    char** temp = NULL;
    temp = realloc(rdd->files, sizeof(char*)*(rdd->files_size+1));
    if( !temp ){ perror("rdd_add_file -> realloc"); return (char) 0; } //fallimento realloc
    rdd->files = temp;

    //assegnamento nuovo path a files
    rdd->files[rdd->files_size] = new_file_path;
    //incremento size
    rdd->files_size++;

    return (char) 1;
}

//dealloca rdd->files[i], rdd->subdirs[i], rdd->files, rdd->subfiles, e rdd
void rdd_free(struct r_dirdesc* rdd){
    if(!rdd) return;
    if(rdd->dir_rpath) free(rdd->dir_rpath);

    if(rdd->files_size){
        for(size_t i = 0; i < rdd->files_size; i++) free(rdd->files[i]);
        free(rdd->files);
    }
    if(rdd->subdirs_size){ 
        for(size_t i = 0; i < rdd->subdirs_size; i++) free(rdd->subdirs[i]);
        free(rdd->subdirs);
    }
    free(rdd);
    rdd = NULL;
}


//naviga la cartella path e costruisce il rdd relativo
//rdd, rdd->r_path, rdd->files, rdd->subdirs, rdd->files[i], rdd->subdirs[i] devono essere deallocati
//returns : NULL se errore, rdd altrimenti
struct r_dirdesc* rdd_create(const char* path){
    //inizializzo rdd
    struct r_dirdesc* rdd = rdd_init();
    //controllo rdd allocato con successo
    if(!rdd) return NULL;
    //controllo che path sia il path realtivo ad una directory
    if(!is_dir(path)){ fprintf(stderr, "%s : is not a directory\n", path); rdd_free(rdd); return NULL; }
    

    //assegnamento dir_rpath a rdd
    if(!rdd_rpath(rdd, path)){ rdd_free(rdd); return NULL; } //controllo errori di allocazione

    //inizializzazione stream della directory rdd->dir_rpath
    DIR* dp = NULL;
    struct dirent* entry = NULL;
    if( !(dp = opendir(path)) ){perror("opendir"); rdd_free(rdd); return NULL;}
    while( (errno = 0, entry = readdir(dp)) ){

        //non sono incluse . e ..
        if(!strncmp(entry->d_name, ".", 1)) continue;
        if(!strncmp(entry->d_name, "..", max_size(strlen(entry->d_name), 2)) ) continue;

        /* pur testando la macro è impossibile compilare con std=c99 e std=c11
        #ifdef _DIRENT_HAVE_D_TYPE
        if( (entry->d_type == DT_DIR) && !(rdd_add_subdir(rdd, entry->d_name)) ){ closedir(dp); rdd_free(rdd); return NULL; }
        if( (entry->d_type == DT_REG) && !(rdd_add_file(rdd, entry->d_name)) ){ closedir(dp); rdd_free(rdd); return NULL; }        
        #endif
        */

        //calcolo il path relativo della entry (per controllare con is_dir/is_file)
        char* newpath = NULL;
        if(!(newpath = path_to_file(path, entry->d_name)) ){closedir(dp); rdd_free(rdd); return NULL; } //errore allocazione
        //controllo il tipo della entry
        if(is_dir(newpath)) //se entry è una directory la aggiungo a rdd->subdirs
            if( !(rdd_add_subdir(rdd, entry->d_name)) ){ free(newpath); closedir(dp); rdd_free(rdd); return NULL; } //controllo errore allocazione
        if(is_reg(newpath)) //se entry è un file regolare lo aggiungo a rdd->files
            if( !(rdd_add_file(rdd, entry->d_name)) ){ free(newpath); closedir(dp); rdd_free(rdd); return NULL; } //controllo errore allocazione
        //newpath va deallocato 
        free(newpath);
    }

    if(errno){perror("readdir"); closedir(dp); rdd_free(rdd); return NULL;}
    closedir(dp);

    return rdd;
}




//naviga la cartella path e costruisce il rdd relativo
//rdd, rdd->r_path, rdd->files, rdd->subdirs, rdd->files[i], rdd->subdirs[i] devono essere deallocati
//returns : NULL se errore, rdd altrimenti
struct r_dirdesc* rdd_filter(const char* path, char (*filter)(const char* path)){
    //inizializzo rdd
    struct r_dirdesc* rdd = rdd_init();
    //controllo rdd allocato con successo
    if(!rdd) return NULL;
    //controllo che path sia il path realtivo ad una directory
    if(!is_dir(path)){ fprintf(stderr, "%s : is not a directory\n", path); rdd_free(rdd); return NULL; }
    
    //assegnamento dir_rpath a rdd
    if(!rdd_rpath(rdd, path)){ rdd_free(rdd); return NULL; } //controllo errori di allocazione

    //inizializzazione stream della directory rdd->dir_rpath
    DIR* dp = NULL;
    struct dirent* entry = NULL;
    if( !(dp = opendir(path)) ){perror("opendir"); rdd_free(rdd); return NULL;}
    while( (errno = 0, entry = readdir(dp)) ){
        //skippa . e ..
        if(!strncmp(entry->d_name, ".", 1)) continue;
        if(!strncmp(entry->d_name, "..", max_size(strlen(entry->d_name), 2)) ) continue;

        /* stesso che per rdd_create
        #ifdef _DIRENT_HAVE_D_TYPE
        if(!(*filter)(entry, rdd->dir_rpath)) continue;
        if( (entry->d_type == DT_DIR) && !(rdd_add_subdir(rdd, entry->d_name)) ){ closedir(dp); rdd_free(rdd); return NULL; }
        if( (entry->d_type == DT_REG) && !(rdd_add_file(rdd, entry->d_name)) ){ closedir(dp); rdd_free(rdd); return NULL; }        
        #endif
        */


        //path completo alla entry
        char* newpath = NULL;
        if(!(newpath = path_to_file(path, entry->d_name)) ){closedir(dp); rdd_free(rdd); return NULL; } //errore allocazione
        //filtro le entry
        if(!(*filter)(newpath)){
            free(newpath);
            continue;
        } 
        //controllo il tipo della entry
        if(is_dir(newpath)) //se entry è una directory la aggiungo a rdd->subdirs
            if( !(rdd_add_subdir(rdd, entry->d_name)) ){ closedir(dp); rdd_free(rdd); return NULL; } //controllo errore allocazione
        if(is_reg(newpath)) //se entry è un file regolare lo aggiungo a rdd->files
            if( !(rdd_add_file(rdd, entry->d_name)) ){ closedir(dp); rdd_free(rdd); return NULL; } //controllo errore allocazione
        //newpath va deallocato
        free(newpath);
    }
    if(errno){perror("readdir"); closedir(dp); rdd_free(rdd); return NULL;}
    closedir(dp);

    return rdd;
}



//ignore
char filtr(const char* path){
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


/*
int main(int argc, char** argv){
    if(argc != 2){ fprintf(stderr, "input an argument\n"); return 1; }
    struct r_dirdesc* rdd = NULL;
    
    rdd = rdd_create(argv[1]);
    if(!rdd){fprintf(stderr, "error in rdd_create\n"); return 1;}
    rdd_print(rdd);
    rdd_free(rdd);

    rdd = rdd_filter(argv[1], filtr);
    if(!rdd){fprintf(stderr, "error in rdd_create\n"); return 1;}
    rdd_print(rdd);
    rdd_free(rdd);

    return 0;
}
*/