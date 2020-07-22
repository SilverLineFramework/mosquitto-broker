import numpy as np
import argparse
import time, random, string, signal, sys
import threading, logging
from camera import Camera

SCENE="bench_"+''.join(random.choice(string.ascii_lowercase+string.digits) for i in range(5))

class GracefulKiller:
    def __init__(self):
        self.kill_now = False
        signal.signal(signal.SIGINT, self.exit_gracefully)
        signal.signal(signal.SIGTERM, self.exit_gracefully)

    def exit_gracefully(self, signum, frame):
        self.kill_now = True

lock = threading.Lock()
tot_lat, cnt = 0, 0
avgs = []
times = []

def time_ms():
    return time.time()*1000

# root mean squared deviation
def rmsd(arr):
    avg = np.mean(arr)
    diffs_sq = (np.array(arr) - avg)**2
    rmsd = np.sum(diffs_sq) / len(diffs_sq)
    return rmsd

def add_cam(cams, broker, port):
    r = lambda: random.randint(0,255)
    cam = Camera(f"cam{len(cams)}", SCENE, "#%02X%02X%02X" % (r(),r(),r()))
    cam.connect(broker, port)
    cams += [cam]

def move_cam(cams, killer):
    global lock
    global tot_lat
    global cnt

    start_t = time_ms()
    while True:
        now = time_ms()
        if int(now - start_t) % 100 == 0: # 10 Hz
            for cam in cams:
                cam.move()
                with lock:
                    if cam.lat > 0:
                        tot_lat += cam.lat
                        cnt += 1
        time.sleep(0.001)

        if killer.kill_now:
            break

def main(num_cams, num_threads, broker, port, identifier):
    global lock
    global tot_lat
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

    print(f"Started! Scene is {SCENE}")

    iters = 0
    start_t = time_ms()
    while True:
        now = time_ms()
        if int(now - start_t) % 100 == 0: # 10 Hz
            with lock:
                if cnt != 0:
                    avgs += [tot_lat / cnt]
                    times += [int(now - start_t) / 1000]

        if iters % (num_cams*1000) == 0:
            sys.stdout.write(".")
            sys.stdout.flush()

        if len(avgs) > 50 and rmsd(avgs[-50:]) < 0.0001:
            killer.kill_now = True
            print("Converged!")
            break

        if killer.kill_now:
            if input("Terminate [y/n]? ") == "y":
                break
            killer.kill_now = False

        iters += 1
        time.sleep(0.001)

    for thread in threads:
        thread.join()

    for c in cams:
        c.disconnect()

    with open(f"data/time_vs_lat_{identifier}.txt", "w") as f:
        f.write(f"{num_cams},{num_threads}\n")
        f.writelines("%s," % t for t in times)
        f.write("\n")
        f.writelines("%s," % a for a in avgs)
        f.close()

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
