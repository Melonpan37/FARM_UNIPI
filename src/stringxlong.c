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
#include "../headers/stringxlong.h"


//ATTENZIONE : pxl deve essere grande abbastanza da contenere nelem+1 elementi. In generale basta che sia grande nelem+1
//la memoria non viene allocata, gestire le free solo per allocazioni esterne
//EFFECT : inserisce pxl_entry ordinatamente risp. a char_p_x_long.value in pxl[]
//MODIFIES : pxl[]
//REQUIRES : pxl grande abbastanza da contenere nelem+1 elementi, pxl != NULL
void stringxlong_add_sorted(stringxlong pxl[], size_t nelem, stringxlong pxl_entry){
    if(!nelem){ pxl[0] = pxl_entry; return; }

    char found = 0;
    for(size_t i = 0; i < nelem; i++){
        if(pxl_entry.val < pxl[i].val){
            for(size_t j = nelem; j > i; j--){
                pxl[j] = pxl[j-1];
            } 
            pxl[i] = pxl_entry;
            found = 1;
            break;
        }
    }
    if(!found) pxl[nelem] = pxl_entry;

    return;
}



//USAGE EXAMPLE
/*
int main(void){
    const int N = 18;

    //STATIC ARRAY
    //BEGIN
    stringxlong pxla[N+1];
    memset(pxla, 0, sizeof(stringxlong)*N+1);
    for(int i = 0; i < N+1; i++){ memset(&pxla[i], 0, sizeof(stringxlong)); printf("%d %s %ld\n", i, pxla[i].string, pxla[i].val); }
    stringxlong pxl;
    memset(&pxl, 0, sizeof(stringxlong));
    for(int i = 0; i < N; i++){
        pxl.val = rand() % 100;
        char stn[4];
        memset(stn, 0, sizeof(char)*4);
        sprintf(stn, "%d", i);
        pxl.string = malloc(sizeof(char)*128);
        memset(pxl.string, 0, sizeof(char)*128);
        strncpy(pxl.string, "pxl", 4);
        strncat(pxl.string, stn, 4);
        printf("bf : %s %ld\n", pxl.string, pxl.val);
        stringxlong_add_sorted(pxla, (size_t) i, pxl);
    }
    for(int i = 0; i < N; i++){
        printf("%s %ld\n", pxla[i].string, pxla[i].val);
        free(pxla[i].string);
    }
    //END

    //DYNAMIC ARRAY
    stringxlong* pxla = NULL;
    stringxlong pxl;
    for(int i = 0; i < N; i++){
        pxl.val = rand()%100;
        char stn[4];
        memset(stn, 0, sizeof(char)*4);
        sprintf(stn, "%d", i);
        pxl.string = malloc(sizeof(char)*128);
        memset(pxl.string, 0, sizeof(char)*128);
        strncpy(pxl.string, "pxl", 3);
        strncat(pxl.string, stn, 4);
        printf("bf : %s %ld\n", pxl.string, pxl.val);
        //EDIT
        stringxlong* temp = realloc(pxla, sizeof(stringxlong)*(i+1));
        if(!temp){perror("realloc"); for(int j = 0; j < i; j++) free(pxla[j].string); free(pxla); return 1; }
        pxla = temp;
        stringxlong_add_sorted(pxla, i, pxl);
    }

    for(int i = 0; i < N; i++){
        printf("%s %ld\n", pxla[i].string, pxla[i].val);
        free(pxla[i].string);
    }
    free(pxla);



    return 0;
}
*/