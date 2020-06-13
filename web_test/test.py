import paho.mqtt.client as mqtt
import time

broker = "127.0.0.1"
port = 9001

def on_subscribe(client, userdata, mid, granted_qos):
    print "subscribed with qos", granted_qos, "\n"

def on_message(client, userdata, message):
    # print "message received", str(message.payload.decode("utf-8"))
    print "message received"

def on_publish(client,userdata,mid):
    print "data published mid =", mid, "\n"

def on_disconnect(client, userdata, rc):
    print "client disconnected ok"

client1 = mqtt.Client("client_py1", clean_session=True, userdata=None, transport="websockets")
client2 = mqtt.Client("client_py2", clean_session=True, userdata=None)

client1.on_subscribe = on_subscribe
client1.on_publish = on_publish
client1.on_message = on_message
client1.on_disconnect = on_disconnect

client2.on_subscribe = on_subscribe
client2.on_publish = on_publish
client2.on_message = on_message
client2.on_disconnect = on_disconnect

print "connecting to broker", broker, "on port", port
client1.connect(broker, port)
client1.loop_start()

time.sleep(1)

print "connecting to broker", broker, "on port", port
client2.connect(broker, 1883)
client2.loop_start()

client2.publish("hi/test1", "hi")
time.sleep(1)

client1.subscribe("demo_topic_js", 1)
time.sleep(1)

client1.publish("test/publish/py", "on", retain=False)
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
