import paho.mqtt.client as paho
import time

broker = "127.0.0.1"
port = 9001

sub_topic = "demo_topic_js"

def on_subscribe(client, userdata, mid, granted_qos):
    print "subscribed with qos", granted_qos, "\n"

def on_message(client, userdata, message):
    print "message received", str(message.payload.decode("utf-8"))

def on_publish(client,userdata,mid):
    print "data published mid =", mid, "\n"

def on_disconnect(client, userdata, rc):
    print "client disconnected ok"

client1 = paho.Client("client_py1", transport="websockets")
client2 = paho.Client("client_py2", transport="websockets")

client1.on_subscribe = on_subscribe
client1.on_publish = on_publish
client1.on_message = on_message
client1.on_disconnect = on_disconnect

client2.on_subscribe = on_subscribe
client2.on_publish = on_publish
client2.on_message = on_message
client2.on_disconnect = on_disconnect

print "connecting to broker", broker, "on port", port
client1.connect(broker,port)
client1.loop_start()

time.sleep(1)

client2.connect(broker,port)
client2.loop_start()

client1.subscribe(sub_topic)
time.sleep(1)

client1.publish("test/publish/py","on")
time.sleep(1)

client1.subscribe("hi/test")
time.sleep(1)

client1.subscribe("$SYS/hello")
time.sleep(1)

client1.disconnect()
client2.disconnect()

