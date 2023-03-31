#ifndef STRINXLONG
#define STRINGXLONG

typedef struct char_p_x_long{
    char* string; 
    long val;
} stringxlong;


//ATTENZIONE : pxl deve essere grande abbastanza da contenere nelem+1 elementi. In generale basta che sia grande nelem+1
//la memoria non viene allocata, gestire le free solo per allocazioni esterne
//EFFECT : inserisce pxl_entry ordinatamente risp. a char_p_x_long.value in pxl[]
//MODIFIES : pxl[]
//REQUIRES : pxl grande abbastanza da contenere nelem+1 elementi, pxl != NULL
void stringxlong_add_sorted(stringxlong pxl[], unsigned long nelem, stringxlong pxl_entry);


#endif