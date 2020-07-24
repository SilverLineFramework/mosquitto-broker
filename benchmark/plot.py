import numpy as np
import argparse

import matplotlib.pyplot as plt
plt.style.use('seaborn-whitegrid')

def plot_line(name, num_cams, times, avgs):
    plt.title(f"Time vs Latency - {num_cams} client(s)")
    plt.xlabel("Time (s)")
    plt.ylabel("Latency (ms)")
    plt.plot(times, avgs, "--b.")
    plt.savefig(f"plots/{name}_line.png")

def plot_hist(name, num_cams, avgs):
    plt.title(f"Hist - {num_cams} client(s)")
    plt.xlabel("Latency (ms)")
    plt.ylabel("Freq")
    plt.hist(avgs, bins=100, density=True, orientation="horizontal")
    plt.savefig(f"plots/{name}_hist.png")

def plot_both(name, num_cams, times, avgs):
    plt.subplots_adjust(wspace=0.35)

    plt.subplot(1, 2, 1)
    plt.title(f"Time vs Latency")
    plt.xlabel("Time (s)")
    plt.ylabel("Latency (ms)")
    plt.plot(times, avgs, "--b.")

    plt.subplot(1, 2, 2)
    plt.title(f"Histogram")
    plt.xlabel("Freq")
    plt.ylabel("Latency (ms)")
    plt.hist(avgs, bins=100, density=True, orientation="horizontal")

    plt.savefig(f"plots/{name}.png")

def main(filename, plot_type):
    num_cams = 0
    times = []
    avgs = []

    with open(filename, "r") as f:
        contents = f.readlines()

        info = contents[0].split(",")
        num_cams = int(info[0])

        times = [float(n) for n in contents[1].split(",")[:-1]]
        avgs = [float(n) for n in contents[2].split(",")[:-1]]

    name = filename.split(".")[:-1][0].split("/")[-1]

    if plot_type == "line":
        plot_line(name, num_cams, times, avgs)
    elif plot_type == "hist":
        plot_hist(name, num_cams, avgs)
    else:
        plot_both(name, num_cams, times, avgs)
    # plt.show()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=("ARENA MQTT broker benchmarking"))

    parser.add_argument("-f", "--filename", type=str, help="txt file to",
                        default="")
    parser.add_argument("-p", "--plot_type", type=str, help="line plot, histogram, or both",
                        default="both")

    args = parser.parse_args()

    main(**vars(args))
