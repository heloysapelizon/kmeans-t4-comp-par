#!/usr/bin/env python3
"""Gera um .binetflow sintetico no formato do CTU-13, so para testar o pipeline
sem o dataset (nao use nas medicoes do relatorio).

Mistura: background (web, DNS, ICMP, P2P), normal (web/DNS de hosts conhecidos)
e botnet (C&C por IRC, spam SMTP, flood UDP, varredura TCP).

Uso: python3 scripts/gera_sintetico.py -n 300000 -o dados/sintetico.binetflow
"""
import argparse

import numpy as np
import pandas as pd

# nome, fracao, proto, dport (fixa, lista ou (a, b)), dur, pkts, bytes/pkt, frac_src, dir, rotulo
PERFIS = [
    ("bg_web", .40, "tcp", [80, 443], (2.0, 1.5), (12, 1.0), (700, .6), (.15, .1), "<->", "flow=Background-TCP-Established"),
    ("bg_dns", .20, "udp", [53], (.01, 1.0), (2, .2), (90, .3), (.4, .1), "<->", "flow=Background-UDP-Established"),
    ("bg_icmp", .05, "icmp", ["0x0008"], (.001, 1.0), (1, .3), (80, .2), (1.0, 0), "->", "flow=Background"),
    ("bg_p2p", .16, "udp", (1024, 65535), (.5, 2.0), (3, 1.0), (200, .8), (.5, .2), "<->", "flow=Background-UDP-Attempt"),
    ("normal", .04, "tcp", [80, 443, 53], (3.0, 1.2), (20, 1.0), (800, .5), (.1, .05), "<->", "flow=From-Normal-V42-Grill"),
    ("bot_irc", .03, "tcp", [6667], (300., .8), (40, .5), (90, .2), (.5, .1), "<->", "flow=From-Botnet-V42-TCP-CC-IRC"),
    ("bot_spam", .04, "tcp", [25], (4.0, .6), (15, .4), (400, .3), (.85, .05), "<->", "flow=From-Botnet-V42-TCP-SPAM"),
    ("bot_flood", .05, "udp", (1024, 65535), (.0005, .5), (1, 0), (1066, .05), (1.0, 0), "->", "flow=From-Botnet-V51-UDP-Attempt"),
    ("bot_scan", .02, "tcp", [135, 139, 445], (.0, 0), (1, 0), (62, .02), (1.0, 0), "->", "flow=From-Botnet-V51-TCP-Attempt"),
    ("to_bot", .01, "tcp", [6667], (1.0, 1.0), (5, .5), (100, .3), (.3, .1), "<->", "flow=To-Botnet-V42-TCP"),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-n", type=int, default=300000)
    ap.add_argument("-o", required=True)
    ap.add_argument("--semente", type=int, default=1)
    a = ap.parse_args()
    rng = np.random.default_rng(a.semente)

    fr = np.array([p[1] for p in PERFIS])
    qual = rng.choice(len(PERFIS), size=a.n, p=fr / fr.sum())
    linhas = []
    for i, (nome, _, proto, dport, dur, pk, bpp, fs, dr, lab) in enumerate(PERFIS):
        m = int((qual == i).sum())
        if isinstance(dport, tuple):
            dp = rng.integers(dport[0], dport[1], m).astype(str)
        else:
            dp = rng.choice(np.array(dport, dtype=object), m).astype(str)
        d = np.abs(dur[0] * rng.lognormal(0, dur[1], m)) if dur[0] > 0 else np.zeros(m)
        p = np.maximum(1, np.rint(pk[0] * rng.lognormal(0, pk[1], m))).astype(int)
        b = (p * bpp[0] * rng.lognormal(0, bpp[1], m)).astype(int)
        s = (b * np.clip(rng.normal(fs[0], fs[1], m), 0, 1)).astype(int)
        linhas.append(pd.DataFrame({
            "StartTime": "2011/08/10 09:46:59.607825", "Dur": d.round(6), "Proto": proto,
            "SrcAddr": "147.32.84.165", "Sport": rng.integers(1024, 65535, m).astype(str)
            if proto != "icmp" else "0x0008", "Dir": f"  {dr}", "DstAddr": "60.190.223.75",
            "Dport": dp, "State": "CON", "sTos": 0, "dTos": 0, "TotPkts": p,
            "TotBytes": b, "SrcBytes": s, "Label": lab, "_ord": np.flatnonzero(qual == i)}))
    df = pd.concat(linhas).sort_values("_ord").drop(columns="_ord")
    df.to_csv(a.o, index=False)
    print(f"{a.o}: {len(df)} fluxos")


if __name__ == "__main__":
    main()
