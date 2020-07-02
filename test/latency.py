import paho.mqtt.client as mqtt
import time

broker = "192.168.0.32"
port = 9001

client1 = mqtt.Client("client_py1", clean_session=True, userdata=None, transport="websockets")

client1.connect(broker, port)
client1.loop_start()

while True:
    print "pub"
    client1.publish("s", None, qos=2)
    time.sleep(1)

client1.disconnect()
client1.loop_stop()
