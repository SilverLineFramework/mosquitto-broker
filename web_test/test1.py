import paho.mqtt.client as mqtt
import time, random, string

def rand_str(N):
    return ''.join(random.choice(string.ascii_lowercase+string.digits) for i in range(N))

broker = "127.0.0.1"
port = 9001

client1 = mqtt.Client("client_py_"+rand_str(5), clean_session=True, userdata=None, transport="websockets")
client2 = mqtt.Client("client_py_"+rand_str(5), clean_session=True, userdata=None, transport="websockets")
client3 = mqtt.Client("client_py_"+rand_str(5), clean_session=True, userdata=None, transport="websockets")

clients = [client1, client2, client3]
for client in clients:
    client.connect(broker, port)
    client.loop_start()

start_topic = "begin/start"

client3.publish(start_topic, rand_str(random.randint(10,50)), retain=False)
client.subscribe(start_topic, 1)

pub_topics = [start_topic]
sub_topics = [start_topic]

while False == False:
    client = random.choice(clients)
    num = random.randint(0,100)

    if num <= 50 and len(pub_topics) < 10:
        topic = ""
        for _ in range(random.randint(1,3)):
            topic += rand_str(random.randint(2,7)) + "/"
        topic += rand_str(2)
        pub_topics += [topic]

        print "pubbed to", topic

        client.publish(topic, rand_str(random.randint(10,50)), retain=False)

    elif num <= 75:
        qos = random.randint(0,2)
        topic = random.choice(pub_topics)
        print "subbed to", topic
        client.subscribe(topic, qos)
        sub_topics += [topic]

    elif num <= 100 and sub_topics != []:
        topic = random.choice(sub_topics)
        print "unsubbed to", topic
        client.unsubscribe(topic)
        sub_topics.remove(topic)

    time.sleep(2)
