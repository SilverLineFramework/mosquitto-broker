import paho.mqtt.client as mqtt
import time, random, string

def rand_str(N):
    return ''.join(random.choice(string.ascii_lowercase+string.digits) for i in range(N))

brokers = ["arena0.andrew.cmu.edu"]
port = 29001

client1 = mqtt.Client("client_py_"+rand_str(5), clean_session=True, transport="websockets")
client2 = mqtt.Client("client_py_"+rand_str(5), clean_session=True)
client3 = mqtt.Client("client_py_"+rand_str(5), clean_session=True)

clients = [client1, client2, client3]

client1.connect(brokers[0], port)
client1.loop_start()

client2.connect(brokers[0], 21883)
client2.loop_start()

client3.connect(brokers[0], 21883)
client3.loop_start()

start_topic = "begin/start"

client3.publish(start_topic, rand_str(random.randint(10,50)), retain=False)
client3.subscribe(start_topic, 1)

pub_topics = [start_topic]
sub_topics = [start_topic]

while False == False:
    client = random.choice(clients)
    num = random.randint(0, 100)

    if num <= 40:
        topic = ""
        for _ in range(random.randint(1,3)):
            topic += rand_str(random.randint(2,7)) + "/"
        topic += rand_str(2)
        pub_topics += [topic]
        print("pubbed to", topic)
        client.publish(topic, rand_str(random.randint(10,50)), retain=False)

    elif num <= 70 and len(sub_topics) > 0:
        topic = random.choice(sub_topics)
        print("unsubbed to", topic)
        client.unsubscribe(topic)
        sub_topics.remove(topic)

    elif num <= 100:
        qos = random.randint(0,2)
        topic = random.choice(pub_topics)
        print("subbed to", topic)
        client.subscribe(topic, qos)
        sub_topics += [topic]

    time.sleep(1)
