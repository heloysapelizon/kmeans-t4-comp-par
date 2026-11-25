#!/bin/bash
# Compila e submete os jobs de medicao, sempre nos mesmos nos (config.sh: NOS).
# Cada configuracao de MEDICOES.md tem um script slurm/<nome>.sbatch.
# Uso, a partir da raiz do repositorio:
#   ./slurm/submete.sh calibra              so a calibracao
#   ./slurm/submete.sh                      todas as medicoes (sem a calibracao)
#   ./slurm/submete.sh fases seq-t1 ...     so as configuracoes indicadas
set -eu
cd "$(dirname "$0")/.."
source ./config.sh
eval "$AMBIENTE"

# A sequencial (e a calibracao) roda no primeiro no de NOS; as paralelas nos 4.
SEQUENCIAIS="seq-t1"
PARALELAS="fases verificacao-bcast avaliacao"

[ $# -eq 0 ] && set -- $SEQUENCIAIS $PARALELAS

for nome in "$@"; do
    [ -f "slurm/$nome.sbatch" ] || { echo "configuracao desconhecida: $nome" >&2; exit 1; }
done
if [ "$*" != "calibra" ] && [ ! -f "$DADOS" ]; then
    echo "entrada $DADOS nao existe: rode scripts/preparar.sh (MEDICOES.md, passo 1)" >&2
    exit 1
fi

make
mkdir -p resultados

PART=""
[ -n "$PARTICAO" ] && PART="--partition=$PARTICAO"
NO1=${NOS%%,*}

for nome in "$@"; do
    case " calibra $SEQUENCIAIS " in
        *" $nome "*) sbatch $PART -w "$NO1" "slurm/$nome.sbatch" ;;
        *)           sbatch $PART -w "$NOS" "slurm/$nome.sbatch" ;;
    esac
done
squeue -u "$USER"
