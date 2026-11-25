/* kmeans_mpi.c — versao paralela no modelo de fases paralelas.
 *
 * Cada processo le N/P pontos consecutivos do arquivo e repete:
 *   1. computacao local : atribui os seus pontos aos centroides e acumula,
 *                         por cluster, a soma das coordenadas e a contagem
 *                         (mais a atualizacao dos centroides, a partir da 2a
 *                         iteracao, que todos fazem com os mesmos dados);
 *   2. verificacao      : soma global de "quantos pontos mudaram de cluster";
 *                         todos param juntos quando ela e 0;
 *   3. troca com todos  : MPI_Allreduce das somas e contagens (K*D + K int64).
 *
 * Verificacao (-v): "allreduce" (padrao) = um MPI_Allreduce de 1 inteiro;
 * "bcast" = P MPI_Bcast, um por processo (cada um difunde a sua contagem).
 *
 * Uso: srun -n P kmeans_mpi -i dados.bin -k K [-s amostra=100000]
 *          [-r semente=42] [-m max_iter=1000] [-v allreduce|bcast] [-o membros.bin]
 */
#define _POSIX_C_SOURCE 200809L
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kmeans.h"

static void uso(const char *p)
{
    fprintf(stderr, "uso: %s -i dados.bin -k K [-s amostra] [-r semente] "
                    "[-m max_iter] [-v allreduce|bcast] [-o membros.bin]\n", p);
    MPI_Abort(MPI_COMM_WORLD, 1);
}

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);
    int rank, P;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &P);

    const char *entrada = NULL, *saida = NULL, *modo = "allreduce";
    int k = 0, max_iter = 1000;
    int64_t s = 100000;
    uint64_t semente = 42;
    int opt;
    while ((opt = getopt(argc, argv, "i:k:s:r:m:v:o:")) != -1) {
        switch (opt) {
        case 'i': entrada = optarg; break;
        case 'k': k = atoi(optarg); break;
        case 's': s = atoll(optarg); break;
        case 'r': semente = strtoull(optarg, NULL, 10); break;
        case 'm': max_iter = atoi(optarg); break;
        case 'v': modo = optarg; break;
        case 'o': saida = optarg; break;
        default: uso(argv[0]);
        }
    }
    int verif_bcast = strcmp(modo, "bcast") == 0;
    if (!entrada || k < 2 || (!verif_bcast && strcmp(modo, "allreduce") != 0))
        uso(argv[0]);

    /* ---- leitura do bloco [inicio, fim) (fora da regiao medida) */
    double t0 = MPI_Wtime();
    int64_t n_total;
    int32_t d, q;
    if (le_cabecalho(entrada, &n_total, &d, &q) != 0) MPI_Abort(MPI_COMM_WORLD, 1);
    int64_t inicio = n_total * rank / P, fim = n_total * (rank + 1) / P;
    int64_t n = fim - inicio;
    int32_t *x = malloc((size_t)(n > 0 ? n : 1) * d * sizeof *x);
    if (!x || le_bloco(entrada, inicio, n, d, x) != 0) MPI_Abort(MPI_COMM_WORLD, 1);
    double t_io = MPI_Wtime() - t0;
    MPI_Barrier(MPI_COMM_WORLD);

    /* ---- inicializacao: amostra regular reunida no 0, k-means++ no 0, Bcast */
    double t_ini0 = MPI_Wtime();
    if (s > n_total) s = n_total;
    int64_t passo = amostra_passo(n_total, s), j0, j1;
    amostra_faixa(inicio, fim, passo, s, &j0, &j1);
    int minha = (int)((j1 - j0) * d);
    int32_t *meus = malloc((size_t)(minha > 0 ? minha : 1) * sizeof *meus);
    for (int64_t j = j0; j < j1; j++)
        memcpy(meus + (j - j0) * d, x + (j * passo - inicio) * d, (size_t)d * sizeof *x);
    int *cnts = NULL, *desl = NULL;
    int32_t *amostra = NULL;
    if (rank == 0) {
        cnts = malloc(P * sizeof *cnts);
        desl = malloc(P * sizeof *desl);
        amostra = malloc((size_t)s * d * sizeof *amostra);
    }
    MPI_Gather(&minha, 1, MPI_INT, cnts, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (rank == 0)
        for (int r = 0, acc = 0; r < P; r++) { desl[r] = acc; acc += cnts[r]; }
    MPI_Gatherv(meus, minha, MPI_INT32_T, amostra, cnts, desl, MPI_INT32_T, 0,
                MPI_COMM_WORLD);
    double *centroides = malloc((size_t)k * d * sizeof *centroides);
    if (rank == 0) kmeanspp(s, d, k, amostra, semente, centroides);
    MPI_Bcast(centroides, k * d, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    free(meus); free(cnts); free(desl); free(amostra);

    int32_t *membro = malloc((size_t)(n > 0 ? n : 1) * sizeof *membro);
    for (int64_t i = 0; i < n; i++) membro[i] = -1;
    /* buffer da troca: soma[k][d] seguida de cont[k] (um so Allreduce) */
    int64_t tam = (int64_t)k * d + k;
    int64_t *buf = malloc((size_t)tam * sizeof *buf);
    int64_t *soma = buf, *cont = buf + (int64_t)k * d;
    double t_init = MPI_Wtime() - t_ini0;
    MPI_Barrier(MPI_COMM_WORLD);

    /* ---- laco de fases paralelas */
    double t_comp = 0, t_verif = 0, t_troca = 0;
    int iter = 0;
    int64_t mudou_global = 0;
    double t_laco0 = MPI_Wtime();
    for (;;) {
        /* 1. computacao local */
        double a = MPI_Wtime();
        int64_t mudou = passo_local(n, d, k, x, centroides, membro, soma, cont);
        double b = MPI_Wtime();
        t_comp += b - a;

        /* 2. verificacao da parada: todos decidem juntos */
        if (verif_bcast) {
            mudou_global = 0;
            for (int r = 0; r < P; r++) {
                int64_t v = (r == rank) ? mudou : 0;
                MPI_Bcast(&v, 1, MPI_INT64_T, r, MPI_COMM_WORLD);
                mudou_global += v;
            }
        } else {
            MPI_Allreduce(&mudou, &mudou_global, 1, MPI_INT64_T, MPI_SUM,
                          MPI_COMM_WORLD);
        }
        double c = MPI_Wtime();
        t_verif += c - b;
        iter++;
        if (mudou_global == 0 || iter >= max_iter) break;

        /* 3. troca com todos: somas e contagens globais */
        MPI_Allreduce(MPI_IN_PLACE, buf, (int)tam, MPI_INT64_T, MPI_SUM,
                      MPI_COMM_WORLD);
        double e = MPI_Wtime();
        t_troca += e - c;

        /* (computacao local) novos centroides, iguais em todos os processos */
        atualiza_centroides(k, d, soma, cont, centroides);
        t_comp += MPI_Wtime() - e;
    }
    double t_laco = MPI_Wtime() - t_laco0;
    if (rank == 0 && mudou_global != 0)
        fprintf(stderr, "AVISO: parou em max_iter=%d com %lld mudancas\n", max_iter,
                (long long)mudou_global);

    /* ---- resumo exato (fora da regiao medida) */
    int64_t *s2 = malloc((size_t)k * sizeof *s2);
    soma_quadrados(n, d, k, x, membro, s2);
    int64_t chk = checksum_membros(inicio, n, membro), chk_global;
    MPI_Allreduce(MPI_IN_PLACE, buf, (int)tam, MPI_INT64_T, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(MPI_IN_PLACE, s2, k, MPI_INT64_T, MPI_SUM, MPI_COMM_WORLD);
    MPI_Reduce(&chk, &chk_global, 1, MPI_INT64_T, MPI_SUM, 0, MPI_COMM_WORLD);

    /* tempos por fase: media, minimo e maximo entre os processos */
    double fases[3] = {t_comp, t_verif, t_troca}, f_soma[3], f_min[3], f_max[3];
    MPI_Reduce(fases, f_soma, 3, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(fases, f_min, 3, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
    MPI_Reduce(fases, f_max, 3, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    double tempos[3] = {t_io, t_init, t_laco}, t_max[3];
    MPI_Reduce(tempos, t_max, 3, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        long double inercia = inercia_exata(k, d, soma, cont, s2);
        double total = t_max[1] + t_max[2];
        printf("k-means MPI (%s): P=%d N=%lld D=%d K=%d amostra=%lld semente=%llu\n",
               modo, P, (long long)n_total, d, k, (long long)s,
               (unsigned long long)semente);
        printf("iteracoes=%d inercia=%.10Le checksum=%lld\n", iter, inercia,
               (long long)chk_global);
        printf("tempo: io=%.4f init=%.4f laco=%.4f total=%.4f s\n", t_max[0], t_max[1],
               t_max[2], total);
        printf("RESULTADO,mpi_%s,%d,%lld,%d,%d,%d,%.10Le,%lld\n", modo, P,
               (long long)n_total, d, k, iter, inercia, (long long)chk_global);
        printf("TEMPOS,mpi_%s,%d,%.6f,%.6f,%.6f,%.6f\n", modo, P, t_max[0], t_max[1],
               t_max[2], total);
        printf("FASES,mpi_%s,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n", modo, P,
               f_soma[0] / P, f_min[0], f_max[0], f_soma[1] / P, f_min[1], f_max[1],
               f_soma[2] / P, f_min[2], f_max[2]);
        printf("TAMANHOS,mpi_%s", modo);
        for (int i = 0; i < k; i++) printf(",%lld", (long long)cont[i]);
        printf("\n");
    }

    if (saida) {
        MPI_File f;
        if (MPI_File_open(MPI_COMM_WORLD, saida, MPI_MODE_CREATE | MPI_MODE_WRONLY,
                          MPI_INFO_NULL, &f) != MPI_SUCCESS) {
            if (rank == 0) fprintf(stderr, "%s: falha ao abrir\n", saida);
        } else {
            MPI_File_set_size(f, (MPI_Offset)n_total * sizeof(int32_t));
            MPI_File_write_at_all(f, (MPI_Offset)inicio * sizeof(int32_t), membro,
                                  (int)n, MPI_INT32_T, MPI_STATUS_IGNORE);
            MPI_File_close(&f);
        }
    }

    free(x); free(membro); free(buf); free(s2); free(centroides);
    MPI_Finalize();
    return 0;
}
