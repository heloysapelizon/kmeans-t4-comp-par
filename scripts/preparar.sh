#!/bin/bash
# Baixa os fluxos bidirecionais rotulados do CTU-13 e gera a entrada do k-means.
# Uso (na raiz do repositorio):
#   bash scripts/preparar.sh [cenarios...]        padrao: cenario 1
#   MAX_FLUXOS=1000000 bash scripts/preparar.sh   amostra regular de 1M fluxos
# Cenarios: 1 (Neris, ~2,8M fluxos, 369 MB) e 10 (Rbot, DDoS UDP, ~1,3M, 171 MB).
# Saida: dados/ctu13_s<cenarios>[_m<MAX>].{bin,rotulos,json}
set -euo pipefail
cd "$(dirname "$0")/.."
BASE=https://mcfp.felk.cvut.cz/publicDatasets
declare -A URL=(
    [1]="$BASE/CTU-Malware-Capture-Botnet-42/detailed-bidirectional-flow-labels/capture20110810.binetflow"
    [10]="$BASE/CTU-Malware-Capture-Botnet-51/detailed-bidirectional-flow-labels/capture20110818.binetflow"
)
CENARIOS=("${@:-1}")
mkdir -p dados
arquivos=()
for c in "${CENARIOS[@]}"; do
    destino="dados/ctu13_cenario$c.binetflow"
    [ -f "$destino" ] || curl -fL "${URL[$c]}" -o "$destino"
    arquivos+=("$destino")
done
nome="dados/ctu13_s$(IFS=_; echo "${CENARIOS[*]}")"
extra=()
if [ -n "${MAX_FLUXOS:-}" ]; then
    nome="${nome}_m$MAX_FLUXOS"
    extra=(--max "$MAX_FLUXOS")
fi
python3 scripts/prep.py "${arquivos[@]}" -o "$nome" ${extra[@]+"${extra[@]}"}
