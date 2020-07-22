import numpy as np
import time, random, string, signal, sys
import threading, logging
from camera import Camera

def time_ms():
    return time.time()*1000

def rand_color():
    r = lambda: random.randint(0,255)
    return "#%02X%02X%02X" % (r(),r(),r())

# root mean squared deviation
def rmsd(arr):
    avg = np.mean(arr)
    diffs_sq = (np.array(arr) - avg)**2
    rmsd = np.sum(diffs_sq) / len(diffs_sq)
    return rmsd

class GracefulKiller:
    def __init__(self):
        self.kill_now = False
        signal.signal(signal.SIGINT, self.exit_gracefully)
        signal.signal(signal.SIGTERM, self.exit_gracefully)

    def exit_gracefully(self, signum, frame):
        self.kill_now = True

class Benchmark(object):
    def __init__(self, name, num_cams, num_threads, timeout, broker, port, scene):
        self.name = name
        self.num_cams = num_cams
        self.num_threads = num_threads
        self.broker = broker
        self.port = port
        self.scene = scene
        self.tot_lat = 0
        self.cnt = 0
        self.avg_lats = []
        self.times = []
        self.cams = []
        self.threads = []
        self.lock = threading.Lock()
        self.killer = GracefulKiller()
        self.timeout = timeout

    def start(self):
        for _ in range(self.num_cams):
            self.add_cam()

        bins = np.array_split(np.array(self.cams), self.num_threads)
        for i in range(self.num_threads):
            thread = threading.Thread(target=self.move_cam, args=(bins[i],))
            self.threads += [thread]
            thread.start()

        print(f"Started! Scene is {self.scene}")
        self.collect()

    def collect(self):
        iters = 0
        start_t = time_ms()
        while True:
            now = time_ms()
            if int(now - start_t) % 100 == 0: # 10 Hz
                with self.lock:
                    if self.cnt != 0:
                        self.avg_lats += [self.tot_lat / self.cnt]
                        self.times += [int(now - start_t) / 1000]

            if iters % (self.num_cams*1000) == 0:
                sys.stdout.write(".")
                sys.stdout.flush()

            if int(now - start_t) > self.timeout:
                self.killer.kill_now = True
                print("Timeout reached, exiting...")
                break

            if len(self.avg_lats) > 100 and rmsd(self.avg_lats[-100:]) < 0.0001:
                self.killer.kill_now = True
                print("RMSD threshold crossed, exiting...")
                break

            if self.killer.kill_now:
                if input("Terminate [y/n]? ") == "y":
                    break
                self.killer.kill_now = False

            iters += 1
            time.sleep(0.001)

        self.cleanup()

    def cleanup(self):
        for thread in self.threads:
            thread.join()

        for c in self.cams:
            c.disconnect()

    def get_avg_lat(self):
        return np.mean(self.avg_lats[-100:])

    def save(self):
        with open(f"data/time_vs_lat_{self.name}.txt", "w") as f:
            f.write(f"{self.num_cams},{self.num_threads}\n")
            f.writelines("%s," % t for t in self.times)
            f.write("\n")
            f.writelines("%s," % a for a in self.avg_lats)
            f.close()

    def add_cam(self):
        cam = Camera(f"cam{len(self.cams)}", self.scene, rand_color())
        cam.connect(self.broker, self.port)
        self.cams += [cam]

    def move_cam(self, cams):
        start_t = time_ms()
        while True:
            now = time_ms()
            if int(now - start_t) % 100 == 0: # 10 Hz
                for cam in cams:
                    cam.move()
                    with self.lock:
                        if cam.lat > 0:
                            self.tot_lat += cam.lat
                            self.cnt += 1
            time.sleep(0.001)

            if self.killer.kill_now:
                break
