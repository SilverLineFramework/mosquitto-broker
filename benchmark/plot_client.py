import numpy as np
import argparse

import matplotlib.pyplot as plt
plt.style.use('seaborn-whitegrid')

def plot_lat_vs_client(name, bound, data):
    clients = [int(n) for n in range(1, data.shape[0]+1)][:bound]
    avgs = np.mean(data, axis=1)[:bound]
    st_devs = np.std(data, axis=1)[:bound]

    plt.figure(figsize=(35,15))
    plt.title(f"Latency vs Num Clients")
    plt.xlabel("Number of Clients")
    plt.ylabel("Avg Latency (ms)")
    plt.errorbar(clients, avgs, yerr=st_devs, fmt="--bo", solid_capstyle='projecting', capsize=5)
    plt.savefig(f"plots/{name}.png")

def main(filename, bound):
    num_cams = 0
    clients = []
    avgs = []

    data = np.load(filename, allow_pickle=True)

    name = filename.split(".")[:-1][0].split("/")[-1]

    plot_lat_vs_client(name, bound, data)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=("ARENA MQTT broker benchmarking"))

    parser.add_argument("-f", "--filename", type=str, help="txt file to",
                        default="")
    parser.add_argument("-b", "--bound", type=int, help="upper bound to plot to",
                        default=-1)

    args = parser.parse_args()

    main(**vars(args))
