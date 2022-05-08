import paho.mqtt.client as mqtt
import time, random, string

def rand_str(N):
    return ''.join(random.choice(string.ascii_lowercase+string.digits) for i in range(N))

brokers = ["arena0.andrew.cmu.edu"]
port = 29001

clients = []

start_topic = "initial/topic"

pub_topics = [start_topic]
sub_topics = [start_topic]

while False == False:
    num = random.randint(0, 100)

    if len(clients) < 3 or num <= 10: # connect
        new_client = mqtt.Client("client_py_"+rand_str(5), clean_session=True, transport="websockets")
        print("added new client")
        new_client.connect(random.choice(brokers), port)
        new_client.loop_start()
        clients += [new_client]
        time.sleep(1)

    if len(clients) > 3 and num <= 20: # disconnect
        client = random.choice(clients)
        print("disconnected client")
        client.disconnect()
        client.loop_stop()
        clients.remove(client)
        time.sleep(1)

    if num <= 50: # pub
        client = random.choice(clients)
        topic = ""
        for _ in range(random.randint(1,3)):
            topic += rand_str(random.randint(2,7)) + "/"
        topic += rand_str(2)
        pub_topics += [topic]
        print("pubbed to", topic)
        client.publish(topic, rand_str(random.randint(10,100)), retain=False)
        time.sleep(1)

    if num <= 80: # sub
        for _ in range(5):
            client = random.choice(clients)
            qos = random.randint(0,2)
            topic = random.choice(pub_topics)
            print("subbed to", topic)
            client.subscribe(topic, qos)
            sub_topics += [topic]
            time.sleep(1)

    if num <= 100: # unsub
        for _ in range(3):
            if len(sub_topics) > 0:
                client = random.choice(clients)
                topic = random.choice(sub_topics)
                print("unsubbed to", topic)
                client.unsubscribe(topic)
                sub_topics.remove(topic)
                time.sleep(1)
