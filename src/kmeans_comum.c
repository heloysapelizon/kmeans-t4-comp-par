/* kmeans_comum.c — nucleo do k-means usado pelas duas versoes.
 *
 * Adaptado de seq_kmeans.c (Wei-keng Liao, 2005; Serban Giuroiu, 2011;
 * licenca MIT, ver LICENSE na raiz; o arquivo original continua em
 * seq_kmeans.c na raiz deste fork). Do original vem o laco de Lloyd:
 * find_nearest_cluster() -> mais_proximo(), o corpo do do/while -> passo_local()
 * e a media das somas -> atualiza_centroides().
 *
 * Mudancas para o T4:
 *  1. Pontos int32 (quantizados por prep.py) e somas int64. No original as
 *     somas sao float: na versao paralela a ordem das somas muda e o resultado
 *     deixa de bater com o da sequencial. Com inteiros as somas sao exatas, os
 *     centroides (soma/cont em double) sao os mesmos bits nas duas versoes e,
 *     portanto, as atribuicoes e o numero de iteracoes tambem.
 *  2. Parada: o original para quando a fracao de pontos que mudou fica abaixo
 *     de um threshold (ou apos 500 voltas). Aqui: nenhum ponto mudou.
 *  3. Inicializacao: o original usa os K primeiros pontos (no CTU-13 seriam K
 *     fluxos quase iguais do inicio da captura). Aqui: k-means++ com D^2 e
 *     sorteio inteiros sobre uma amostra regular, com gerador proprio
 *     (splitmix64), para ser deterministico em qualquer no.
 *  4. Cluster vazio mantem o centroide anterior (o original faz isso na
 *     sequencial, mas o mpi_kmeans.c do mesmo pacote usa "> 1" e zera o
 *     centroide: as duas versoes dele divergem nesse caso).
 *  5. Resumo exato do resultado: inercia calculada das somas inteiras globais
 *     e checksum das atribuicoes com o indice global de cada ponto.
 */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kmeans.h"

/* ------------------------------------------------------------------ entrada */

int le_cabecalho(const char *arq, int64_t *n, int32_t *d, int32_t *q)
{
    FILE *f = fopen(arq, "rb");
    if (!f) { perror(arq); return -1; }
    char magic[4];
    int ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, "KMQ1", 4) == 0 &&
             fread(n, sizeof *n, 1, f) == 1 && fread(d, sizeof *d, 1, f) == 1 &&
             fread(q, sizeof *q, 1, f) == 1;
    fclose(f);
    if (!ok) { fprintf(stderr, "%s: cabecalho invalido\n", arq); return -1; }
    return 0;
}

int le_bloco(const char *arq, int64_t inicio, int64_t n, int32_t d, int32_t *x)
{
    FILE *f = fopen(arq, "rb");
    if (!f) { perror(arq); return -1; }
    off_t pos = (off_t)CABECALHO_BYTES + (off_t)inicio * d * (off_t)sizeof(int32_t);
    if (fseeko(f, pos, SEEK_SET) != 0 ||
        fread(x, sizeof(int32_t), (size_t)(n * d), f) != (size_t)(n * d)) {
        fprintf(stderr, "%s: falha lendo %lld pontos a partir de %lld\n", arq,
                (long long)n, (long long)inicio);
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ amostra */

int64_t amostra_passo(int64_t n_total, int64_t s)
{
    return s >= n_total ? 1 : n_total / s;
}

/* indices j da amostra (ponto global j*passo) que caem em [inicio, fim) */
void amostra_faixa(int64_t inicio, int64_t fim, int64_t passo, int64_t s,
                   int64_t *j0, int64_t *j1)
{
    int64_t a = (inicio + passo - 1) / passo;
    int64_t b = (fim + passo - 1) / passo;
    if (a > s) a = s;
    if (b > s) b = s;
    *j0 = a;
    *j1 = b;
}

/* ------------------------------------------------------------ k-means++ */

static uint64_t splitmix64(uint64_t *estado)
{
    uint64_t z = (*estado += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static int64_t dist2_int(int d, const int32_t *a, const int32_t *b)
{
    int64_t s = 0;
    for (int j = 0; j < d; j++) {
        int64_t t = (int64_t)a[j] - b[j];
        s += t * t;
    }
    return s;
}

void kmeanspp(int64_t s, int d, int k, const int32_t *a, uint64_t semente,
              double *centroides)
{
    uint64_t rng = semente;
    int64_t *dmin = malloc((size_t)s * sizeof *dmin);
    int64_t esc = (int64_t)(splitmix64(&rng) % (uint64_t)s);

    for (int c = 0; c < k; c++) {
        for (int j = 0; j < d; j++) centroides[(int64_t)c * d + j] = a[esc * d + j];
        if (c == k - 1) break;

        /* D^2 ate o centroide mais proximo ja escolhido (exato, inteiro) */
        uint64_t total = 0;
        for (int64_t i = 0; i < s; i++) {
            int64_t t = dist2_int(d, a + i * d, a + esc * d);
            if (c == 0 || t < dmin[i]) dmin[i] = t;
            total += (uint64_t)dmin[i];
        }
        if (total == 0) {            /* amostra com menos de k pontos distintos */
            esc = (esc + 1) % s;
            continue;
        }
        uint64_t alvo = splitmix64(&rng) % total, acum = 0;
        for (esc = 0; esc < s - 1; esc++) {
            acum += (uint64_t)dmin[esc];
            if (acum > alvo) break;
        }
    }
    free(dmin);
}

/* ---------------------------------------------- laco de Lloyd (do Liao) */

/* find_nearest_cluster() do original, com ponto int32 e centroide double */
static inline int mais_proximo(int k, int d, const int32_t *x, const double *c)
{
    int melhor = 0;
    double menor = 0.0;
    for (int j = 0; j < d; j++) {
        double t = (double)x[j] - c[j];
        menor += t * t;
    }
    for (int i = 1; i < k; i++) {
        const double *ci = c + (int64_t)i * d;
        double dist = 0.0;
        for (int j = 0; j < d; j++) {
            double t = (double)x[j] - ci[j];
            dist += t * t;
        }
        if (dist < menor) {          /* empate fica com o menor indice */
            menor = dist;
            melhor = i;
        }
    }
    return melhor;
}

/* corpo do do/while de seq_kmeans() */
int64_t passo_local(int64_t n, int d, int k, const int32_t *x,
                    const double *centroides, int32_t *membro,
                    int64_t *soma, int64_t *cont)
{
    memset(soma, 0, (size_t)k * d * sizeof *soma);
    memset(cont, 0, (size_t)k * sizeof *cont);
    int64_t mudou = 0;
    for (int64_t i = 0; i < n; i++) {
        const int32_t *xi = x + i * d;
        int idx = mais_proximo(k, d, xi, centroides);
        if (membro[i] != idx) mudou++;
        membro[i] = idx;
        cont[idx]++;
        int64_t *si = soma + (int64_t)idx * d;
        for (int j = 0; j < d; j++) si[j] += xi[j];
    }
    return mudou;
}

void atualiza_centroides(int k, int d, const int64_t *soma, const int64_t *cont,
                         double *centroides)
{
    for (int i = 0; i < k; i++) {
        if (cont[i] == 0) continue;
        for (int j = 0; j < d; j++)
            centroides[(int64_t)i * d + j] =
                (double)soma[(int64_t)i * d + j] / (double)cont[i];
    }
}

/* -------------------------------------------------------- resumo exato */

void soma_quadrados(int64_t n, int d, int k, const int32_t *x,
                    const int32_t *membro, int64_t *s2)
{
    memset(s2, 0, (size_t)k * sizeof *s2);
    for (int64_t i = 0; i < n; i++) {
        int64_t t = 0;
        for (int j = 0; j < d; j++) t += (int64_t)x[i * d + j] * x[i * d + j];
        s2[membro[i]] += t;
    }
}

/* sum_i |x_i - mu_k|^2 = sum_k (S2_k - |S1_k|^2 / n_k), a partir de somas
 * inteiras globais: mesmo valor, em qualquer P */
long double inercia_exata(int k, int d, const int64_t *soma, const int64_t *cont,
                          const int64_t *s2)
{
    long double total = 0.0L;
    for (int i = 0; i < k; i++) {
        if (cont[i] == 0) continue;
        long double q = 0.0L;
        for (int j = 0; j < d; j++) {
            long double s = (long double)soma[(int64_t)i * d + j];
            q += s * s;
        }
        total += (long double)s2[i] - q / (long double)cont[i];
    }
    return total;
}

/* sum_i ((g_i mod 1000003) + 1) * (membro_i + 1), g_i = indice global */
int64_t checksum_membros(int64_t inicio, int64_t n, const int32_t *membro)
{
    int64_t c = 0;
    for (int64_t i = 0; i < n; i++)
        c += ((inicio + i) % 1000003 + 1) * (int64_t)(membro[i] + 1);
    return c;
}

double agora(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + 1e-9 * t.tv_nsec;
}
