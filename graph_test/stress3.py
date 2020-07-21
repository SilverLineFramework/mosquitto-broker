import paho.mqtt.client as mqtt
import time, random, string

def rand_str(N):
    return ''.join(random.choice(string.ascii_lowercase+string.digits) for i in range(N))

brokers = ["localhost", "127.0.0.1", "192.168.0.32", "Edwards-MacBook-Air.local"]
port = 9001

client = mqtt.Client("client_py_"+rand_str(5), clean_session=True, transport="websockets")
client.connect(random.choice(brokers), port)
client.loop_start()

topics = []

while False == False:
    topic = ""
    for _ in range(random.randint(1,3)):
        topic += rand_str(random.randint(2,7)) + "/"
    topic += rand_str(2)
    topics += [topic]
    for t in topics:
        client.publish(t, rand_str(random.randint(100,1000)), retain=False, qos=2)
        client.subscribe(t, 2)
        print("added", t)

    time.sleep(1)
