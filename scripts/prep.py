#!/usr/bin/env python3
"""Converte arquivos .binetflow do CTU-13 na entrada do k-means (fora da medicao).

Saidas (prefixo -o):
  <prefixo>.bin      cabecalho KMQ1 + N x D int32 em [0, Q]   (ver src/kmeans.h)
  <prefixo>.rotulos  N x int8: 0 = background, 1 = normal, 2 = botnet
  <prefixo>.json     nomes das features, min/max, contagens por rotulo

Rotulos (coluna Label do CTU-13): "From-Botnet" = botnet (fluxos gerados pelo
malware); "From-Normal"/"To-Normal" = normal; o resto (Background, To-Botnet)
= background. O k-means nunca ve os rotulos: eles so servem para o eval.py.

Uso: python3 scripts/prep.py capture20110810.binetflow [outros.binetflow ...] \
        -o dados/ctu13_s1 [-q 10000]
"""
import argparse
import json

import numpy as np
import pandas as pd

COLUNAS = ["StartTime", "Dur", "Proto", "SrcAddr", "Sport", "Dir", "DstAddr",
           "Dport", "State", "sTos", "dTos", "TotPkts", "TotBytes", "SrcBytes",
           "Label"]

# servicos comuns no trafego de botnet do CTU-13 (C&C por IRC, spam, DNS)
SERVICOS = {
    "dport_dns": lambda p: p == 53,
    "dport_web": lambda p: p.isin([80, 443, 8080]),
    "dport_smtp": lambda p: p.isin([25, 465, 587]),
    "dport_irc": lambda p: (p >= 6660) & (p <= 6669),
}


def porta(s):
    """Portas vem em decimal ou hexa ('0x0303', ICMP); vazio vira -1."""
    def conv(v):
        try:
            return int(v, 0)
        except (TypeError, ValueError):
            return -1
    return s.astype(str).map(conv).astype(np.int64)


def le(arquivos):
    partes = []
    for a in arquivos:
        df = pd.read_csv(a, usecols=["Dur", "Proto", "Sport", "Dir", "Dport",
                                     "TotPkts", "TotBytes", "SrcBytes", "Label"],
                         dtype={"Sport": str, "Dport": str, "Proto": str,
                                "Dir": str, "Label": str},
                         low_memory=False)
        print(f"{a}: {len(df)} fluxos")
        partes.append(df)
    return pd.concat(partes, ignore_index=True)


def features(df):
    dur = df["Dur"].fillna(0).clip(lower=0).astype(float)
    pk = df["TotPkts"].fillna(0).clip(lower=0).astype(float)
    by = df["TotBytes"].fillna(0).clip(lower=0).astype(float)
    src = df["SrcBytes"].fillna(0).clip(lower=0).astype(float).clip(upper=by)
    dst = by - src
    proto = df["Proto"].fillna("").str.lower()
    dport = porta(df["Dport"])

    f = pd.DataFrame({
        "log_dur": np.log1p(dur),
        "log_pkts": np.log1p(pk),
        "log_bytes": np.log1p(by),
        "log_src_bytes": np.log1p(src),
        "log_dst_bytes": np.log1p(dst),
        "log_bytes_por_pkt": np.log1p(by / pk.clip(lower=1)),
        "frac_src_bytes": np.where(by > 0, src / by.clip(lower=1), 0.0),
        "log_pkts_por_s": np.log1p(pk / dur.clip(lower=1e-3)),
        "proto_tcp": proto == "tcp",
        "proto_udp": proto == "udp",
        "proto_icmp": proto == "icmp",
        "proto_outro": ~proto.isin(["tcp", "udp", "icmp"]),
        "dport_conhecida": (dport >= 0) & (dport < 1024),
        "dport_registrada": (dport >= 1024) & (dport < 49152),
        "dport_dinamica": dport >= 49152,
        "bidirecional": df["Dir"].fillna("").str.contains("<"),
    })
    for nome, cond in SERVICOS.items():
        f[nome] = cond(dport)
    return f.astype(float)


def rotulos(df):
    lab = df["Label"].fillna("")
    r = np.zeros(len(df), dtype=np.int8)
    r[lab.str.contains("Normal", regex=False).to_numpy()] = 1
    r[lab.str.contains("From-Botnet", regex=False).to_numpy()] = 2
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("arquivos", nargs="+")
    ap.add_argument("-o", "--saida", required=True)
    ap.add_argument("-q", type=int, default=10000, help="escala da quantizacao")
    ap.add_argument("--max", type=int, default=0,
                    help="usa no maximo N fluxos, em passo regular (calibracao do tempo)")
    a = ap.parse_args()

    df = le(a.arquivos)
    if a.max and len(df) > a.max:
        df = df.iloc[np.linspace(0, len(df) - 1, a.max).astype(np.int64)].reset_index(drop=True)
        print(f"amostra regular de {a.max} fluxos")
    f = features(df)
    lo, hi = f.min(), f.max()
    larg = (hi - lo).replace(0, 1)
    x = np.rint((f - lo) / larg * a.q).astype(np.int32).to_numpy()
    r = rotulos(df)
    n, d = x.shape

    with open(a.saida + ".bin", "wb") as fp:
        fp.write(b"KMQ1")
        fp.write(np.array([n], dtype=np.int64).tobytes())
        fp.write(np.array([d, a.q], dtype=np.int32).tobytes())
        fp.write(np.ascontiguousarray(x).tobytes())
    r.tofile(a.saida + ".rotulos")
    meta = {
        "arquivos": a.arquivos, "n": int(n), "d": int(d), "q": a.q,
        "features": list(f.columns),
        "min": lo.round(6).tolist(), "max": hi.round(6).tolist(),
        "rotulos": {"background": int((r == 0).sum()), "normal": int((r == 1).sum()),
                    "botnet": int((r == 2).sum())},
    }
    with open(a.saida + ".json", "w") as fp:
        json.dump(meta, fp, indent=2, ensure_ascii=False)
    print(f"{a.saida}.bin: N={n} D={d} Q={a.q}  "
          f"({n * d * 4 / 2**20:.0f} MiB)  rotulos={meta['rotulos']}")


if __name__ == "__main__":
    main()
