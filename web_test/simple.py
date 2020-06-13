import paho.mqtt.client as mqtt
import time

brokers = ["127.0.0.1", "192.168.0.32"]
port = 9001

client1 = mqtt.Client("client_py1", clean_session=True, userdata=None, transport="websockets")
client2 = mqtt.Client("client_py2", clean_session=True, userdata=None)

client1.connect(brokers[0], port)
client1.loop_start()

time.sleep(1)

client2.connect(brokers[1], 1883)
client2.loop_start()

client2.publish("hi/test1", "hi")
time.sleep(1)

client1.subscribe("demo_topic_js", 1)
time.sleep(1)

client1.publish("test/publish/py", "string of characters")
time.sleep(1)

client1.subscribe("hi/test1", 1)
time.sleep(1)

client2.subscribe("hi/test2")
time.sleep(1)

client1.subscribe("$SYS/graph")
time.sleep(1)

client2.subscribe("test/publish/py")
time.sleep(1)

client1.publish("test1/publish/js", "on", retain=False)
time.sleep(1)

client2.subscribe("test1/#")
time.sleep(1)

client1.unsubscribe("hi/test1")
time.sleep(1)

client2.disconnect()
client2.loop_stop()

time.sleep(1)
client1.disconnect()
client1.loop_stop()
