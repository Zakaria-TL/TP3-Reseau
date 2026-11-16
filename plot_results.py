import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def plot(csv, x, y, xlabel, ylabel, title, outfile, logx=False):
    df = pd.read_csv(csv)
    g = df.groupby(x)[y].agg(["mean", "std"]).fillna(0).reset_index()
    plt.figure(figsize=(6, 4))
    plt.errorbar(g[x], g["mean"], yerr=g["std"], marker="o", capsize=3)
    if logx:
        plt.xscale("log")
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.title(title)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(outfile, dpi=200)
    plt.close()
    print("saved", outfile)


plot("resA.csv", "nNodes", "pdr", "Number of sensor nodes", "PDR (%)",
     "PDR vs number of nodes", "pdr_vs_nodes.png")
plot("resB.csv", "interval", "throughput", "Send interval (s)", "Throughput (bytes/s)",
     "Throughput vs send interval", "thr_vs_interval.png", logx=True)
plot("resC.csv", "txPower", "delay_ms", "Tx power (dBm)", "Average delay (ms)",
     "End-to-end delay vs Tx power", "delay_vs_txpower.png")

# Bonus: collisions/drops vs number of nodes
plot("resA.csv", "nNodes", "phyRxDrop", "Number of sensor nodes", "PHY RX drops",
     "Collisions (PHY RX drops) vs number of nodes", "collisions_vs_nodes.png")
