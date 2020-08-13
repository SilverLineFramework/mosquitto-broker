import numpy as np
import argparse

import matplotlib.pyplot as plt
plt.style.use("seaborn-whitegrid")

def plot_data(name, bound, data):
    interval = int(name.split("_")[-1][1:])

    if bound < 0:
        bound = len(data["avg_lats"])
    else:
        bound //= interval

    avg_lats = data["avg_lats"][:bound]
    mbps_sent = data["bpms_sent"][:bound] * 0.001 # turn to Mb/s
    mbps_recvd = data["bpms_recvd"][:bound] * 0.001 # turn to Mb/s
    dropped_clients = data["dropped_clients"][:bound]
    dropped_packets_percent = data["dropped_packets_percent"][:bound] * 100
    cpu = data["cpu"][:bound] * 100
    mem = data["mem"][:bound] * 100

    clients = [interval*int(n) for n in range(1, avg_lats.shape[0]+1)][:bound]

    try:
        avg_lats = np.mean(avg_lats, axis=1)[:bound]
        std_lats = np.std(avg_lats, axis=1)[:bound]
    except:
        avg_lats = list(map(np.mean, avg_lats))[:bound]
        std_lats = list(map(np.std, avg_lats))[:bound]

    # plot latency
    plt.figure(figsize=(20,10))
    plt.title("Latency")
    plt.xlabel("Number of Clients")
    plt.ylabel("Avg Latency (ms)")
    plt.errorbar(clients, avg_lats, yerr=std_lats, fmt="--b.", solid_capstyle="projecting", capsize=5)
    plt.bar(clients, dropped_clients)
    plt.savefig(f"plots/{name}_latency.png")

    # plot bandwidth and dropped packets
    plt.figure(figsize=(20,10))
    plt.subplot(2, 1, 1)
    plt.title("Bandwidth")
    plt.xlabel("Number of Clients")
    plt.ylabel("MB/s")
    plt.plot(clients, mbps_sent, "--b.", label="sent")
    plt.plot(clients, mbps_recvd, "--r.", label="received")
    plt.legend()

    plt.subplot(2, 1, 2)
    plt.title("% Packet Loss")
    plt.xlabel("Number of Clients")
    plt.ylabel("% Dropped Packets")
    plt.plot(clients, dropped_packets_percent, "--b.")
    plt.legend()
    plt.savefig(f"plots/{name}_bandwidth+packet_loss.png")

    # plot cpu and memory usage
    plt.figure(figsize=(20,10))
    plt.subplot(2, 1, 1)
    plt.title("CPU Usage")
    plt.ylabel("% CPU")
    plt.plot(clients, cpu, "--b.")

    plt.subplot(2, 1, 2)
    plt.title("Memory Usage")
    plt.xlabel("Number of Clients")
    plt.ylabel("% Virtual Memory")
    plt.plot(clients, mem, "--b.")
    plt.savefig(f"plots/{name}_cpu+mem.png")

def main(filename, bound):
    data = np.load(filename, allow_pickle=True)
    name = filename.split(".")[-2].split("/")[-1]
    plot_data(name, bound, data)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=("ARENA MQTT broker benchmarking"))

    parser.add_argument("-f", "--filename", type=str, help=".npz file to plot",
                        default="")
    parser.add_argument("-b", "--bound", type=int, help="upper bound of clients to plot to",
                        default=-1)

    args = parser.parse_args()

    main(**vars(args))
