#!/bin/bash
# Configuracao fases (secao 5, itens 1 e 2): versao MPI com P = 1..64 e a
# verificacao por um MPI_Allreduce. Da T_P (speed-up e eficiencia) e o tempo
# de cada fase (media, minimo e maximo entre os processos, linha FASES).
# Os 4 nos ficam reservados; cada P usa so os de nos_para (comum.sh).
#SBATCH --job-name=t4-fases
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=16
#SBATCH --exclusive
#SBATCH --time=03:00:00
#SBATCH --output=resultados/%x-%j.out

source slurm/comum.sh
cabecalho

PROCS="1 2 4 8 16 32 64"
for P in $PROCS; do mostra_distribuicao "$P"; done

for rep in $(seq 1 "$REPS"); do
    echo "---- repeticao $rep"
    for P in $(na_ordem "$rep" $PROCS); do
        roda "$P" ./kmeans_mpi "${ARGS[@]}" -v allreduce
    done
done
