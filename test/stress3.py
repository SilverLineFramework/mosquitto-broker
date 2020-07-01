import paho.mqtt.client as mqtt
import time, random, string

def rand_str(N):
    return ''.join(random.choice(string.ascii_lowercase+string.digits) for i in range(N))

broker = "localhost"
port = 9001

client = mqtt.Client("client_py_"+rand_str(5), clean_session=True, transport="websockets")
client.connect(broker, port)
client.loop_start()

topics = []

while False == False:
    topic = ""
    for _ in range(random.randint(1,3)):
        topic += rand_str(random.randint(2,7)) + "/"
    topic += rand_str(2)
    topics += [topic]
    for t in topics:
        client.publish(t, rand_str(random.randint(100,1000)), retain=False)

    time.sleep(1)
