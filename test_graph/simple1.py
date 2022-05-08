import paho.mqtt.client as mqtt
import time

brokers = ["arena0.andrew.cmu.edu"]
port = 29001

client1 = mqtt.Client("client_py1", clean_session=True, userdata=None, transport="websockets")

client1.connect(brokers[0], port)
client1.loop_start()

time.sleep(1)


print("pub")
client1.publish("test/publish/py", "string of characters")
time.sleep(1)

print("sub")
client1.subscribe("hi/test1", 1)
time.sleep(1)

print("sub")
client1.subscribe("test/+/py")
time.sleep(1)

print("unsub")
client1.unsubscribe("test/publish/py")
time.sleep(1)

print("pub")
client1.publish("test1/publish/js", "on", retain=False)
time.sleep(1)

print("sub")
client1.subscribe("test1/+/js")
time.sleep(1)

print("pub")
client1.publish("test1/publish/js", "on", retain=False)
time.sleep(1)

print("sub")
time.sleep(1)

print("unsub")
client1.unsubscribe("+/+/py")
time.sleep(1)


client1.disconnect()
client1.loop_stop()
