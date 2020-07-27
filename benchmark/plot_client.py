import numpy as np
import argparse

import matplotlib.pyplot as plt
plt.style.use('seaborn-whitegrid')

def plot_lat_vs_client(name, num_cams, clients, avgs):
    plt.title(f"Latency vs Num Clients")
    plt.xlabel("Number of Clients")
    plt.ylabel("Avg Latency (ms)")
    plt.plot(clients, avgs, "--b.")
    plt.savefig(f"plots/{name}.png")

def main(filename, bound):
    num_cams = 0
    clients = []
    avgs = []

    with open(filename, "r") as f:
        contents = f.readlines()

        clients = [int(n) for n in contents[0].split(",")[:-1]]
        bound = len(clients) if bound < 0 else bound
        clients = clients[:bound]
        avgs = [float(n) for n in contents[1].split(",")[:-1]]
        avgs = avgs[:bound]

    name = filename.split(".")[:-1][0].split("/")[-1]

    plot_lat_vs_client(name, num_cams, clients, avgs)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=("ARENA MQTT broker benchmarking"))

    parser.add_argument("-f", "--filename", type=str, help="txt file to",
                        default="")
    parser.add_argument("-b", "--bound", type=int, help="upper bound to plot to",
                        default=-1)

    args = parser.parse_args()

    main(**vars(args))
