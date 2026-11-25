#!/bin/bash
# Configuracao seq-t1 (secao 5, item 1): versao sequencial em um no.
# Da T1 (numerador do speed-up), a fracao sequencial s (linha SEQFRAC, para o
# teto de Amdahl) e a referencia de equivalencia (linha RESULTADO, secao 4).
#SBATCH --job-name=t4-seq-t1
#SBATCH --nodes=1
#SBATCH --exclusive
#SBATCH --time=01:30:00
#SBATCH --output=resultados/%x-%j.out

source slurm/comum.sh
cabecalho
for rep in $(seq 1 "$REPS"); do
    echo "---- repeticao $rep"
    ./kmeans_seq "${ARGS[@]}"
done
