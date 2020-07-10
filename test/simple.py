import paho.mqtt.client as mqtt
import time

broker = "127.0.0.1"
port = 9001

client1 = mqtt.Client("client_py1", clean_session=True, userdata=None, transport="websockets")

client1.connect(broker, port)
client1.loop_start()

time.sleep(1)

print "pub"
client1.publish("simple/test/py", "test", retain=False)
time.sleep(1)

print "sub"
client1.subscribe("simple/+/py", 1)
time.sleep(1)

client1.disconnect()
client1.loop_stop()

