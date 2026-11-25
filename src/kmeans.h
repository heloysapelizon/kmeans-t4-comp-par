/* kmeans.h — declaracoes compartilhadas por kmeans_seq.c e kmeans_mpi.c.
 *
 * Base: seq_kmeans.c de Wei-keng Liao (Northwestern University, 2005), na
 * versao publicada por Serban Giuroiu (github.com/serban/kmeans, licenca MIT,
 * ver LICENSE na raiz). Este repositorio e um fork dele; o original continua
 * em seq_kmeans.c na raiz. Mudancas estao listadas em kmeans_comum.c.
 *
 * Formato do arquivo de entrada (gerado por scripts/prep.py):
 *   char    magic[4] = "KMQ1"
 *   int64_t n        pontos
 *   int32_t d        dimensoes
 *   int32_t q        escala da quantizacao (coordenadas em [0, q])
 *   int32_t x[n][d]  pontos, linha a linha
 */
#ifndef KMEANS_H
#define KMEANS_H

#include <stdint.h>

#define CABECALHO_BYTES 20

/* entrada */
int le_cabecalho(const char *arq, int64_t *n, int32_t *d, int32_t *q);
int le_bloco(const char *arq, int64_t inicio, int64_t n, int32_t d, int32_t *x);

/* amostra para o k-means++: indices globais j*passo, j = 0..s-1 */
int64_t amostra_passo(int64_t n_total, int64_t s);
void    amostra_faixa(int64_t inicio, int64_t fim, int64_t passo, int64_t s,
                      int64_t *j0, int64_t *j1);

/* inicializacao deterministica (k-means++ sobre a amostra, D^2 inteiro) */
void kmeanspp(int64_t s, int d, int k, const int32_t *amostra, uint64_t semente,
              double *centroides);

/* fase de computacao local: atribui cada ponto ao centroide mais proximo,
 * acumula soma[k][d] e cont[k], devolve quantos pontos mudaram de cluster */
int64_t passo_local(int64_t n, int d, int k, const int32_t *x,
                    const double *centroides, int32_t *membro,
                    int64_t *soma, int64_t *cont);

/* centroide = soma / cont; cluster vazio mantem o centroide anterior */
void atualiza_centroides(int k, int d, const int64_t *soma, const int64_t *cont,
                         double *centroides);

/* resumo exato do resultado (igual na sequencial e na paralela) */
void    soma_quadrados(int64_t n, int d, int k, const int32_t *x,
                       const int32_t *membro, int64_t *s2);
long double inercia_exata(int k, int d, const int64_t *soma, const int64_t *cont,
                          const int64_t *s2);
int64_t checksum_membros(int64_t inicio, int64_t n, const int32_t *membro);

double agora(void);

#endif
