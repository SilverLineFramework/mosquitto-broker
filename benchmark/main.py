import argparse
import time, random, string, signal, sys
from camera import Camera
from benchmark import Benchmark

def make_scene():
    return "benchmark_"+''.join(random.choice(string.ascii_lowercase+string.digits) for i in range(5))

def main(num_cams, num_threads, timeout, broker, port, identifier):
    test = Benchmark(identifier, num_cams, num_threads, timeout*60000, broker, port, make_scene())
    test.start()
    test.save()
    print(test.get_avg_lat(), "ms")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=("ARENA MQTT broker benchmarking"))

    parser.add_argument("-c", "--num_cams", type=int, help="Number of clients to spawn",
                        default=1)
    parser.add_argument("-t", "--num_threads", type=int, help="Number of threads to spawn",
                        default=1)
    parser.add_argument("-b", "--broker", type=str, help="Broker to connect to",
                        default="oz.andrew.cmu.edu")
    parser.add_argument("-p", "--port", type=int, help="Port to connect to",
                        default=9001)
    parser.add_argument("-i", "--identifier", type=str, help="Optional id for saved plot",
                        default="")
    parser.add_argument("-o", "--timeout", type=int, help="Amount of mins to wait before ending data collection",
                        default=5) # default is 5 mins

    args = parser.parse_args()

    main(**vars(args))
