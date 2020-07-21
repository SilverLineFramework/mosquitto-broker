import numpy as np
import argparse
import time, signal, threading, logging
from camera import Camera

import matplotlib.pyplot as plt
plt.style.use('seaborn-whitegrid')

class GracefulKiller:
    def __init__(self):
        self.kill_now = False
        signal.signal(signal.SIGINT, self.exit_gracefully)
        signal.signal(signal.SIGTERM, self.exit_gracefully)

    def exit_gracefully(self, signum, frame):
        self.kill_now = True

lock = threading.Lock()
tot, cnt = 0, 0
avgs = []
times = []

def add_cam(cams, broker, port):
    cam = Camera(f"cam{len(cams)}", "bench", "#52e8be")
    cam.connect(broker, port)
    cams += [cam]

def move_cam(cams, killer):
    global lock
    global tot
    global cnt

    while 1:
        for cam in cams:
            cam.move()
            with lock:
                if cam.lat > 0:
                    tot += cam.lat
                    cnt += 1
            time.sleep(0.5)

        if killer.kill_now:
            break

def main(num_cams, num_threads, broker, port, identifier):
    global lock
    global tot
    global cnt
    global avgs
    global times

    killer = GracefulKiller()

    cams = []
    threads = []
    for _ in range(num_cams):
        add_cam(cams, broker, port)

    bins = np.array_split(np.array(cams), num_threads)
    for i in range(num_threads):
        thread = threading.Thread(target=move_cam, args=(bins[i],killer,))
        threads += [thread]
        thread.start()

    start_t = time.time()*1000
    while True:
        now = time.time()*1000
        if int(now - start_t) % 1000 == 0:
            with lock:
                if cnt != 0:
                    avgs += [tot / cnt]
                    times += [int(now - start_t) / 1000]
            time.sleep(0.01)

        if killer.kill_now:
            if input("Terminate [y/n]? ") == "y":
                break

    for thread in threads:
        thread.join()

    for c in cams:
        c.disconnect()

    plt.plot(times, avgs)
    plt.title(f"Time vs Response Time - {num_cams} client(s), {num_threads} thread(s)")
    plt.xlabel("Time (s)")
    plt.ylabel("Response Time (ms)")
    plt.savefig(f'plots/time_vs_lat_{identifier}.png')
    # plt.show()

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

    args = parser.parse_args()

    main(**vars(args))
