CC = gcc
CFLAGS = -Wall -std=c11 -g -o
OFLAGS = -Wall -std=c11 -c -o
DDEBUG = -DDEBUG

HEAD_DIR = ./headers/
OBJ_DIR = ./obj/
SRC_DIR = ./src/

OBJS = $(OBJ_DIR)*.o
HEADERS = $(HEAD_DIR)*.h

.PHONY : clean test debug_master debug_collector debug_pthreads debug_clear

farm : $(SRC_DIR)main.c $(OBJ_DIR)utils.o $(OBJ_DIR)opts.o $(OBJ_DIR)rdd.o $(OBJ_DIR)cqueue.o $(OBJ_DIR)worker.o $(OBJ_DIR)master.o collector $(HEADERS) 
	$(CC) $(CFLAGS) $@ $< $(OBJ_DIR)utils.o $(OBJ_DIR)opts.o $(OBJ_DIR)rdd.o $(OBJ_DIR)cqueue.o $(OBJ_DIR)worker.o $(OBJ_DIR)master.o -I $(HEAD_DIR) -lpthread

$(OBJ_DIR)master.o : $(SRC_DIR)master.c $(OBJ_DIR)cqueue.o $(OBJ_DIR)utils.o $(HEADERS)
	$(CC) $(OFLAGS) $@ $< -I $(HEAD_DIR)

$(OBJ_DIR)worker.o : $(SRC_DIR)worker.c $(HEADERS)
	$(CC) $(OFLAGS) $@ $< -I $(HEAD_DIR)

$(OBJ_DIR)opts.o : $(SRC_DIR)opts.c $(HEADERS)
	$(CC) $(OFLAGS) $@ $< -I $(HEAD_DIR)

$(OBJ_DIR)rdd.o : $(SRC_DIR)rdd.c $(HEADERS)
	$(CC) $(OFLAGS) $@ $< -I $(HEAD_DIR)

$(OBJ_DIR)cqueue.o : $(SRC_DIR)cqueue.c $(HEADERS)
	$(CC) $(OFLAGS) $@ $< -I $(HEAD_DIR)

$(OBJ_DIR)utils.o : $(SRC_DIR)utils.c 
	$(CC) $(OFLAGS) $@ $< 

collector : $(SRC_DIR)collector.c $(OBJ_DIR)fdtable.o $(OBJ_DIR)stringxlong.o $(HEADERS)
	$(CC) $(CFLAGS) $@ $< $(OBJ_DIR)fdtable.o $(OBJ_DIR)stringxlong.o -I $(HEAD_DIR) -lpthread 

$(OBJ_DIR)fdtable.o : $(SRC_DIR)fdtable.c $(HEADERS)
	$(CC) $(OFLAGS) $@ $< -I $(HEAD_DIR)

$(OBJ_DIR)stringxlong.o : $(SRC_DIR)stringxlong.c $(HEADERS)
	$(CC) $(OFLAGS) $@ $< -I $(HEAD_DIR)


test : test.sh generafile.c
	gcc -o generafile generafile.c
	./test.sh
	rm generafile
	rm *.dat
	rm -r testdir
	rm expected.txt

clean :
	-rm $(OBJS)

debug_master : 
	echo "#define DEBUG\n" >> $(HEAD_DIR)debug_flags.h

debug_collector : 
	echo "#define DSERVER_VERBOSE\n" >> $(HEAD_DIR)debug_flags.h

debug_pthreads : 
	echo "#define PTDB\n" >> $(HEAD_DIR)debug_flags.h

debug_clear :
	echo "" > $(HEAD_DIR)debug_flags.h

	

