#!/usr/bin/env python3
"""Avalia os clusters contra os rotulos do CTU-13 (fora da medicao).

O k-means nao viu os rotulos. Aqui cada cluster e classificado como "botnet"
se, entre os seus fluxos rotulados (normal + botnet; background nao tem
rotulo confiavel), a fracao de botnet passa de --limiar. A deteccao e medida
nos fluxos rotulados: um fluxo e alertado se cai num cluster botnet.

Uso: python3 scripts/eval.py dados/ctu13_s1 membros.bin [--limiar 0.5]
"""
import argparse
import json

import numpy as np

MOSTRAR = ["log_dur", "log_pkts", "log_bytes", "frac_src_bytes", "proto_tcp",
           "proto_udp", "proto_icmp", "dport_dns", "dport_web", "dport_smtp", "dport_irc"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("prefixo", help="prefixo usado no prep.py")
    ap.add_argument("membros", help="saida -o do kmeans_seq/kmeans_mpi")
    ap.add_argument("--limiar", type=float, default=0.5)
    a = ap.parse_args()

    meta = json.load(open(a.prefixo + ".json"))
    n, d, q = meta["n"], meta["d"], meta["q"]
    rot = np.fromfile(a.prefixo + ".rotulos", dtype=np.int8)
    m = np.fromfile(a.membros, dtype=np.int32)
    assert len(rot) == n and len(m) == n, "tamanhos nao batem"
    x = np.memmap(a.prefixo + ".bin", dtype=np.int32, mode="r", offset=20, shape=(n, d))
    lo, hi = np.array(meta["min"]), np.array(meta["max"])
    k = int(m.max()) + 1

    cont = np.zeros((k, 3), dtype=np.int64)
    np.add.at(cont, (m, rot), 1)
    rotulados = cont[:, 1] + cont[:, 2]
    frac_bot = np.divide(cont[:, 2], rotulados, out=np.zeros(k), where=rotulados > 0)
    eh_bot = (frac_bot >= a.limiar) & (cont[:, 2] > 0)

    idx = {f: meta["features"].index(f) for f in MOSTRAR if f in meta["features"]}
    print(f"{'cl':>3} {'fluxos':>9} {'backgr':>9} {'normal':>7} {'botnet':>8} "
          f"{'%bot*':>6}  perfil medio do cluster")
    for c in np.argsort(-cont.sum(1)):
        sel = m == c
        media = lo + x[sel].mean(0) / q * (hi - lo) if sel.any() else lo
        perfil = []
        for f, j in idx.items():
            v = media[j]
            if f.startswith("log_"):
                perfil.append(f"{f[4:]}={np.expm1(v):.3g}")
            elif v >= 0.5:
                perfil.append(f.split("_", 1)[1])
        marca = " <== BOTNET" if eh_bot[c] else ""
        print(f"{c:>3} {cont[c].sum():>9} {cont[c, 0]:>9} {cont[c, 1]:>7} {cont[c, 2]:>8} "
              f"{100 * frac_bot[c]:>5.1f}%  {' '.join(perfil)}{marca}")
    print("* fracao de botnet entre os fluxos rotulados (normal + botnet) do cluster")

    tp = int(cont[eh_bot, 2].sum())
    fp = int(cont[eh_bot, 1].sum())
    fn = int(cont[~eh_bot, 2].sum())
    prec = tp / (tp + fp) if tp + fp else 0.0
    rec = tp / (tp + fn) if tp + fn else 0.0
    f1 = 2 * prec * rec / (prec + rec) if prec + rec else 0.0
    pureza = cont[:, 1:].max(1).sum() / max(1, rotulados.sum())
    print(f"\nclusters botnet: {int(eh_bot.sum())} de {k}")
    print(f"deteccao (fluxos rotulados): precisao={prec:.3f} revocacao={rec:.3f} "
          f"F1={f1:.3f}  (TP={tp} FP={fp} FN={fn})")
    print(f"pureza (normal x botnet): {pureza:.3f}")
    print(f"background nos clusters botnet: {int(cont[eh_bot, 0].sum())} fluxos "
          f"({100 * cont[eh_bot, 0].sum() / max(1, cont[:, 0].sum()):.1f}% do background)")


if __name__ == "__main__":
    main()
