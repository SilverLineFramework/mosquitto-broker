import numpy as np
import argparse

import matplotlib.pyplot as plt
plt.style.use("seaborn-whitegrid")

def plot_lat_vs_client(name, bound, data):
    interval = int(name.split("_")[-1][1:])

    if bound is None:
        bound = len(data["avg_lats"])
    else:
        bound //= interval

    dropped = data["dropped"][:bound]
    avg_lats = data["avg_lats"][:bound]
    avg_bpms = data["avg_bpms"][:bound]
    cpu = data["cpu"][:bound]
    mem = data["mem"][:bound]

    clients = [interval*int(n) for n in range(1, avg_lats.shape[0]+1)][:bound]

    try:
        avgs = np.mean(avg_lats, axis=1)[:bound]
        st_devs = np.std(avg_lats, axis=1)[:bound]
        bpms = np.mean(avg_bpms, axis=1)[:bound]
        bpms_stds = np.std(avg_bpms, axis=1)[:bound]
    except:
        avgs = list(map(np.mean, avg_lats))[:bound]
        st_devs = list(map(np.std, avg_lats))[:bound]
        bpms = list(map(np.mean, avg_bpms))[:bound]
        bpms_stds = list(map(np.std, avg_bpms))[:bound]

    plt.figure(figsize=(20,10))
    plt.title("Latency vs Num Clients")
    plt.xlabel("Number of Clients")
    plt.ylabel("Avg Latency (ms)")
    plt.errorbar(clients, avgs, yerr=st_devs, fmt="--b.", solid_capstyle="projecting", capsize=5)
    plt.bar(clients, dropped)
    plt.savefig(f"plots/{name}.png")

    plt.figure(figsize=(20,10))
    plt.title("Bytes/ms vs Num Clients")
    plt.xlabel("Number of Clients")
    plt.ylabel("Bytes/Millisecond (bpms)")
    plt.errorbar(clients, bpms, yerr=bpms_stds, fmt="--b.", solid_capstyle="projecting", capsize=5)
    plt.bar(clients, dropped)
    plt.savefig(f"plots/{name}_bpms.png")

    plt.figure(figsize=(20,10))
    plt.title("% CPU vs Num Clients")
    plt.xlabel("Number of Clients")
    plt.ylabel("% CPU used")
    plt.plot(clients, cpu, "--b.")
    plt.savefig(f"plots/{name}_cpu.png")

    plt.figure(figsize=(20,10))
    plt.title("% Memory vs Num Clients")
    plt.xlabel("Number of Clients")
    plt.ylabel("% Memory used")
    plt.plot(clients, mem, "--b.")
    plt.savefig(f"plots/{name}_mem.png")

def main(filename, bound):
    data = np.load(filename, allow_pickle=True)
    name = filename.split(".")[:-1][0].split("/")[-1]
    plot_lat_vs_client(name, bound, data)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=("ARENA MQTT broker benchmarking"))

    parser.add_argument("-f", "--filename", type=str, help="txt file to",
                        default="")
    parser.add_argument("-b", "--bound", type=int, help="upper bound to plot to",
                        default=None)

    args = parser.parse_args()

    main(**vars(args))
