#!/bin/bash
# Configuracao avaliacao (aplicacao, fora das medicoes de desempenho): uma
# execucao com P = 64 que grava a atribuicao de cada fluxo em
# resultados/membros.bin, para o scripts/eval.py comparar com os rotulos.
# Fica num job separado para nao entrar nas medianas de fases.sbatch.
#SBATCH --job-name=t4-avaliacao
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=16
#SBATCH --exclusive
#SBATCH --time=00:30:00
#SBATCH --output=resultados/%x-%j.out

source slurm/comum.sh
cabecalho
roda 64 ./kmeans_mpi "${ARGS[@]}" -v allreduce -o resultados/membros.bin
if python3 -c "import numpy" 2>/dev/null; then
    python3 scripts/eval.py "${DADOS%.bin}" resultados/membros.bin
else
    echo "sem numpy no no: rode scripts/eval.py fora do cluster (MEDICOES.md, passo 7)"
fi
