import paho.mqtt.client as mqtt
import time, random, string

def rand_str(N):
    return ''.join(random.choice(string.ascii_lowercase+string.digits) for i in range(N))

broker = "127.0.0.1"
port = 9001

clients = []
start_topic = "initial/topic"

pub_topics = [start_topic]
sub_topics = [start_topic]
shared_topics = ["topic/shared1", "topic/shared2", "topic/shared3", "topic/shared4", "topic/shared5"]

while False == False:
    num = random.randint(0, 100)

    if len(clients) < 5: # connect
        new_client = mqtt.Client("client_py_"+rand_str(5), clean_session=True, transport="websockets")
        print "added new client"
        new_client.connect(broker, port)
        new_client.loop_start()
        clients += [new_client]

    elif len(clients) >= 10 and num <= 5: # disconnect
        client = random.choice(clients)
        print "disconnected client"
        client.disconnect()
        client.loop_stop()
        clients.remove(client)

    elif len(clients) > 0 and num <= 40: # pub
        client = random.choice(clients)
        topic = ""
        for _ in range(random.randint(1,3)):
            topic += rand_str(random.randint(2,7)) + "/"
        topic += rand_str(2)
        pub_to = random.choice([random.choice(shared_topics), topic])
        pub_topics += [pub_to]
        print "pubbed to", pub_to
        client.publish(pub_to, rand_str(random.randint(10,100)), retain=False)

    elif len(clients) > 0 and num <= 70: # sub
        client = random.choice(clients)
        qos = random.randint(0,2)
        topic = random.choice(pub_topics)
        print "subbed to", topic
        client.subscribe(topic, qos)
        sub_topics += [topic]

    elif len(clients) > 0 and len(sub_topics) > 0 and num <= 100: # unsub
        client = random.choice(clients)
        topic = random.choice(random.choice([shared_topics, sub_topics]))
        print "unsubbed to", topic
        client.unsubscribe(topic)
        if topic not in shared_topics:
            sub_topics.remove(topic)

    time.sleep(1)
