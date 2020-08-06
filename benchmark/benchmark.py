import argparse
import numpy as np
import time, random, string, signal, sys, json
from multiprocessing import Process, Value, Queue
import paho.mqtt.client as mqtt
from camera import *
from utils import *

def rand_color():
    r = lambda: random.randint(0,255)
    return "#%02X%02X%02X" % (r(),r(),r())

# root mean squared deviation
def rmsd(arr):
    avg = np.mean(arr)
    diffs_sq = np.square(np.array(arr) - avg)
    rmsd = np.mean(diffs_sq)
    return rmsd

class GracefulKiller:
    def __init__(self):
        self.kill_now = Value("i", 0)
        signal.signal(signal.SIGINT, self.exit_gracefully)
        signal.signal(signal.SIGTERM, self.exit_gracefully)

    def exit_gracefully(self, signum, frame):
        self.kill_now.value = 1

class Benchmark(object):
    def __init__(self, name, num_cams, timeout, broker, port, scene):
        self.name = name
        self.num_cams = num_cams
        self.broker = broker
        self.port = port
        self.scene = scene
        self.dropped = 0
        self.avg_lats = []
        self.avg_bpms = []
        self.times = []
        self.cpu = []
        self.mem = []
        self.dropped_queue = Queue()
        self.lat_queue = Queue()
        self.bpms_queue = Queue()
        self.killer = GracefulKiller()
        self.timeout = timeout
        self.client = mqtt.Client("cpu_mem_log_bench", clean_session=True)
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message

    def on_connect(self, client, userdata, flags, rc):
        client.subscribe("cpu_mem")

    def on_message(self, client, userdata, message):
        msg = json.loads(message.payload.decode())
        if "cpu" in msg and "mem" in msg:
            self.cpu += [msg["cpu"]]
            self.mem += [msg["mem"]]

    def run(self):
        self.client.connect("oz.andrew.cmu.edu", 1883)
        self.client.loop_start()

        ps = [Process(target=self.move_cam, args=()) for _ in range(self.num_cams)]

        for i in range(len(ps)):
            ps[i].start()
            if i != 0 and i % 50 == 0:
                time.sleep(1)

        print(f"Started! Scene is {self.scene}")
        self.collect()

        # clear out all queued results or else code will hang!
        while self.lat_queue.qsize() > 0:
            self.lat_queue.get()

        while self.bpms_queue.qsize() > 0:
            self.bpms_queue.get()

        while self.dropped_queue.qsize() > 0:
            self.dropped += self.dropped_queue.get()

        for p in ps:
            p.join()

        self.client.loop_stop()
        self.client.disconnect()

        print("done!")

    def collect(self):
        iters = 0
        start_t = time_ms()
        while True:
            now = time_ms()
            if int(now - start_t) % 100 == 0: # 10 Hz
                lat_tot = []
                while self.lat_queue.qsize() > 0:
                    lat_tot += [self.lat_queue.get()]

                bpms_tot = []
                while self.bpms_queue.qsize() > 0:
                    bpms_tot += [self.bpms_queue.get()]

                if lat_tot and bpms_tot:
                    self.avg_lats += [np.mean(lat_tot)]
                    self.avg_bpms += [np.mean(bpms_tot)]
                    self.times += [(now - start_t) / 1000]

            if iters % 5000 == 0:
                sys.stdout.write(".")
                sys.stdout.flush()

            if int(now - start_t) > self.timeout:
                self.killer.kill_now.value = 1
                print("Timeout reached, exiting...")
                break

            if len(self.avg_lats) > 100 and rmsd(self.avg_lats[-100:]) < 0.00005:
                self.killer.kill_now.value = 1
                print("RMSD threshold crossed, exiting...")
                break

            if self.killer.kill_now.value:
                if input("Terminate [y/n]? ") == "y":
                    break
                self.killer.kill_now.value = 0

            iters += 1
            time.sleep(0.005)

    def create_cam(self):
        cam = Camera(f"cam{rand_num(5)}", self.scene, rand_color())
        cam.connect(self.broker, self.port)
        return cam

    def move_cam(self):
        try:
            cam = self.create_cam()
        except:
            self.dropped_queue.put(1)
            return

        start_t = time_ms()
        while True:
            if self.killer.kill_now.value:
                break

            now = time_ms()
            if int(now - start_t) % 100 == 0: # 10 Hz
                cam.move()
                if cam.get_avg_lat() is not None and cam.get_avg_bpms() is not None:
                    self.lat_queue.put(cam.get_avg_lat())
                    self.bpms_queue.put(cam.get_avg_bpms())

            if int(now - start_t) > self.timeout:
                break

            time.sleep(0.005)

        cam.disconnect()

    def get_avg_lats(self):
        return self.avg_lats[-100:]

    def get_avg_bpms(self):
        return self.avg_bpms

    def get_dropped_cams(self):
        return self.dropped

    def get_cpu(self):
        return self.cpu

    def get_mem(self):
        return self.mem

    def save(self):
        np.save(f"data/time_vs_lat_{self.name}", np.array([self.times, self.avg_lats]))

def main(num_cams, timeout, broker, port, name):
    np.set_printoptions(precision=2)
    test = Benchmark(name, num_cams, timeout*60000, broker, port, "benchmark_"+rand_str(5))
    test.run()
    test.save()
    print(f"{np.mean(test.get_avg_lats()) if test.get_avg_lats() else -1} ms | {np.mean(test.get_avg_bpms()) if test.get_avg_lats() else -1} bytes/ms | {test.get_dropped_cams()} dropped | {test.get_cpu()}% cpu usage | {test.get_mem()}% mem usage")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=("ARENA MQTT broker benchmarking"))

    parser.add_argument("-c", "--num_cams", type=int, help="Number of clients to spawn",
                        default=1)
    parser.add_argument("-b", "--broker", type=str, help="Broker to connect to",
                        default="oz.andrew.cmu.edu")
    parser.add_argument("-p", "--port", type=int, help="Port to connect to",
                        default=9001)
    parser.add_argument("-n", "--name", type=str, help="Optional name for saved plot",
                        default="benchmark")
    parser.add_argument("-t", "--timeout", type=int, help="Amount of mins to wait before ending data collection",
                        default=3) # default is 3 mins

    args = parser.parse_args()

    main(**vars(args))
