#!/bin/bash
# Configuracao verificacao-bcast (secao 6, item 2): a mesma serie de fases.sbatch,
# com a verificacao feita por P MPI_Bcast (um por processo) em vez de um
# MPI_Allreduce. Compara o custo das duas coletivas (menor tempo da verificacao).
#SBATCH --job-name=t4-verificacao-bcast
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=16
#SBATCH --exclusive
#SBATCH --time=03:00:00
#SBATCH --output=resultados/%x-%j.out

source slurm/comum.sh
cabecalho

PROCS="2 4 8 16 32 64"
for P in $PROCS; do mostra_distribuicao "$P"; done

for rep in $(seq 1 "$REPS"); do
    echo "---- repeticao $rep"
    for P in $(na_ordem "$rep" $PROCS); do
        roda "$P" ./kmeans_mpi "${ARGS[@]}" -v bcast
    done
done
