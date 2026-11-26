# Funcoes comuns aos jobs. Incluido com `source` pelos .sbatch.
# Adaptado do slurm/comum.sh do nosso T3, que ja rodou no Atlantica.
set -u
cd "$SLURM_SUBMIT_DIR"
source ./config.sh
eval "$AMBIENTE"

ARGS=(-i "$DADOS" -k "$K" -s "$AMOSTRA" -r "$SEMENTE")

# Processos em bloco (ranks 0..P/nos-1 no primeiro no). O binding de CPU vem
# de bind_para: cada processo fica preso ao seu nucleo (ou thread).
SRUN_OPTS="--distribution=block $SRUN_MPI"

cabecalho() {
    echo "==== job $SLURM_JOB_ID ($SLURM_JOB_NAME) em $(date "+%Y-%m-%d %H:%M:%S")"
    echo "nos: $SLURM_JOB_NODELIST   DADOS=$DADOS K=$K AMOSTRA=$AMOSTRA SEMENTE=$SEMENTE REPS=$REPS"
    echo "git: $(git rev-parse --short HEAD 2>/dev/null)"
    echo "---- lscpu (no $(hostname))"
    lscpu | grep -E '^(Model name|Socket|Core|Thread|CPU\(s\)|NUMA node\(s\))'
    echo "---- como o Slurm conta as CPUs deste no (os scripts supoem CPUTot=16)"
    scontrol show node "$(hostname -s)" 2>/dev/null \
        | grep -oE "(CPUTot|ThreadsPerCore|CoresPerSocket|Sockets)=[0-9]+" | tr '\n' ' '
    echo
    echo "---- Open MPI"
    (mpirun --version 2>/dev/null || ompi_info --version 2>/dev/null) | head -1
    echo "---- entrada"
    ls -l "$DADOS" "${DADOS%.bin}.json" 2>/dev/null
    echo "===="
}

# Nos para P processos (secao 5): 1 no ate 8, 2 com 16, 4 com 32 e 64.
nos_para() {
    if   [ "$1" -le 8 ];  then echo 1
    elif [ "$1" -le 16 ]; then echo 2
    else                       echo 4
    fi
}

# CPUs do Slurm (threads de hardware) por processo. Com ate 8 processos por no,
# 2 CPUs = um nucleo fisico inteiro (sem isso, 8 processos poderiam ficar em 4
# nucleos, 2 por nucleo via HT). Com 16 por no (P = 64), 1 CPU: usa o HT.
cpt_para() {
    local nos por_no
    nos=$(nos_para "$1")
    por_no=$(( ($1 + nos - 1) / nos ))
    if [ "$por_no" -le 8 ]; then echo 2; else echo 1; fi
}

# Binding: com -c 2, cada processo preso as 2 threads de um nucleo fisico
# (cores); com -c 1 (P = 64, HT), preso a uma thread (threads). Com
# --cpu-bind=none (a 1a medicao), o Linux chegou a por dois processos nas duas
# threads do mesmo nucleo com P = 32: um processo 2x mais lento e os outros 31
# esperando por ele (resultados/cpu-bind-none/).
bind_para() {
    if [ "$(cpt_para "$1")" -eq 2 ]; then echo cores; else echo threads; fi
}

# roda P programa args...
roda() {
    local P=$1; shift
    srun -N "$(nos_para "$P")" -n "$P" -c "$(cpt_para "$P")" \
         --cpu-bind="$(bind_para "$P")" $SRUN_OPTS "$@"
}

# Registra quantos processos ficaram em cada no e a mascara de CPUs de cada um
# (linhas MASCARA, do --cpu-bind=verbose), para conferir que nao ha dois
# processos no mesmo nucleo.
mostra_distribuicao() {
    local P=$1 saida
    saida=$(srun -N "$(nos_para "$P")" -n "$P" -c "$(cpt_para "$P")" \
                 --cpu-bind="verbose,$(bind_para "$P")" $SRUN_OPTS hostname 2>&1)
    echo "DISTRIBUICAO P=$P (srun -N $(nos_para "$P") -n $P -c $(cpt_para "$P")" \
         "--cpu-bind=$(bind_para "$P") $SRUN_OPTS):" \
         "$(echo "$saida" | grep -v cpu-bind | sort | uniq -c | tr '\n' ' ')"
    echo "$saida" | grep cpu-bind | sed "s/^/MASCARA P=$P /"
}

# na_ordem REP lista...: crescente nas repeticoes impares, decrescente nas
# pares (do T2: evita que um efeito de ordem se confunda com o efeito de P).
na_ordem() {
    local rep=$1; shift
    local lista=("$@") i
    if [ $(( rep % 2 )) -eq 1 ]; then
        echo "${lista[@]}"
    else
        for (( i = ${#lista[@]} - 1; i >= 0; i-- )); do printf '%s ' "${lista[i]}"; done
        echo
    fi
}
