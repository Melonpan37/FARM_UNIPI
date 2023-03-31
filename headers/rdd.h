#ifndef RDD_H
#define RDD_H

#include <dirent.h>
#include <sys/types.h>

//descrittore directory con path relativi
struct r_dirdesc {
    char* dir_rpath;
    char** subdirs;
    size_t subdirs_size;
    char** files;
    size_t files_size;
};
        
//!!!ATTENZIONE!!! il valore di ritorno deve essere deallocato
char* path_to_file(const char* path, const char* filename);

void rdd_print(struct r_dirdesc* rdd);

//inizializza un puntatore a r_dirdesc
//rdd deve essere deallocato
//returns : NULL se errore, rdd altrimenti
struct r_dirdesc* rdd_init();

//assegna il valore di path a rdd->dir_rpath
//rdd->dir_rpath deve essere deallocato 
//REQUIRES : rdd->dir_rpath = NULL
//RETURNS : 0 se errore (allocazione o dir_rpath già esistente), 1 altrimenti
char rdd_rpath(struct r_dirdesc* rdd, const char* path);

//aggiunge subdir a rdd->subdirs e aggiorna rdd->subdirs_size
//rdd->subdirs[i] e rdd->subdirs devono essere deallocati
//REQUIRES : rdd->dir_rpath != NULL
//returns : 0 se errore (allocazione), 1 altrimenti
char rdd_add_subdir(struct r_dirdesc* rdd, char* subdir);

//aggiunge subdir a rdd->files e aggiorna rdd->files_size
//rdd->files[i] e rdd->files devono essere deallocati
//REQUIRES : rdd->dir_rpath != NULL
//returns : 0 se errore (allocazione), 1 altrimenti
char rdd_add_file(struct r_dirdesc* rdd, char* file);

//dealloca rdd->files[i], rdd->subdirs[i], rdd->files, rdd->subfiles, e rdd
void rdd_free(struct r_dirdesc* rdd);


//naviga la cartella path e costruisce il rdd relativo
//rdd, rdd->r_path, rdd->files, rdd->subdirs, rdd->files[i], rdd->subdirs[i] devono essere deallocati
//returns : NULL se errore, rdd altrimenti
struct r_dirdesc* rdd_create(const char* path);

//naviga la cartella path e costruisce il rdd relativo
//rdd, rdd->r_path, rdd->files, rdd->subdirs, rdd->files[i], rdd->subdirs[i] devono essere deallocati
//returns : NULL se errore, rdd altrimenti
struct r_dirdesc* rdd_filter(const char* path, char (*filter)(const char* path));


#endif