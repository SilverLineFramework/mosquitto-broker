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

def add_cam(cams):
    cam = Camera(f"cam{len(cams)}", "bench", "#52e8be")
    cam.connect("oz.andrew.cmu.edu", 9001)
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

def main(num_cams, num_threads):
    global lock
    global tot
    global cnt
    global avgs
    global times

    killer = GracefulKiller()

    cams = []
    threads = []
    for _ in range(num_cams):
        add_cam(cams)

    bins = np.array_split(np.array(cams), num_threads)
    for i in range(num_threads):
        thread = threading.Thread(target=move_cam, args=(bins[i],killer,))
        threads += [thread]
        thread.start()

    start_t = time.time()*1000000
    while True:
        now = time.time()*1000000
        if int(now - start_t) % 1000000 == 0:
            with lock:
                if cnt != 0:
                    avgs += [tot / cnt]
                    times += [int(now - start_t) // 1000000]

        if killer.kill_now:
            break

    for thread in threads:
        thread.join()

    for c in cams:
        c.disconnect()

    print()

    plt.plot(times, avgs)
    plt.title(f"Time vs Response Time - {num_cams} client(s), {num_threads} thread(s)")
    plt.xlabel("Time (s)")
    plt.ylabel("Response Time (ms)")
    plt.savefig(f'plots/time_vs_lat_{num_cams}_{num_threads}.png')
    # plt.show()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=("ARENA MQTT broker benchmarking"))

    parser.add_argument("-c", "--num_cams", type=int, help="Number of clients to spawn",
                        default=1)
    parser.add_argument("-t", "--num_threads", type=int, help="Number of threads to spawn",
                        default=1)

    args = parser.parse_args()

    main(**vars(args))
