#!/usr/bin/env python3
"""Tabelas e graficos do T4 a partir das saidas do Slurm.

Le as linhas RESULTADO/TEMPOS/FASES/VERIFCUSTO/SEQFRAC das saidas de seq-t1, fases e
verificacao-bcast (resultados/t4-*.out; calibra e avaliacao ficam de fora), confere a
equivalencia (iteracoes, inercia e checksum iguais aos da sequencial em todas
as execucoes) e gera, em resultados/:
  tabela.csv         mediana por (versao, P): tempos, fases, S, E, teto de Amdahl
  speedup.png        ideal, teto de Amdahl (s medido na sequencial) e medido
  eficiencia.png
  fases.png          barras empilhadas por P: computacao, verificacao (custo =
                     soma, nas iteracoes, do menor tempo entre os processos, linha
                     VERIFCUSTO; espera = media - custo), troca
  verificacao.png    Allreduce x P broadcasts (se verificacao.sbatch rodou)

Saidas sem VERIFCUSTO (as de resultados/cpu-bind-none/) usam como custo o menor
tempo total de verificacao entre os processos (FASES), que o superestima.

Uso: python3 scripts/analise.py [arquivos.out ...]
"""
import csv
import glob
import statistics as st
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

AZUL, LARANJA, VERDE_AGUA = "#257de0", "#f47230", "#00b894"
CINZA, TEXTO = "#8a8a8a", "#333333"
plt.rcParams.update({"font.size": 9, "axes.edgecolor": "#bbbbbb", "axes.labelcolor": TEXTO,
                     "xtick.color": TEXTO, "ytick.color": TEXTO, "axes.grid": True,
                     "grid.color": "#e6e6e6", "grid.linewidth": .6,
                     "axes.spines.top": False, "axes.spines.right": False})


def le(arquivos):
    res, tempos, fases, seqfrac = [], defaultdict(list), defaultdict(list), []
    custo = defaultdict(list)
    for a in arquivos:
        for linha in open(a, errors="replace"):
            c = linha.strip().split(",")
            if c[0] == "RESULTADO":
                res.append((c[1], int(c[2]), c[6], c[7], c[8]))
            elif c[0] == "TEMPOS":
                tempos[(c[1], int(c[2]))].append([float(v) for v in c[3:7]])
            elif c[0] == "FASES":
                fases[(c[1], int(c[2]))].append([float(v) for v in c[3:12]])
            elif c[0] == "VERIFCUSTO":
                custo[(c[1], int(c[2]))].append(float(c[3]))
            elif c[0] == "SEQFRAC":
                seqfrac.append(float(c[4]))
    return res, tempos, fases, custo, seqfrac


def mediana_col(linhas):
    return [st.median(col) for col in zip(*linhas)]


def main():
    arquivos = sys.argv[1:] or sorted(
        f for c in ("seq-t1", "fases", "verificacao-bcast")
        for f in glob.glob(f"resultados/t4-{c}-[0-9]*.out"))
    res, tempos, fases, custo, seqfrac = le(arquivos)
    if ("seq", 1) not in tempos:
        sys.exit("faltam execucoes da sequencial (sequencial.sbatch)")
    saida = Path("resultados")
    saida.mkdir(exist_ok=True)

    # ---- equivalencia
    ref = {(i, n, c) for v, p, i, n, c in res if v == "seq"}
    todos = {(i, n, c) for v, p, i, n, c in res}
    print(f"equivalencia: {len(res)} execucoes, resumos distintos = {len(todos)} "
          f"-> {'OK' if len(todos) == 1 and todos == ref else 'DIVERGE: ' + str(todos)}")

    t_seq = st.median(t[3] for t in tempos[("seq", 1)])
    s = st.median(seqfrac)
    print(f"T_seq (mediana) = {t_seq:.3f} s   fracao sequencial s = {s:.5f}")

    paralelas = sorted(k for k in tempos if k[0] != "seq")
    if not paralelas:
        sys.exit("faltam execucoes paralelas (fases.sbatch, verificacao-bcast.sbatch)")
    linhas = []
    for (v, p) in paralelas:
        io, ini, laco, tot = mediana_col(tempos[(v, p)])
        f = mediana_col(fases[(v, p)])
        # custo da verificacao: VERIFCUSTO se houver; senao, o menor total (FASES)
        vc = st.median(custo[(v, p)]) if custo.get((v, p)) else f[4]
        S = t_seq / tot
        amd = 1 / (s + (1 - s) / p)
        linhas.append(dict(versao=v, P=p, reps=len(tempos[(v, p)]), io=io, init=ini,
                           laco=laco, total=tot, speedup=S, eficiencia=S / p, amdahl=amd,
                           comp_med=f[0], comp_min=f[1], comp_max=f[2],
                           verif_med=f[3], verif_min=f[4], verif_max=f[5],
                           verif_custo=vc,
                           troca_med=f[6], troca_min=f[7], troca_max=f[8],
                           espera_verif=f[3] - vc))
    with open(saida / "tabela.csv", "w", newline="") as fp:
        w = csv.DictWriter(fp, fieldnames=list(linhas[0]))
        w.writeheader()
        for l in linhas:
            w.writerow({k: (f"{x:.6g}" if isinstance(x, float) else x) for k, x in l.items()})
    print(f"\n{'versao':>14} {'P':>3} {'total':>8} {'S':>6} {'E':>5} {'Amdahl':>7} "
          f"{'comp':>8} {'verif':>8} {'v.custo':>8} {'troca':>8}")
    for l in linhas:
        print(f"{l['versao']:>14} {l['P']:>3} {l['total']:>8.3f} {l['speedup']:>6.2f} "
              f"{l['eficiencia']:>5.2f} {l['amdahl']:>7.2f} {l['comp_med']:>8.3f} "
              f"{l['verif_med']:>8.3f} {l['verif_custo']:>8.3f} {l['troca_med']:>8.3f}")

    serie = [l for l in linhas if l["versao"] == "mpi_allreduce"]
    if not serie:
        return
    Ps = [l["P"] for l in serie]

    # ---- speed-up
    fig, ax = plt.subplots(figsize=(4.2, 3.2))
    ax.plot(Ps, Ps, "--", color=CINZA, lw=1.2, label="ideal (S = P)")
    ax.plot(Ps, [l["amdahl"] for l in serie], color=LARANJA, lw=2,
            label=f"teto de Amdahl (s = {s:.4f})")
    ax.plot(Ps, [l["speedup"] for l in serie], "o-", color=AZUL, lw=2, ms=5,
            mec="white", mew=1.5, label="medido")
    ax.set_xscale("log", base=2)
    ax.set_yscale("log", base=2)
    ax.set_xticks(Ps, [str(p) for p in Ps])
    ax.set_yticks(Ps, [str(p) for p in Ps])
    ax.set_xlabel("processos (P)")
    ax.set_ylabel("speed-up")
    ultimo = serie[-1]
    ax.annotate(f"{ultimo['speedup']:.1f}", (ultimo["P"], ultimo["speedup"]),
                textcoords="offset points", xytext=(-4, -12), ha="right", color=TEXTO)
    ax.legend(frameon=False, loc="upper left")
    fig.tight_layout()
    fig.savefig(saida / "speedup.png", dpi=200)

    # ---- eficiencia
    fig, ax = plt.subplots(figsize=(4.2, 3.0))
    ax.plot(Ps, [l["eficiencia"] for l in serie], "o-", color=AZUL, lw=2, ms=5,
            mec="white", mew=1.5, label="medida")
    ax.plot(Ps, [l["amdahl"] / l["P"] for l in serie], color=LARANJA, lw=2,
            label="teto de Amdahl")
    ax.set_xscale("log", base=2)
    ax.set_xticks(Ps, [str(p) for p in Ps])
    ax.set_ylim(0, 1.05)
    ax.set_xlabel("processos (P)")
    ax.set_ylabel("eficiência (S / P)")
    ax.legend(frameon=False, loc="lower left")
    fig.tight_layout()
    fig.savefig(saida / "eficiencia.png", dpi=200)

    # ---- fases (absoluto e percentual)
    fig, axs = plt.subplots(1, 2, figsize=(8, 3.2))
    x = range(len(Ps))
    for ax, pct in zip(axs, (False, True)):
        base = [0.0] * len(serie)
        partes = [("computação local", [l["comp_med"] for l in serie], AZUL, None),
                  ("verificação: custo", [l["verif_custo"] for l in serie], VERDE_AGUA, None),
                  ("verificação: espera (méd. − custo)", [l["espera_verif"] for l in serie],
                   "#bfeee3", "////"),
                  ("troca", [l["troca_med"] for l in serie], LARANJA, None)]
        tot = [sum(p[1][i] for p in partes) for i in range(len(serie))]
        for nome, val, cor, hatch in partes:
            v = [100 * a / t for a, t in zip(val, tot)] if pct else val
            ax.bar(x, v, bottom=base, color=cor, edgecolor="white", linewidth=1,
                   hatch=hatch, label=nome, width=.7)
            base = [b + a for b, a in zip(base, v)]
        ax.set_xticks(list(x), [str(p) for p in Ps])
        if pct:
            ax.set_ylim(0, 100)
        ax.set_xlabel("processos (P)")
        ax.set_ylabel("% do tempo do laço" if pct else "tempo médio por processo (s)")
        ax.grid(axis="x", visible=False)
    axs[0].legend(frameon=False, fontsize=8)
    fig.tight_layout()
    fig.savefig(saida / "fases.png", dpi=200)

    # ---- verificacao: Allreduce x P broadcasts
    bc = {l["P"]: l for l in linhas if l["versao"] == "mpi_bcast"}
    if bc:
        comuns = [l for l in serie if l["P"] in bc]
        Pc = [l["P"] for l in comuns]
        fig, ax = plt.subplots(figsize=(4.2, 3.0))
        ax.plot(Pc, [l["verif_custo"] for l in comuns], "o-", color=AZUL, lw=2, ms=5,
                mec="white", mew=1.5, label="1 MPI_Allreduce")
        ax.plot(Pc, [bc[p]["verif_custo"] for p in Pc], "s-", color=LARANJA, lw=2, ms=5,
                mec="white", mew=1.5, label="P MPI_Bcast")
        ax.set_xscale("log", base=2)
        ax.set_xticks(Pc, [str(p) for p in Pc])
        ax.set_xlabel("processos (P)")
        ax.set_ylabel("verificação, custo (s)")
        ax.legend(frameon=False, loc="upper left")
        fig.tight_layout()
        fig.savefig(saida / "verificacao.png", dpi=200)
    print(f"\ngraficos e tabela.csv em {saida}/")


if __name__ == "__main__":
    main()
