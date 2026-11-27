# Medições do T4

Este documento lista o que o enunciado do T4 manda medir e descreve, passo a passo, como
rodar cada medição no Atlântica. Cada configuração tem o seu script em
`slurm/<nome>.sbatch`, e todos leem os parâmetros de `config.sh`.

## 1. O que o enunciado exige

### 1.1 Catálogo

| # | Exigência | Seção | Grandeza | Onde sai | Configuração |
|---|---|---|---|---|---|
| 1 | Toda execução imprime o número de iterações e um resumo **iguais aos da sequencial** | 4 | iterações, inércia, checksum | linha `RESULTADO` | todas |
| 2 | Nós exclusivos, o mínimo para um processo por núcleo físico: 1 nó até 8 processos, 2 com 16, 4 com 32 e 64 (HT) | 5 | — | linha `DISTRIBUICAO` | todas as paralelas |
| 3 | Tempo sequencial T1 (numerador do speed-up) | 5.1 | T1 | `TEMPOS,seq,...` | seq-t1 |
| 4 | Fração sequencial **s medida na versão sequencial** (teto de Amdahl) | 5.1 | s | `SEQFRAC,...` | seq-t1 |
| 5 | Speed-up e eficiência com **P = 1, 2, 4, 8, 16, 32 e 64** | 5.1 | T_P, S = T1/T_P, E = S/P | `TEMPOS,mpi_allreduce,P,...` | fases |
| 6 | Gráfico de speed-up com **três curvas**: ideal (S = P), teto de Amdahl e medido | 5.1 | — | `speedup.png` | analise.py |
| 7 | Tempo por fase em **toda a série de P**: computação local, verificação e troca, **média entre os processos**, barras empilhadas (uma por P) | 5.2 | médias por fase | `FASES,...` → `fases.png` | fases |
| 8 | Na verificação, também o **menor tempo entre os processos** (custo da comunicação); média − menor = **espera** pelo mais lento | 5.2 e 6.2 | custo (menor tempo por iteração, somado) e média da verificação | `VERIFCUSTO,...` e `FASES,...` | fases |
| 9 | Qual coletiva foi usada na verificação (**P broadcasts** ou **um `MPI_Allreduce`**) e **quanto ela custa** | 6.2 | custo da verificação, nas duas coletivas | `VERIFCUSTO,...` → `verificacao.png` | fases × verificacao-bcast |
| 10 | Quanto do tempo vai para verificação e troca, e quanto disso é espera | 6.2 | % por fase | `fases.png` (painel %) | fases |
| 11 | Efeito do HT | 6.3 | S e E com P = 64 (16 por nó, HT) × P = 32 (8 por nó) | `tabela.csv` | fases |
| 12 | Distância entre o speed-up medido e o teto de Amdahl | 6.3 | S medido × teto | `speedup.png`, `tabela.csv` | analise.py |
| 13 | Código, scripts sbatch e **saídas do Slurm** num repositório público | 6 | — | `resultados/*.out` | entrega |

Duas configurações não são exigidas pelo enunciado:
- **calibra:** escolhe a entrada e o K. O enunciado do T4 não fixa um tempo; mantive o
  critério do T2/T3 (T1 entre 30 e 60 s) e 10 repetições, para caber na fila.
- **avaliacao:** grava as atribuições de cada fluxo para o `eval.py`. É a parte de
  segurança do trabalho e fica fora das medições de desempenho.

### 1.2 Ordem e por quê

1. **calibra** (só a sequencial): fixa `DADOS` e `K` em `config.sh`. Todas as medições
   dependem desses dois valores: se mudarem, tudo é refeito.
2. **seq-t1**: dá T1, s e o `RESULTADO` de referência, que todas as execuções paralelas
   precisam repetir (exigência 1).
3. **fases**: a série principal (exigências 5, 7, 8, 10 e 11).
4. **verificacao-bcast**: a mesma série com a outra coletiva (exigência 9).
5. **avaliacao**: atribuições para a análise de segurança.
6. Conferir as saídas e rodar `analise.py` (exigências 6 e 12), depois versionar os
   `.out` (exigência 13).

## 2. Visão geral das configurações

| Configuração | Script | Programa | P | Nós | Mede |
|---|---|---|---|---|---|
| calibra | `calibra.sbatch` | `kmeans_seq` | 1 | 1 | T1 para cada entrada × K (1 vez cada) |
| seq-t1 | `seq-t1.sbatch` | `kmeans_seq` | 1 | 1 | T1, s, `RESULTADO` de referência |
| fases | `fases.sbatch` | `kmeans_mpi -v allreduce` | 1, 2, 4, 8, 16, 32, 64 | 1, 1, 1, 1, 2, 4, 4 | T_P e tempo por fase |
| verificacao-bcast | `verificacao-bcast.sbatch` | `kmeans_mpi -v bcast` | 2, 4, 8, 16, 32, 64 | 1, 1, 1, 2, 4, 4 | custo da verificação com P broadcasts |
| avaliacao *(aplicação)* | `avaliacao.sbatch` | `kmeans_mpi -o` | 64 | 4 | atribuições → `eval.py` |

## 3. O que vale para todas as configurações

Estes parâmetros estão em `config.sh` e em `slurm/comum.sh`.

- **Entrada:** `DADOS` (fluxos do CTU-13 quantizados por `scripts/prep.py`), `K`, `AMOSTRA`
  (pontos do k-means++) e `SEMENTE`. Os mesmos em todas as execuções.
- **Repetições:** `REPS=10`. O laço de repetições é o de fora: cada rodada passa por todos
  os P. Nas paralelas, a ordem dos P alterna, crescente nas rodadas ímpares e decrescente
  nas pares, para um efeito de ordem (temperatura, turbo) não se confundir com P.
- **Nós:** sempre os 4 de `NOS`, com `--exclusive`. A sequencial e a calibração rodam no
  primeiro. Nas paralelas, o job reserva os 4 nós e cada P usa só os de que precisa.

  | P | nós | processos por nó | `-c` | núcleos |
  |---|---|---|---|---|
  | 1, 2, 4, 8 | 1 | P | 2 | um físico por processo |
  | 16 | 2 | 8 | 2 | um físico por processo |
  | 32 | 4 | 8 | 2 | um físico por processo |
  | 64 | 4 | 16 | 1 | HT: duas threads por núcleo |

  - `-c 2` reserva as duas threads de um núcleo. Sem isso, 8 processos poderiam cair em 4
    núcleos físicos, como observado no T3.
  - Os processos são distribuídos em bloco (`--distribution=block`) e presos ao seu
    núcleo: `--cpu-bind=cores` com `-c 2` e `--cpu-bind=threads` com `-c 1` (P = 64).
    As linhas `MASCARA` do `.out` mostram a máscara de cada processo.
  - Na primeira medição, com `--cpu-bind=none` (como no T3), o Linux chegou a pôr dois
    processos nas duas threads do mesmo núcleo com P = 32 em 4 ou 5 das 10 repetições:
    um processo 2× mais lento e os outros 31 esperando por ele na verificação. Essas
    saídas ficam em `resultados/cpu-bind-none/`, fora da análise, como exemplo de espera.
- **Região medida:** depois de uma barreira, da inicialização (amostra, k-means++ e
  `MPI_Bcast` dos centroides) até a parada. T = init + laço. A leitura do arquivo (`io`)
  e o resumo final ficam de fora e são impressos separados.
- **Fases (por processo, somadas em todas as iterações):**
  - computação = atribuição + somas locais + atualização dos centroides;
  - verificação = coletiva do "quantos mudaram";
  - troca = `MPI_Allreduce` das somas e contagens.

  O processo 0 imprime a média, o mínimo e o máximo de cada fase entre os processos.
- **Custo da verificação (`VERIFCUSTO`):** em cada iteração, o menor tempo de verificação
  entre os processos (o do mais lento, que quase não espera), somado nas iterações. O
  mínimo da soma (`verif_min` em `FASES`) superestima esse custo quando o processo mais
  lento muda de uma iteração para outra: na 1ª medição, com P = 2 num só nó, ele deu
  0,12 s, ou 3,4 ms por iteração para um `MPI_Allreduce` de 1 inteiro em memória
  compartilhada. A espera é `verif_med − VERIFCUSTO`.
- **Saída:** `resultados/t4-<configuração>-<job>.out`, com o cabeçalho (lscpu, contagem de
  CPUs do Slurm, versão do Open MPI, entrada), as linhas `DISTRIBUICAO` e, por execução:

  ```
  RESULTADO,<versao>,<P>,<N>,<D>,<K>,<iteracoes>,<inercia>,<checksum>
  TEMPOS,<versao>,<P>,<io>,<init>,<laco>,<total>
  FASES,<versao>,<P>,<comp_med>,<comp_min>,<comp_max>,<verif_med>,<verif_min>,<verif_max>,<troca_med>,<troca_min>,<troca_max>
  VERIFCUSTO,<versao>,<P>,<custo da verificacao>    (só na MPI)
  SEQFRAC,<init>,<atualizacao>,<total>,<s>          (só na sequencial)
  TAMANHOS,<versao>,<n_0>,...,<n_K-1>
  ```

## 4. Configurações

### calibra: `slurm/calibra.sbatch`

- **Objetivo:** escolher `DADOS` e `K` para T1 entre 30 e 60 s.
- **Execução:** `./kmeans_seq` uma vez para cada `dados/ctu13_*.bin` × K ∈ {8, 16, 32}
  (mude com `CALIBRA_K`). Imprime iterações e tempo.
- **Por que medir em vez de extrapolar:** o tempo é ~ N·K·iterações, e o número de
  iterações depende dos dados e de K. Não dá para prever a partir de outra combinação.

### seq-t1: `slurm/seq-t1.sbatch`

- **Mede:**
  - T1 (mediana de `TEMPOS,seq`);
  - s = (init + atualização dos centroides) / total (`SEQFRAC`). É o que a versão MPI não
    divide entre os processos: o k-means++ roda só no processo 0 e a atualização é
    replicada em todos;
  - o `RESULTADO` de referência.
- **Execução:** `./kmeans_seq`, 10 vezes, em 1 nó.

### fases: `slurm/fases.sbatch`

- **Mede:** T_P e as três fases para P = 1, 2, 4, 8, 16, 32, 64, com a verificação por um
  `MPI_Allreduce` de 1 inteiro.
- **Execução:** `srun ./kmeans_mpi ... -v allreduce`, 10 vezes cada P.
- **Uso:**
  - S = T1/T_P, E = S/P e teto de Amdahl = 1/(s + (1−s)/P);
  - barras de fases;
  - verificação: custo = `VERIFCUSTO`, média − custo = espera;
  - troca: com P = 64 ela também tem espera (média bem acima do mínimo), porque os
    processos saem da verificação em momentos diferentes; reporte o mínimo também;
  - HT: comparar P = 32 com P = 64.

### verificacao-bcast: `slurm/verificacao-bcast.sbatch`

- **Mede:** a mesma série com a verificação feita por P `MPI_Bcast`, um com cada processo
  como raiz.
- **Uso:** comparar o custo da verificação (`VERIFCUSTO`) com o da série fases. O Allreduce faz a
  redução em árvore, em ~log P passos; os P broadcasts fazem P coletivas em sequência.

### avaliacao: `slurm/avaliacao.sbatch`

- **Mede:** nada de desempenho. Uma execução com P = 64 grava `resultados/membros.bin`, a
  atribuição de cada fluxo. Se o nó tiver numpy, roda o `eval.py` em seguida.
- **Por que é separada:** com `-o`, a execução escreve o arquivo e não deve entrar nas
  medianas da série fases.

## 5. Roteiro de execução

Todos os comandos rodam a partir da raiz do repositório, no nó de login do Atlântica.

### 0. Configurações iniciais (uma vez)

#### 0.1 Entrar no cluster e clonar

```bash
cd ~                                    # o home é visto pelos nós de computação
git clone https://github.com/heloysapelizon/kmeans-t4-comp-par.git
cd kmeans-t4-comp-par
```

#### 0.2 Ambiente MPI → `AMBIENTE` e `SRUN_MPI`

No T3, o `mpicc` já estava no PATH (`/LADAPPs/OpenMPI/openmpi-4.1.1`) e o `srun` lançou o
Open MPI sem opções extras. Confira:

```bash
which mpicc srun                        # aparecendo os dois: AMBIENTE="" e SRUN_MPI=""
```

Se aparecer erro de PMI no teste do passo 0.5, use `SRUN_MPI="--mpi=pmix"`
(`srun --mpi=list` mostra as opções).

#### 0.3 Partição e os 4 nós → `PARTICAO` e `NOS`

```bash
sinfo                                   # partição padrão (no T3: LAD_geral)
sinfo -N -l                             # um nó por linha, com o estado
scontrol show node atlantica05 | grep -E "CPUTot|Sockets|CoresPerSocket|ThreadsPerCore|State"
```

- Escolha **4 nós** `idle`, iguais, com `Sockets=2 CoresPerSocket=4 ThreadsPerCore=2
  CPUTot=16`. No T3, o `atlantica02` estava `down` e as medições rodaram no `atlantica05`
  e no `atlantica06`.
- Grave os nomes **no arquivo `config.sh`**, separados por vírgula e sem espaço. Definir a
  variável só no terminal não funciona: os scripts leem o arquivo.
  ```bash
  NOS="atlantica03,atlantica04,atlantica05,atlantica06"
  ```
- Use os mesmos 4 nós do começo ao fim.

#### 0.4 Compilar

```bash
source config.sh && eval "$AMBIENTE"
make                                    # kmeans_seq e kmeans_mpi, mesmo mpicc e mesmas flags
```

O `submete.sh` roda `make` antes de cada submissão. Depois de um `git pull` com mudanças no
código, os binários são refeitos.

#### 0.5 Python e teste rápido nos 4 nós (dados sintéticos)

O `python3` do Atlântica (3.8) não vem com numpy. Instale no seu usuário, uma vez (vai
para `~/.local`, visto pelos nós):

```bash
python3 -m pip install --user --upgrade pip
python3 -m pip install --user numpy pandas matplotlib
python3 -c "import numpy, pandas, matplotlib; print('ok')"
```

Teste:

```bash
mkdir -p dados resultados               # dados/ está no .gitignore e não vem no clone
python3 scripts/gera_sintetico.py -n 200000 -o dados/teste.binetflow
python3 scripts/prep.py dados/teste.binetflow -o dados/teste
source config.sh; P=$PARTICAO; [ -n "$P" ] && P="--partition=$P"
./kmeans_seq -i dados/teste.bin -k 8 | grep RESULTADO
srun $P $SRUN_MPI -N 4 -n 32 -c 2 -w "$NOS" ./kmeans_mpi -i dados/teste.bin -k 8 | grep RESULTADO
srun $P $SRUN_MPI -N 4 -n 8 -w "$NOS" hostname | sort | uniq -c    # 2 por nó
```

As duas linhas `RESULTADO` devem ter iterações, inércia e checksum iguais. Se o pip não
conseguir instalar, rode os passos com Python (0.5, 1, 6 e 8) no seu computador e copie os
arquivos com `scp`.

### 1. Baixar e preparar o CTU-13

```bash
bash scripts/preparar.sh 1              # cenário 1 (Neris): dados/ctu13_s1.{bin,rotulos,json}
cat dados/ctu13_s1.json                 # N, D = 20, contagem de background/normal/botnet
```

- **Se a calibração pedir menos fluxos**, gere variantes com amostra regular. A calibração
  mede todas as variantes que estiverem em `dados/`:
  ```bash
  MAX_FLUXOS=1000000 bash scripts/preparar.sh 1     # dados/ctu13_s1_m1000000.*
  ```
- **Se o `curl` do nó de login não alcançar o site da CTU**, baixe o arquivo no seu
  computador, copie com `scp` para `dados/ctu13_cenario1.binetflow` e rode o
  `preparar.sh`, que pula o download quando o arquivo já existe:
  `https://mcfp.felk.cvut.cz/publicDatasets/CTU-Malware-Capture-Botnet-42/detailed-bidirectional-flow-labels/capture20110810.binetflow`
- **Se quiser mais fluxos**, junte o cenário 10:
  ```bash
  bash scripts/preparar.sh 1 10         # dados/ctu13_s1_10.*
  ```

### 2. Calibração

```bash
./slurm/submete.sh calibra
grep -A2 '^---- CALIBRA' resultados/t4-calibra-[0-9]*.out
```

1. Confira `CPUTot=16` no cabeçalho. Se aparecer 8, veja a seção 6.
2. Escolha a combinação entrada × K com `total` entre 30 e 60 s. Prefira a entrada maior
   e K de 8 a 16: com N grande sobra trabalho para P = 64.
3. Grave `DADOS` e `K` em `config.sh`.

### Como acompanhar um job (passos 3 a 6)

```bash
C=fases                                                   # nome da configuração
squeue -u $USER -o "%.8i %.24j %.3t %.10M %.10L %R"       # PD = fila, R = rodando
tail -f resultados/t4-$C-[0-9]*.out                       # Ctrl+C sai sem afetar o job
grep -c '^---- repeticao' resultados/t4-$C-[0-9]*.out     # repetição atual (de 10)
grep -c '^RESULTADO'      resultados/t4-$C-[0-9]*.out     # execuções concluídas
grep -inE 'error|killed|abort|cancelled|segmentation|AVISO' resultados/t4-$C-[0-9]*.out  # vazio
sacct -X -j <JOBID> -o JobName%24,State,Elapsed,ExitCode  # depois: COMPLETED 0:0
```

`AVISO` aparece se o k-means bater na trava de `max_iter` sem convergir. Nesse caso a
parada não foi pelos dados e a execução não vale.

Função de resumo (cole uma vez por sessão):

```bash
resumo() {  # resumo <configuração>: por versão e P, execuções, mediana, mín. e máx. de T
  grep -h '^TEMPOS' resultados/t4-$1-[0-9]*.out | awk -F, '{print $2, $3, $7}' |
  sort -k1,1 -k2,2n -k3,3g |
  awk 'function f(){if(n)printf "%-14s P=%-3s exec=%-3d mediana=%-9.3f min=%-9.3f max=%-9.3f\n",
         v,p,n,(n%2?t[(n+1)/2]:(t[n/2]+t[n/2+1])/2),t[1],t[n];n=0}
       {k=$1" "$2; if(k!=a){f();a=k;v=$1;p=$2}; t[++n]=$3} END{f()}'
}
```

### 3. seq-t1

```bash
./slurm/submete.sh seq-t1               # ~10 × T1 ≈ 8 min
resumo seq-t1                           # 1 linha, exec=10, mediana entre 30 e 60 s
grep -h '^SEQFRAC' resultados/t4-seq-t1-[0-9]*.out | head -3    # s (última coluna)
grep -h '^RESULTADO' resultados/t4-seq-t1-[0-9]*.out | sort -u  # 1 linha: a referência
```

Se a mediana saiu da faixa, ajuste `DADOS`/`K`, apague o `.out` e repita. **Daqui em diante,
nada em `config.sh` muda.**

### 4. fases

```bash
./slurm/submete.sh fases                # ~10 × (T1 + T1/2 + ... ) ≈ 20 min
resumo fases                            # 7 linhas (P = 1..64), exec=10 cada
grep '^DISTRIBUICAO' resultados/t4-fases-[0-9]*.out
```

O que conferir:
- **Distribuição:** P = 1, 2, 4, 8 num nó só; 16 = 8 + 8; 32 = 8 × 4; 64 = 16 × 4.
- **P = 1:** T próximo de T1, porque o MPI com 1 processo não troca nada.
- **Tempos:** caem com P até 32. Em 64 (HT), o ganho é bem menor.

### 5. verificacao-bcast

```bash
./slurm/submete.sh verificacao-bcast    # ~15 min
resumo verificacao-bcast                # 6 linhas (P = 2..64), exec=10 cada
```

O que conferir: o custo da verificação (`VERIFCUSTO`) cresce mais rápido com P que no
Allreduce, sobretudo com 32 e 64.

### 6. avaliacao

```bash
./slurm/submete.sh avaliacao
tail -30 resultados/t4-avaliacao-[0-9]*.out    # tabela de clusters e F1, se o nó tiver numpy
```

Sem numpy no nó, copie `dados/ctu13_s1.{bin,rotulos,json}` e `resultados/membros.bin`
para o seu computador e rode lá `python3 scripts/eval.py dados/ctu13_s1 membros.bin`.

Se só o `eval.py` mudou (por exemplo, `--limiar` ou `--min-rotulados`), não precisa rodar o
k-means de novo: o `membros.bin` é o mesmo. Rode só o `eval.py` e guarde a saída em
`resultados/eval.txt`; a tabela que está no `t4-avaliacao-*.out` fica desatualizada.

Os passos 3 a 6 podem ser submetidos juntos com `./slurm/submete.sh`. Todos usam
`--exclusive` nos mesmos nós, então o Slurm os executa um por vez. Submeter um por vez só
serve para conferir cada saída antes da próxima.

### 7. Conferir as saídas

| Arquivo | Linhas `RESULTADO` esperadas |
|---|---|
| `t4-seq-t1-*.out` | 10 |
| `t4-fases-*.out` | 70 (7 P × 10) |
| `t4-verificacao-bcast-*.out` | 60 (6 P × 10) |
| `t4-avaliacao-*.out` | 1 |

```bash
grep -c '^RESULTADO' resultados/t4-*.out
# equivalência (seção 4): uma única linha, com a contagem total de execuções
grep -h '^RESULTADO' resultados/t4-*.out | cut -d, -f4-9 | sort | uniq -c
grep -h '^nos:' resultados/t4-*.out | sort -u   # mesmos nós em todos os jobs
```

Se um job falhou ou foi interrompido, apague o `.out` dele e submeta só aquela
configuração de novo. Senão, o `analise.py` mistura repetições incompletas com as novas.

### 8. Análise

```bash
python3 scripts/analise.py
```

O script:
- confere a equivalência em todas as execuções;
- imprime a tabela: mediana por P de T, S, E, teto de Amdahl e fases;
- gera em `resultados/`:
  - `tabela.csv`;
  - `speedup.png`: ideal, Amdahl e medido (exigência 6);
  - `eficiencia.png`;
  - `fases.png`: em segundos e em %, com a verificação separada em custo e espera
    (exigências 7, 8 e 10);
  - `verificacao.png`: Allreduce × P broadcasts (exigência 9).

### 9. Entrega

```bash
git add resultados/*.out resultados/*.png resultados/tabela.csv slurm config.sh
git commit -m "Medições do T4 no Atlântica" && git push
```

O `.gitignore` deixa de fora `dados/` e `resultados/*.bin`. O link do repositório vai no
cabeçalho do relatório.

## 6. Se algo der errado no cluster

- **`CPUTot=8` no cabeçalho:** o Slurm não conta as threads de HT. Faça `cpt_para`
  devolver sempre 1 em `slurm/comum.sh` e troque `--ntasks-per-node=16` por
  `--ntasks-per-node=8 --overcommit` nos `.sbatch` das paralelas.
- **Erro de PMI no `srun`:** `SRUN_MPI="--mpi=pmix"` (ou `pmi2`) em `config.sh`.
- **Job some sem saída:** confira se `resultados/` existe (o `submete.sh` cria) e o
  `--time` do `.sbatch`.
- **Linhas `MASCARA` com a mesma máscara para dois processos, ou aviso de que o binding
  foi ignorado:** o plugin de afinidade do Slurm não está ativo nesse nó. Registre no
  relatório e compare as repetições pelo máximo da computação (`FASES`, coluna 6).
- **`AVISO: parou em max_iter`:** o k-means não convergiu em 1000 iterações com essa
  entrada e esse K. Troque K na calibração; não aumente `-m` só para passar.
