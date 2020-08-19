import argparse
import numpy as np
import time, random, string, signal, sys, json
from multiprocessing import Process, Value, Queue, Lock, Event
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
        self.timeout = timeout

        self.start_flag = Event()
        self.killer = GracefulKiller()

        self.times = []
        self.avg_lats = []
        self.lat_temp = Value("d", 0.0)
        self.lat_cnt = Value("i", 0)
        self.dropped_clients = 0
        self.packets_sent = 0
        self.packets_recvd = 0
        self.bytes_sent = 0
        self.bytes_recvd = 0
        self.cpu = []
        self.mem = []

        self.lat_lock = Lock()
        self.queues_lock = Lock()
        self.drop_lock = Lock()

        self.dropped_clients_queue = Queue()
        self.packets_sent_queue = Queue()
        self.packets_recvd_queue = Queue()
        self.bytes_sent_queue = Queue()
        self.bytes_recvd_queue = Queue()

        self.client = mqtt.Client("cpu_mem_log_bench", clean_session=True)
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message

    def on_connect(self, client, userdata, flags, rc):
        client.subscribe("cpu_mem")

    def on_message(self, client, userdata, message):
        msg = json.loads(message.payload.decode())
        if "cpu" in msg and "mem" in msg:
            self.cpu += [msg["cpu"]/100]
            self.mem += [msg["mem"]/100]

    def run(self):
        self.client.connect("oz.andrew.cmu.edu", 1883)
        self.client.loop_start()

        ps = [Process(target=self.move_cam, args=()) for _ in range(self.num_cams)]
        for p in ps:
            p.daemon = True
            p.start()

        time.sleep(0.5)

        print(f"Started! Scene is {self.scene}")
        self.start_flag.set()

        start_t = time_ms()
        self.collect()

        time.sleep(0.5)

        self.client.loop_stop()
        self.client.disconnect()

        print("waiting for processes to finish")
        for p in ps: p.join()

        while self.dropped_clients_queue.qsize() > 0:
            self.dropped_clients += self.dropped_clients_queue.get()
        while self.packets_sent_queue.qsize() > 0:
            self.packets_sent += self.packets_sent_queue.get()
        while self.packets_recvd_queue.qsize() > 0:
            self.packets_recvd += self.packets_recvd_queue.get()
        while self.bytes_sent_queue.qsize() > 0:
            self.bytes_sent += self.bytes_sent_queue.get()
        while self.bytes_recvd_queue.qsize() > 0:
            self.bytes_recvd += self.bytes_recvd_queue.get()

        print("done!")

    def collect(self):
        iters = 0
        start_t = time_s()
        prev_t = 0
        while True:
            now_t = time_s()
            if (now_t - prev_t) >= 1.0/10.0: # 10 Hz
                prev_t = now_t

                self.lat_lock.acquire()
                if self.lat_cnt.value > 0:
                    self.avg_lats += [self.lat_temp.value / self.lat_cnt.value]
                    self.times += [now_t - start_t] # secs
                self.lat_lock.release()

                iters += 1
                if iters % 100 == 0:
                    sys.stdout.write(".")
                    sys.stdout.flush()

            if (now_t - start_t) >= self.timeout:
                self.killer.kill_now.value = 1
                print("Timeout reached, exiting...")
                break

            if len(self.avg_lats) > 100 and rmsd(self.avg_lats[-100:]) < 0.00005:
                self.killer.kill_now.value = 1
                print("RMSD threshold crossed, exiting...")
                break

            if self.killer.kill_now.value:
                if input("End Benchmark [y/n]? ") == "y":
                    break
                self.killer.kill_now.value = 0

            time.sleep(0.001)

    def create_cam(self):
        cam = Camera(f"cam{rand_num(5)}", self.scene, rand_color())
        cam.connect(self.broker, self.port)
        return cam

    def move_cam(self):
        try:
            cam = self.create_cam()
        except:
            self.drop_lock.acquire()
            self.dropped_clients_queue.put(1)
            self.drop_lock.release()
            return

        self.start_flag.wait()
        iters = 0

        prev_t = 0
        while True:
            now_t = time_s()
            if (now_t - prev_t) >= 1.0/10.0: # 10 Hz
                prev_t = now_t
                iters += 1

                cam.move()
                if cam.get_avg_lat() is not None:
                    self.lat_lock.acquire()
                    self.lat_temp.value += cam.get_avg_lat()
                    self.lat_cnt.value += 1
                    self.lat_lock.release()

            if self.killer.kill_now.value:
                break

            time.sleep(0.001)

        # print(len(cam.client._out_packet), len(cam.client._in_packet))
        cam.disconnect()
        # print("here")

        self.queues_lock.acquire()
        self.bytes_sent_queue.put(cam.get_bytes_sent())
        self.bytes_recvd_queue.put(cam.get_bytes_recvd())
        self.packets_sent_queue.put(cam.get_packets_sent())
        self.packets_recvd_queue.put(cam.get_packets_recvd())
        self.queues_lock.release()

    def get_avg_lats(self):
        return self.avg_lats[-100:]

    def get_bpms_sent(self):
        return self.bytes_sent / self.timeout

    def get_bpms_recvd(self):
        return self.bytes_recvd / self.timeout

    def get_dropped_clients(self):
        return self.dropped_clients

    def dropped_packets_percent(self):
        return (self.packets_sent - self.packets_recvd) / self.packets_sent

    def get_cpu(self):
        return self.cpu

    def get_mem(self):
        return self.mem

    def save(self):
        np.savez(f"data/client_{self.name}", times=np.array(self.times), avg_lats=np.array(self.avg_lats))

def main(num_cams, timeout, broker, port, name):
    print()
    print(f"----- Running benchmark with {num_cams} clients -----")

    test = Benchmark(f"{name}_c{num_cams}", num_cams, timeout*60, broker, port, "benchmark_"+rand_str(5))
    test.run()

    avg_lats = np.mean(test.get_avg_lats())
    bpms_sent = test.get_bpms_sent()
    bpms_recvd = test.get_bpms_recvd()
    dropped_clients = test.get_dropped_clients()
    dropped_packets_percent = test.dropped_packets_percent()
    cpu = np.mean(test.get_cpu())
    mem = np.mean(test.get_mem())

    print("----- Summary -----")
    print(f"{num_cams} Clients connecting to {broker}:{port} with {timeout} sec timeout:")
    print(f"  {np.mean(avg_lats)} ms response time")
    print(f"  {bpms_sent} bytes/ms sent | {bpms_recvd} bytes/ms received")
    print(f"  {dropped_clients} clients dropped | {dropped_packets_percent*100}% packet loss")
    print(f"  {cpu*100}% cpu usage | {mem*100}% mem usage")

    test.save()

    print("------------------------------------------------------")
    print()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=("ARENA MQTT broker benchmarking"))
    parser.add_argument("-c", "--num_cams", type=int, help="Number of clients to spawn",
                        default=1)
    parser.add_argument("-b", "--broker", type=str, help="Broker to connect to",
                        default="oz.andrew.cmu.edu")
    parser.add_argument("-p", "--port", type=int, help="Port to connect to",
                        default=1883)
    parser.add_argument("-n", "--name", type=str, help="Optional name for saved plot",
                        default="benchmark")
    parser.add_argument("-t", "--timeout", type=float, help="Amount of mins to wait before ending data collection",
                        default=2.0) # default is 2 mins
    args = parser.parse_args()

    main(**vars(args))
