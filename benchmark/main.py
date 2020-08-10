import argparse
import numpy as np
import time, random, string, signal, sys
from camera import Camera
from benchmark import Benchmark

def make_scene():
    return "benchmark_"+"".join(random.choice(string.ascii_lowercase+string.digits) for i in range(5))

def main(max_cams, timeout, broker, port, name, interval):
    np.set_printoptions(precision=2)
    dropped = []
    cpu = []
    mem = []
    avg_lats = []
    bpms = []
    for num_cams in range(1, max_cams+interval, interval):
        test = Benchmark(f"{name}_c{num_cams}", num_cams, timeout*60000, broker, port, make_scene())
        test.run()

        avg_lats += [test.get_avg_lats() if test.get_avg_lats() else [-1] * 100]
        bpms += [test.get_bpms()]
        dropped += [test.get_dropped_cams()]
        cpu += [np.mean(test.get_cpu())]
        mem += [np.mean(test.get_mem())]
        print(f"{num_cams} clients -> {np.mean(avg_lats[-1])} ms | {bpms[-1]} bytes/ms | {dropped[-1]} dropped | {cpu[-1]}% cpu usage | {mem[-1]}% mem usage")

        test.save()

        time.sleep(1)

    dropped = np.array(dropped)
    cpu = np.array(cpu)
    mem = np.array(mem)
    avg_lats = np.array(avg_lats)
    bpms = np.array(bpms)
    np.savez(f"data/avg_lats_{name}_c{num_cams}_i{interval}", avg_lats=avg_lats, bpms=bpms, dropped=dropped, cpu=cpu, mem=mem)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=("ARENA MQTT broker benchmarking"))

    parser.add_argument("-c", "--max_cams", type=int, help="Number of clients to spawn",
                        default=1)
    parser.add_argument("-b", "--broker", type=str, help="Broker to connect to",
                        default="oz.andrew.cmu.edu")
    parser.add_argument("-p", "--port", type=int, help="Port to connect to",
                        default=9001)
    parser.add_argument("-n", "--name", type=str, help="Optional name for saved plot",
                        default="benchmark")
    parser.add_argument("-t", "--timeout", type=int, help="Amount of mins to wait before ending data collection",
                        default=3) # default is 3 mins
    parser.add_argument("-i", "--interval", type=int, help="Interval of clients for benchmarking",
                        default=1)

    args = parser.parse_args()

    main(**vars(args))
