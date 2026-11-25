# Makefile do T4. O Makefile original (Liao/Giuroiu) esta no historico do git.
# As duas versoes saem do mesmo mpicc, com as mesmas flags e o mesmo objeto
# do nucleo (kmeans_comum.o): o ponto flutuante da distancia e identico.
# Sem -march=native e com -ffp-contract=off (sem FMA): o resultado nao depende
# do no onde foi compilado/executado.
MPICC  ?= mpicc
CFLAGS  = -std=c11 -O3 -Wall -Wextra -ffp-contract=off -DNDEBUG
SRC     = src

all: kmeans_seq kmeans_mpi

kmeans_comum.o: $(SRC)/kmeans_comum.c $(SRC)/kmeans.h
	$(MPICC) $(CFLAGS) -c $< -o $@

kmeans_seq: $(SRC)/kmeans_seq.c kmeans_comum.o $(SRC)/kmeans.h
	$(MPICC) $(CFLAGS) $< kmeans_comum.o -o $@

kmeans_mpi: $(SRC)/kmeans_mpi.c kmeans_comum.o $(SRC)/kmeans.h
	$(MPICC) $(CFLAGS) $< kmeans_comum.o -o $@

clean:
	rm -f kmeans_seq kmeans_mpi kmeans_comum.o

.PHONY: all clean
