#!/bin/bash
# Calibracao: escolhe a entrada (DADOS) e K para que T1 fique entre 30 e 60 s.
# Roda a sequencial uma vez para cada dados/ctu13_*.bin x K. No k-means o
# tempo e ~ N * K * iteracoes, e as iteracoes dependem dos dados: por isso
# mede em vez de extrapolar.
#SBATCH --job-name=t4-calibra
#SBATCH --nodes=1
#SBATCH --exclusive
#SBATCH --time=01:30:00
#SBATCH --output=resultados/%x-%j.out

source slurm/comum.sh
cabecalho
for dados in dados/ctu13_*.bin; do
    for k in ${CALIBRA_K:-8 16 32}; do
        echo "---- CALIBRA dados=$dados K=$k"
        ./kmeans_seq -i "$dados" -k "$k" -s "$AMOSTRA" -r "$SEMENTE" \
            | grep -E '^(iteracoes|tempo)'
    done
done
