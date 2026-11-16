#!/bin/bash
# Run from the ns-3 root directory (where ./ns3 or ./waf lives).
# Prerequisite: cp tp3-lrwpan-mqtt.cc scratch/
PROG="scratch/tp3-lrwpan-mqtt"
RUNS="1 2 3 4 5"          # 5 seeds per point -> mean +/- std
rm -f resA.csv resB.csv resC.csv

./ns3 build || exit 1      # older versions: ./waf build
run() { ./ns3 run "$PROG $*" --no-build; }   # older: ./waf --run "$PROG $*" --no-build

# Exp A: PDR vs number of nodes (interval=1 s, 0 dBm)
for n in 5 10 15 20 30 40; do
  for r in $RUNS; do run --nNodes=$n --interval=1 --txPower=0 --run=$r --out=resA.csv; done
done

# Exp B: Throughput vs send interval (20 nodes, 0 dBm)
for i in 0.05 0.1 0.25 0.5 1 2 5; do
  for r in $RUNS; do run --nNodes=20 --interval=$i --txPower=0 --run=$r --out=resB.csv; done
done

# Exp C: Delay vs Tx power (10 nodes, interval=0.5 s, far sensors so power matters)
for p in -25 -20 -15 -10 -5 0 5; do
  for r in $RUNS; do run --nNodes=10 --interval=0.5 --txPower=$p --radius=40 --run=$r --out=resC.csv; done
done

python3 plot_results.py
