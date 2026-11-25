# T4 — Fases paralelas com MPI: k-means em fluxos de rede (CTU-13)

Versão paralela, no modelo de **fases paralelas** com MPI, do k-means (algoritmo de Lloyd)
aplicado a fluxos de rede do dataset **CTU-13** (tráfego real de botnets capturado na CTU de
Praga, 2011). O k-means agrupa os fluxos sem ver os rótulos. Depois, os rótulos mostram se os
fluxos da botnet caíram em clusters próprios, ou seja, se seria possível detectá-los sem
supervisão.

Só o núcleo do k-means (`src/`) é paralelizado e medido. O pré-processamento e a avaliação
(`scripts/prep.py`, `scripts/eval.py`) ficam fora da região medida.

```
.binetflow ──prep.py──> dados.bin (N×D int32) ──kmeans_seq / kmeans_mpi──> membros.bin ──eval.py──> clusters × rótulos
                        dados.rotulos ───────────────────────────────────────────────────────────┘
```

## Origem do código

O núcleo (`src/kmeans_comum.c`) é adaptado de `seq_kmeans.c`, de **Wei-keng Liao**
(Northwestern University, 2005), na versão de Serban Giuroiu
([github.com/serban/kmeans](https://github.com/serban/kmeans), licença MIT, ver `LICENSE`).
Este repositório é um fork dele.

Os arquivos na raiz (`seq_*.c`, `mpi_*.c`, `omp_*.c`, `cuda_*.cu`, `file_io.c`, `kmeans.h`,
`README`) são os originais e **não são compilados nem usados** neste trabalho. O código do T4
está em `src/` e `scripts/`. O `Makefile` da raiz foi substituído; o original está no
histórico do git.

O pacote original inclui uma versão MPI (`mpi_kmeans.c`), que **não foi usada**: a versão
paralela deste trabalho foi escrita a partir da sequencial adaptada, seguindo as três fases.

Mudanças em relação ao original (detalhes no cabeçalho de `kmeans_comum.c`):

1. **Pontos `int32` e somas `int64`.** No original as somas são `float`. Na paralela, a ordem
   das somas muda e o resultado deixa de bater com o da sequencial. Com inteiros as somas são
   exatas, e as duas versões chegam bit a bit aos mesmos centroides, atribuições e número de
   iterações.
2. **Parada:** "nenhum ponto mudou de cluster", em vez de fração menor que um threshold ou
   500 voltas. `-m` é só uma trava de segurança e gera um AVISO se for atingida.
3. **Inicialização:** k-means++ determinístico (D² e sorteio inteiros, `splitmix64`) sobre uma
   amostra regular de `-s` pontos. O original usa os K primeiros pontos, que no CTU-13 seriam
   fluxos quase idênticos do início da captura.
4. **Cluster vazio** mantém o centroide anterior. O `mpi_kmeans.c` original usa `> 1` em vez
   de `> 0` e diverge da sequencial dele nesse caso.
5. **Resumo exato:** inércia calculada a partir das somas inteiras globais, mais um checksum
   das atribuições.

## O modelo

Cada um dos P processos lê N/P fluxos consecutivos e repete:

| Fase | O que faz | Comunicação |
|---|---|---|
| 1. Computação local | Atribui cada fluxo ao centroide mais próximo (O(N/P·K·D)); acumula `soma[K][D]` e `cont[K]`; conta quantos fluxos mudaram de cluster. A partir da 2ª iteração, recalcula os centroides (O(K·D), igual em todos) | nenhuma |
| 2. Verificação | Todos decidem juntos: para quando o total de mudanças é 0 | `MPI_Allreduce` de 1 `int64` (`-v allreduce`) ou P `MPI_Bcast` (`-v bcast`) |
| 3. Troca com todos | Somas e contagens globais | `MPI_Allreduce` de K·D + K `int64` |

Não sobra nada sequencial no laço além da atualização dos centroides, que é replicada. A fração
sequencial `s` (teto de Amdahl) é a inicialização (k-means++ no processo 0) mais essa
atualização, medidas na versão sequencial (linha `SEQFRAC`).

## Equivalência

Toda execução imprime:

```
RESULTADO,<versao>,<P>,<N>,<D>,<K>,<iteracoes>,<inercia>,<checksum>
```

`iteracoes`, `inercia` e `checksum` são iguais aos da sequencial para qualquer P e modo de
verificação (testado com P = 1, 2, 3, 4 e 8, e as atribuições gravadas por `-o` são idênticas
byte a byte). O `analise.py` confere isso em todas as saídas do Slurm.

## Features (prep.py)

20 por fluxo, normalizadas para [0, Q] (Q = 10⁴) e quantizadas:
- log de duração, pacotes, bytes totais, bytes de origem e de destino, bytes por pacote e
  pacotes por segundo;
- fração dos bytes que veio da origem;
- protocolo (tcp/udp/icmp/outro);
- classe da porta de destino (conhecida/registrada/dinâmica);
- serviço (DNS, web, SMTP, IRC);
- bidirecional.

Rótulos: `From-Botnet` → botnet; `Normal` → normal; o resto (Background, To-Botnet) →
background.

## Arquivos

| Caminho | Conteúdo |
|---|---|
| `src/` | k-means: núcleo (`kmeans_comum.c`), sequencial e MPI |
| `config.sh` | entrada, K, repetições, nós, partição: o único arquivo a editar |
| `slurm/` | `submete.sh`, `comum.sh` e um `.sbatch` por configuração de medição |
| `scripts/preparar.sh`, `scripts/prep.py` | baixa o CTU-13 e gera a entrada |
| `scripts/analise.py` | equivalência, tabela, speed-up/Amdahl, eficiência, fases |
| `scripts/eval.py` | clusters × rótulos (detecção de botnet) |
| `scripts/gera_sintetico.py` | dados falsos no formato do CTU-13, para testes |
| `MEDICOES.md` | **o que o enunciado exige medir e o roteiro completo no Atlântica** |

## Rodar no Atlântica

O passo a passo está em [`MEDICOES.md`](MEDICOES.md). Resumo:

```bash
make
bash scripts/preparar.sh 1             # CTU-13, cenário 1 -> dados/ctu13_s1.*
./slurm/submete.sh calibra             # escolher DADOS e K em config.sh
./slurm/submete.sh                     # seq-t1, fases, verificacao-bcast, avaliacao
python3 scripts/analise.py             # resultados/tabela.csv e gráficos
```

Programas:

```
kmeans_seq -i dados.bin -k K [-s amostra=100000] [-r semente=42] [-m max_iter=1000] [-o membros.bin]
srun -n P kmeans_mpi  (mesmas opções)  [-v allreduce|bcast]
```

## Testar sem o dataset

```bash
python3 scripts/gera_sintetico.py -n 400000 -o dados/sintetico.binetflow
python3 scripts/prep.py dados/sintetico.binetflow -o dados/sintetico
./kmeans_seq -i dados/sintetico.bin -k 16 -o dados/m_seq.bin
mpirun -np 4 ./kmeans_mpi -i dados/sintetico.bin -k 16 -o dados/m_mpi.bin && cmp dados/m_seq.bin dados/m_mpi.bin
python3 scripts/eval.py dados/sintetico dados/m_seq.bin
```

Os dados sintéticos servem só para testar o pipeline. Não use nas medições.
