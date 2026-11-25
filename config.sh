# Configuracao comum a todos os jobs (lida por slurm/comum.sh e slurm/submete.sh).
# Unico arquivo a editar antes de medir. Ver MEDICOES.md, passo 0.

# Entrada do k-means, gerada por scripts/preparar.sh (passo 1).
DADOS="dados/ctu13_s1.bin"

# Numero de clusters. Escolher na calibracao (passo 2).
K=16

# Pontos da amostra do k-means++ e semente: fixos em todas as medicoes.
AMOSTRA=100000
SEMENTE=42

# Repeticoes por configuracao (mesma convencao do T2/T3).
REPS=10

# Os mesmos 4 nos em todos os jobs. A sequencial roda no primeiro; P <= 8 no
# primeiro, P = 16 nos dois primeiros, P = 32 e 64 nos quatro (secao 5).
NOS="atlantica03,atlantica04,atlantica05,atlantica06"

# Particao do Slurm (vazio = padrao).
PARTICAO="LAD_geral"

# Opcoes extras do srun para lancar o Open MPI (ex.: "--mpi=pmix"), se precisar.
SRUN_MPI=""

# Comandos para carregar o ambiente (ex.: "module load openmpi"), se houver.
AMBIENTE=""
