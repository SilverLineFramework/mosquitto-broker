import psutil
import time, json, signal, sys
import paho.mqtt.client as mqtt
from utils import *

class GracefulKiller:
    def __init__(self):
        self.kill_now = False
        signal.signal(signal.SIGINT, self.exit_gracefully)
        signal.signal(signal.SIGTERM, self.exit_gracefully)

    def exit_gracefully(self, signum, frame):
        self.kill_now = True

def main():
    client = mqtt.Client("cpu_mem_log", clean_session=True)
    client.connect("oz.andrew.cmu.edu", 1883)
    client.loop_start()

    mosquitto_ps = None
    msg = {"cpu": 0.0, "mem": 0.0}
    prev_msg = msg.copy()
    killer = GracefulKiller()

    ps = psutil.process_iter()
    for p in ps:
        if p.status() != psutil.STATUS_ZOMBIE and p.name() == "mosquitto":
            mosquitto_ps = p
            break

    if mosquitto_ps is None:
        print("ERROR: mosquitto not running!")
        return

    start_t = time_ms()
    while True:
        now = time_ms()
        if int(now - start_t) % 100 == 0: # 10 Hz
            msg["cpu"] = mosquitto_ps.cpu_percent(interval=0.1)
            msg["mem"] = mosquitto_ps.memory_percent()
            if msg != prev_msg:
                print(msg)
            client.publish("cpu_mem", payload=json.dumps(msg))
            prev_msg = msg.copy()
            time.sleep(0.005)

        if killer.kill_now:
            if input("Terminate [y/n]? ") == "y":
                break
            killer.kill_now = False

        time.sleep(0.005)

    client.loop_stop()
    client.disconnect()

if __name__ == "__main__":
    main()
