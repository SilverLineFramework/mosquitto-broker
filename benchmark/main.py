import argparse
import numpy as np
import time, random, string, signal, sys
from camera import Camera
from benchmark import Benchmark

def make_scene():
    return "benchmark_"+''.join(random.choice(string.ascii_lowercase+string.digits) for i in range(5))

def main(max_cams, timeout, broker, port, identifier):
    avg_lats = []
    for num_cams in range(1,max_cams+1):
        test = Benchmark(f"{identifier}_c{num_cams}", num_cams, timeout*60000, broker, port, make_scene())
        test.start()
        test.save()

        avg_lats += [test.get_avg_lats()]
        print(f"{num_cams} clients - {np.mean(avg_lats[-1])} ms")
        time.sleep(1)

    avg_lats = np.array(avg_lats)
    # print(avg_lats)
    np.save(f"data/avg_lats_{identifier}_c{num_cams}", avg_lats)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=("ARENA MQTT broker benchmarking"))

    parser.add_argument("-c", "--max_cams", type=int, help="Number of clients to spawn",
                        default=1)
    parser.add_argument("-b", "--broker", type=str, help="Broker to connect to",
                        default="oz.andrew.cmu.edu")
    parser.add_argument("-p", "--port", type=int, help="Port to connect to",
                        default=9001)
    parser.add_argument("-i", "--identifier", type=str, help="Optional id for saved plot",
                        default="")
    parser.add_argument("-t", "--timeout", type=int, help="Amount of mins to wait before ending data collection",
                        default=3) # default is 3 mins

    args = parser.parse_args()

    main(**vars(args))
