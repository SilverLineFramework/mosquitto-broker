import paho.mqtt.client as mqtt
import time

brokers = ["127.0.0.1", "192.168.0.32"]
port = 9001

client1 = mqtt.Client("client_py1", clean_session=True, userdata=None, transport="websockets")
client2 = mqtt.Client("client_py2", clean_session=True, userdata=None, transport="websockets")

client1.connect(brokers[0], port)
client1.loop_start()

client2.connect(brokers[1], port)
client2.loop_start()

time.sleep(1)

print "pub"
client1.publish("simple/test/py", "test")
time.sleep(1)

print "sub"
client1.subscribe("simple/#", 1)
time.sleep(1)

print "pub"
client2.publish("simple/test/py1", "test1")
time.sleep(1)

print "sub"
client2.subscribe("simple/test/#", 1)
time.sleep(1)

client1.disconnect()
client1.loop_stop()
