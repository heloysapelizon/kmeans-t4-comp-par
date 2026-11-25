/* kmeans_seq.c — versao sequencial de referencia.
 *
 * Mesmo nucleo (kmeans_comum.c) e mesmas fases da versao MPI, sem MPI. Mede
 * a fracao sequencial s usada no teto de Amdahl: o que a versao paralela NAO
 * divide entre os processos (inicializacao k-means++ na amostra e a
 * atualizacao dos centroides, que todos os processos repetem).
 *
 * Uso: kmeans_seq -i dados.bin -k K [-s amostra=100000] [-r semente=42]
 *                 [-m max_iter=1000] [-o membros.bin]
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kmeans.h"

static void uso(const char *p)
{
    fprintf(stderr, "uso: %s -i dados.bin -k K [-s amostra] [-r semente] "
                    "[-m max_iter] [-o membros.bin]\n", p);
    exit(1);
}

int main(int argc, char **argv)
{
    const char *entrada = NULL, *saida = NULL;
    int k = 0, max_iter = 1000;
    int64_t s = 100000;
    uint64_t semente = 42;
    int opt;
    while ((opt = getopt(argc, argv, "i:k:s:r:m:o:")) != -1) {
        switch (opt) {
        case 'i': entrada = optarg; break;
        case 'k': k = atoi(optarg); break;
        case 's': s = atoll(optarg); break;
        case 'r': semente = strtoull(optarg, NULL, 10); break;
        case 'm': max_iter = atoi(optarg); break;
        case 'o': saida = optarg; break;
        default: uso(argv[0]);
        }
    }
    if (!entrada || k < 2) uso(argv[0]);

    /* ---- leitura (fora da regiao medida) */
    double t0 = agora();
    int64_t n;
    int32_t d, q;
    if (le_cabecalho(entrada, &n, &d, &q) != 0) return 1;
    int32_t *x = malloc((size_t)n * d * sizeof *x);
    if (!x || le_bloco(entrada, 0, n, d, x) != 0) return 1;
    double t_io = agora() - t0;

    /* ---- inicializacao: k-means++ sobre a amostra regular */
    double t_ini0 = agora();
    if (s > n) s = n;
    int64_t passo = amostra_passo(n, s);
    int32_t *amostra = malloc((size_t)s * d * sizeof *amostra);
    for (int64_t j = 0; j < s; j++)
        memcpy(amostra + j * d, x + j * passo * d, (size_t)d * sizeof *x);
    double *centroides = malloc((size_t)k * d * sizeof *centroides);
    kmeanspp(s, d, k, amostra, semente, centroides);
    free(amostra);
    int32_t *membro = malloc((size_t)n * sizeof *membro);
    for (int64_t i = 0; i < n; i++) membro[i] = -1;
    int64_t *soma = malloc((size_t)k * d * sizeof *soma);
    int64_t *cont = malloc((size_t)k * sizeof *cont);
    double t_init = agora() - t_ini0;

    /* ---- laco: computacao local / verificacao / (troca: nada a trocar) */
    double t_comp = 0, t_atualiza = 0, t_verif = 0;
    int iter = 0;
    double t_laco0 = agora();
    for (;;) {
        double a = agora();
        int64_t mudou = passo_local(n, d, k, x, centroides, membro, soma, cont);
        double b = agora();
        t_comp += b - a;

        iter++;
        int parar = (mudou == 0) || (iter >= max_iter);
        double c = agora();
        t_verif += c - b;
        if (parar) {
            if (mudou != 0)
                fprintf(stderr, "AVISO: parou em max_iter=%d com %lld mudancas\n",
                        max_iter, (long long)mudou);
            break;
        }

        atualiza_centroides(k, d, soma, cont, centroides);
        double e = agora();
        t_atualiza += e - c;
        t_comp += e - c;
    }
    double t_laco = agora() - t_laco0;
    double t_total = t_init + t_laco;

    /* ---- resumo (fora da regiao medida) */
    int64_t *s2 = malloc((size_t)k * sizeof *s2);
    soma_quadrados(n, d, k, x, membro, s2);
    long double inercia = inercia_exata(k, d, soma, cont, s2);
    int64_t chk = checksum_membros(0, n, membro);

    printf("k-means sequencial: N=%lld D=%d K=%d amostra=%lld semente=%llu\n",
           (long long)n, d, k, (long long)s, (unsigned long long)semente);
    printf("iteracoes=%d inercia=%.10Le checksum=%lld\n", iter, inercia, (long long)chk);
    printf("tempo: io=%.4f init=%.4f laco=%.4f total=%.4f s\n", t_io, t_init, t_laco, t_total);
    printf("RESULTADO,seq,1,%lld,%d,%d,%d,%.10Le,%lld\n", (long long)n, d, k, iter,
           inercia, (long long)chk);
    printf("TEMPOS,seq,1,%.6f,%.6f,%.6f,%.6f\n", t_io, t_init, t_laco, t_total);
    printf("FASES,seq,1,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,0,0,0\n",
           t_comp, t_comp, t_comp, t_verif, t_verif, t_verif);
    printf("SEQFRAC,%.6f,%.6f,%.6f,%.6f\n", t_init, t_atualiza, t_total,
           (t_init + t_atualiza) / t_total);
    printf("TAMANHOS,seq");
    for (int i = 0; i < k; i++) printf(",%lld", (long long)cont[i]);
    printf("\n");

    if (saida) {
        FILE *f = fopen(saida, "wb");
        if (!f || fwrite(membro, sizeof *membro, (size_t)n, f) != (size_t)n) {
            perror(saida);
            return 1;
        }
        fclose(f);
    }

    free(x); free(membro); free(soma); free(cont); free(s2); free(centroides);
    return 0;
}
