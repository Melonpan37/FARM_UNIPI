#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>


#include "../headers/fdtable.h"
#include "../headers/stringxlong.h"
#include "../headers/utils.h"

#include "../headers/debug_flags.h"

//#define DSERVER_VERBOSE

stringxlong* output_list = NULL;
size_t output_size = 0; 

void print_output(){
    fprintf(stdout, "\n\nOUTPUT:\n");
    for(int i = 0; i < output_size; i++)
        printf("%ld %s\n", output_list[i].val, output_list[i].string);
}

volatile sig_atomic_t termination = 0;
void termination_signal_handler(int signum){
    #ifdef DSERVER_VERBOSE
    write(2, "\nSERVER SIGHANDLER\n", 20*sizeof(char));
    #endif
    termination++;
}

volatile sig_atomic_t print_output_request = 0;
void print_signal_handler(int signum){
    #ifdef DSERVER_VERBOSE
    write(2, "\nSERVER PRINT SIGNAL HANDLER\n", sizeof(char)*30); 
    #endif
    print_output_request++;
}

//EFFECT : partiziona la stringa str in due sottostringhe (escludendo delimiter) e le alloca in part1 e part2
//RETURNS : 0 success, 1 not found, 2 alloc error
char partition_message(const char* str, char** part1, char** part2, char delimiter){
    char* delim = NULL;
    delim = strchr(str, delimiter);
    if(!delim) return 1;
    ++delim;
    size_t part1_size, part2_size;
    part2_size = strlen(delim);
    part1_size = strlen(str)-part2_size-1;
    part2_size++; part1_size++;
    *part1 = malloc(sizeof(char)*part1_size);
    if(!*part1){perror("partition message : malloc"); return 2;}
    *part2 = malloc(sizeof(char)*part2_size);
    if(!*part2){perror("partition message : malloc"); return 2;}
    memset(*part1, 0, part1_size); memset(*part2, 0, part2_size);
    strncpy(*part1, str, part1_size-1);
    strncpy(*part2, delim, part2_size-1);
    return 0;
}


//EFFECT : suddivide la stringa argomento in un long e una stringa (path), e poi li inserisce in output_list
//MODIFIES : (stringxlong*) output_list
//RETURNS : 0 on success, 1 on malloc error, 2 realloc fail, 3 not fatal error
//!!!ATTENZIONE!!! client_msg non deallocato, output_list reallocato con nuova dimensione
char client_message_to_output_entry(const char* client_msg){
    //----PARSING MESSAGGIO DEL CLIENT----
    //estrazione del numero inviato dal client
    char* endptr;
    long output_val = strtol(client_msg, &endptr, 10);

    //controllo numero
    if(endptr == client_msg){fprintf(stderr, "SERVER -> CLIENT ERROR : client didn't send a number\n"); return 3; }
    if(*endptr != ' '){fprintf(stderr, "SERVER -> CLIENT ERROR : error in client message\n"); return 3;}
    endptr++;

    //estrazione del path inviato dal client
    char* output_path = NULL;
    output_path = malloc(sizeof(char)*(strlen(endptr)+1));
    if(!output_path){perror("client_message_to_output_list -> malloc"); return 2;} //malloc error

    //----COPIA INFORMAZIONI DA fd_entry A output_entry (strinxlong)----
    //copia della porzione di stringa di fd_entry relativa al filepath
    memset(output_path, 0, strlen(endptr)+1);
    output_path = strncpy(output_path, endptr, strlen(endptr));

    //inserimento in output_list dei valori inviati dal client
    stringxlong output_entry;
    memset(&output_entry, 0, sizeof(stringxlong));
    output_entry.val = output_val;
    output_entry.string = output_path;
    //ingrandimento memoria allocata per output_list
    stringxlong* temp = realloc((stringxlong*) output_list, ++output_size*sizeof(stringxlong));
    if(!temp){ perror("client_message_to_output_list -> realloc"); free(output_path); return 2; }
    output_list = temp;
    //inserimento ordinato sul campo val
    stringxlong_add_sorted(output_list, output_size-1, output_entry);

    return 0;
}

//EFFECT : chiude fd e resetta il bit relativo a fd in set
void close_client_connection(int fd, fd_set* set){
    #ifdef DSERVER_VERBOSE
    printf("server : connection closed by client %d\n", fd);
    #endif
    close(fd);
    FD_CLR(fd, set);
}

/*EFFECT : legge il fd relativo ad un client e gestisce il messaggio :
* 1) terminazione       : se la read restituisce 0 allora si chiude la connessione col client (relativo a fd) 
* 2) lettura normale    : se non ci sono caratteri \n nel messaggio, allora lo si memorizza nella entry relativa a fd in (struct fd_table) fdtb
* 3) lettura completa   : se nel messaggio è presente \n, allora tutto ciò che è memorizzato nel buffer della entry di fdtb relativa a fd, unito
*                         ad eventuali caratteri precedenti a \n vengono memorizzati in output_entry (usando client_message_to_ouptput_entry)
*/
//RETURNS : 0 on success, 1 on (fatal) erro
char read_client_message(int fd, fd_set* set, struct fd_table* fdtb){
    //====LETTURA DEL MESSAGGIO====
    char buf[256];
    memset(buf, (char) 0, 256);
    size_t read_retval = read(fd, (char*) buf, sizeof(char) * 255);

    //controllo errore read
    if(read_retval < 0){ 
        perror("SERVER : read"); 
        //se è arrivato SIGPIPE, si ignora e si gestisce come una disconnessione
        if(errno == EPIPE) {read_retval = 0;}
        //read interrotta da un segnale prima che si sia potuto leggere
        if(errno == EINTR) return 0;
        else return 1; 
    } 

    //====CHIUSURA CONNESSIONE DA CLIENT====
    if(read_retval == 0){ //chiusura connessione da un client
        
        //chiusura comunicazione col client
        close_client_connection(fd, set);
        
        //rimozione entry relativa a fd in fdtb (si occupa anche della deallocazione del buffer di fdtb->ent[idx].buf)
        switch(fd_table_remove(fdtb, fd)){ 
            case(0) : {
                //OK
                break;
            }
            case(1) : {
                fprintf(stderr, "\nSERVER ERROR : cannot find fd %d to be removed from fdtb\n", fd);
                fprintf(stderr, "printing fdtb before server shuts down\n");
                fd_table_print(*fdtb);
                return 1;
            }
            case(2) : {
                fprintf(stderr, "\nSERVER ERROR : alloc fail\n"); 
                return 1;
            } 
        }

        //successo chiusura comunicazione
        return 0;
    } 

    #ifdef DSERVER_VERBOSE
    printf("server : received message from client %d :\n%s\n", fd, buf);
    #endif

    //====MESSAGGIO CON CARATTERE DI COMPLETAMENTO====
    //il messaggio contiene almeno un carattere '\n' che significa che il messaggio relativo ad un file è completato
    char* buf_idx = buf;
    if(strchr(buf, '\n')) while(1){ //potrebbero esserci più di un '\n' in un singolo messaggio
        #ifdef DSERVER_VERBOSE
        printf("server : message from %d contains at least one completition character in buf : %s\n", fd, buf_idx);
        #endif

        //ricerca della entry relativa al fd del client
        size_t fd_entry_index = fd_table_find(*fdtb, fd);
        if(fd_entry_index == -1){ //FATAL (entry non trovata) 
            fprintf(stderr, "SERVER ERROR : can't find %d in fdtb for message completion\n", fd);
            return 1;
        }

        //partizionamento del messaggio (si divide rispetto al delimitatore '\n')
        char* p1 = NULL;
        char* p2 = NULL;
        if(partition_message(buf_idx, &p1, &p2, '\n')){return 1;} //errore allocazione


        //scruttura sul buffer di fd se c'è ancora una parte di messaggio prima del carattere di terminazione
        if(*p1 != '\0') if(fd_table_write_buffer(fdtb, fd, p1)) return 1; //errore allocazione (l'errore fd non trovato è già controllato prima)


        //controllo che il client abbia inviato davvero un messaggio
        if(!fdtb->ent[fd_entry_index].buf_size){
            fprintf(stderr, "CLIENT ERROR : client %d sent completition message before writing a message\n", fd);
            free(p1); free(p2);
            return 0; //NON FATAL
        }
    
        //copia del buffer della entry in output_list
        switch(client_message_to_output_entry(fdtb->ent[fd_entry_index].buf)){
            case(0) : {
                //OK
                break;
            }
            case(3) : {
                //NON FATAL (errore nella formattazione del messaggio) -> il messaggio viene scartato
                break;
            }
            default : {
                fprintf(stderr, "\nSERVER ERROR : alloc fail\n"); 
                free(p1); free(p2);
                return 1;
            }
        }

        //reset del buffer dei messaggi di fd
        fd_table_clear_buffer(fdtb, fd);


        free(p1); free(p2);
        
        buf_idx = strchr(buf_idx, '\n');
        ++buf_idx;
        if(buf_idx == NULL) return 0;
        if(*buf_idx == '\0') return 0;
    }

    //====MESSAGGIO NORMALE====
    //scrittura del messaggio in append nel buffer della fd_entry relativa a fd nella fd_table
    switch(fd_table_write_buffer(fdtb, fd, buf)){
        //errore
        case(1) : {
            fprintf(stderr, "\nSERVER ERROR : cannot find fd %d in fdtb whom buffer has to be written\n", fd);
            fprintf(stderr, "printing fdtb state before server shuts down\n");
            fd_table_print(*fdtb);
            return 1;
        }
        case(2) : {
            fprintf(stderr, "\nSERVER ERROR : alloc fail\n"); 
            return 1;
        }
    }
    
    return 0;    
}

//funzione di terminazione in seguito a errore
void fail(struct fd_table* fdtb, int sock_fd){
    if(unlink("./farm.sck") == -1) perror("unlink");
    if( close(sock_fd) == -1 ) perror("close socket fd");
    fd_table_free(fdtb);
    for(size_t i = 0; i < output_size; i++) if(output_list[i].string) free(output_list[i].string);
    free(output_list);

    fprintf(stderr, "\n\nCOLLECTOR : FAIL AFTER FATAL ERROR\n\n");
    exit(EXIT_FAILURE); 
}


int main(void){
    printf("SERVER PID : %d\n", getpid());

    //====INSTAL SIGNAL HANDLERS====
    //handle sigusr2 for termination
    struct sigaction sigact;
    memset(&sigact, 0, sizeof(struct sigaction));
    sigact.sa_handler = termination_signal_handler;
    if( (sigaction(SIGUSR2, &sigact, NULL)) ){ perror("COLLECTOR : sigaction"); exit(EXIT_FAILURE); }
     
    //handler sigusr1 for print otuput
    memset(&sigact, 0, sizeof(struct sigaction));
    sigact.sa_handler = print_signal_handler;
    if( (sigaction(SIGUSR1, &sigact, NULL)) ){ perror("COLLECTOR : sigaction"); exit(EXIT_FAILURE); }

    //mask signals handled by farm main thread 
    sigset_t sigset;
    if(sigemptyset(&sigset)) {perror("COLLECTOR : sigemptyset"); exit(EXIT_FAILURE);}
    if(sigaddset(&sigset, SIGINT)){perror("COLLECTOR : sigaddset"); exit(EXIT_FAILURE);}
    if(sigaddset(&sigset, SIGTERM)){perror("COLLECTOR : sigaddset"); exit(EXIT_FAILURE);}
    if(sigaddset(&sigset, SIGHUP)){perror("COLLECTOR : sigaddset"); exit(EXIT_FAILURE);}
    if(sigaddset(&sigset, SIGQUIT)){perror("COLLECTOR : sigaddset"); exit(EXIT_FAILURE);}
    if(sigaddset(&sigset, SIGPIPE)){perror("COLLECTOR : sigaddset"); exit(EXIT_FAILURE);}
    if(pthread_sigmask(SIG_SETMASK, &sigset, NULL)){fprintf(stderr, "COLLECTOR : pthread_sigmask error\n"); exit(EXIT_FAILURE);}

    //====INIT LISTENING SOCKET====
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(sock_fd == -1){ perror("socket"); return -1; }
    struct sockaddr_un sa;
    strncpy(sa.sun_path, "./farm.sck", 108);
    sa.sun_family = AF_UNIX;
    if( bind(sock_fd, (struct sockaddr*) &sa, sizeof(sa)) ){ perror("bind"); close(sock_fd); return -1; }
    if( listen(sock_fd, 64) ){ perror("listen"); close(sock_fd); return -1; }
    socklen_t sock_len = sizeof(sa);
    
    fd_set set, rdset, excset;
    FD_ZERO(&set); FD_SET(sock_fd, &set);
    int max_fd = sock_fd;

    #ifdef DSERVER_VERBOSE
    printf("server initialized socket\n");
    #endif

    //init fd_table (tabella : [client fd] -> [buffer messaggi] )
    struct fd_table fdtb = fd_table_init();


    while(1){

        //check termination condition (received signal and no client is communicating)
        if(termination && !fdtb.size) break;
        //check print output signal received
        if(print_output_request){print_output(); print_output_request = 0;}

        //====POLLING CANALI DI COMUNICAZIONE====
        struct timeval timeout;
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        rdset = set;
        excset = set;
        int select_result;
        if( (select_result = select(max_fd+1, &rdset, NULL, &excset, &timeout) ) == -1){
            //----CONTROLLO ERRORI SELECT----
            //---ricevuto segnale----
            if(errno == EINTR){
                //il segnale è stato gestito
                if(print_output_request) continue;
                if(termination) continue; 
                //il segnale non è stato gestito
                else{ fprintf(stderr, "COLLECTOR : received unhandled signal\n"); fail(&fdtb, sock_fd); }
            }
            //----errore sigpipe----
            if(errno == EPIPE) {
                //----il client viene considerato disconnesso----
                for(int fd = 0; fd < max_fd+1; fd++) if(FD_ISSET(fd, &excset)) {
                    close_client_connection(fd, &set);
                    if(fd_table_remove(&fdtb, fd)) fail(&fdtb, sock_fd);
                }
                continue;
            }
            perror("COLLECTOR : select");
            //----altri errori (exit)----
            fail(&fdtb, sock_fd);
        }

        //select interrotta per timeout
        if(!select_result) continue;

        //====PARSE COMMUNICATION FDS====
        for(int fd = 0; fd < max_fd+1; fd++){
            if(FD_ISSET(fd, &rdset)){
                //FD NEW CONNECTION
                if(fd == sock_fd){ 

                    //se siamo in terminazione non vengono accettate nuove richieste
                    if(termination) continue; 
                    
                    //accettazine nuova connessione
                    int newconn_fd; 
                    if((newconn_fd = accept(fd, (struct sockaddr*)&sa, &sock_len)) == -1){ perror("accept"); continue; }
                    FD_SET(newconn_fd, &set);
                    if(newconn_fd > max_fd) max_fd = newconn_fd;

                    //creazione entry relativa al fd del client nella fd_table
                    if(fd_table_append(&fdtb, newconn_fd)){ 
                        fprintf(stderr, "SERVER ERROR : alloc fail\n"); 
                        break; 
                    }

                    #ifdef DSERVER_VERBOSE
                    printf("server received new connection from fd %d\n", newconn_fd);
                    #endif
                }
                //FD READY TO READ
                else if(read_client_message(fd, &set, &fdtb)) break;
            }
        }
    }

    //====TERMINATION====
    #ifdef DSERVER_VERBOSE 
    fprintf(stdout, "SERVER : termination\n");
    #endif

    //print result
    print_output();
    
    //free structs
    for(size_t i = 0; i < output_size; i++) if(output_list[i].string) free(output_list[i].string);
    free(output_list);
    if(fdtb.size) fprintf(stderr, "SERVER ERROR : fdtb is not empty on termination\n");
 
    //handle files
    if(close(sock_fd) == -1) perror("close"); //procedes to unlink anyway
    if(unlink("./farm.sck") == -1){ perror("unlink"); return -1; }


    return 0;
    
}


