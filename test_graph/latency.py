import paho.mqtt.client as mqtt
import time

broker = "arena0.andrew.cmu.edu"
port = 21883

client1 = mqtt.Client("client_py1", clean_session=True, userdata=None, transport="websockets")

client1.connect(broker, port)
client1.loop_start()

while False == False:
    client1.publish("$NETWORK/latency", None, qos=2)
    time.sleep(10)

client1.disconnect()
client1.loop_stop()
