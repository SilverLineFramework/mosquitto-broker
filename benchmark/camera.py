import time, json, random, string
from math import ceil, sin, cos
import paho.mqtt.client as mqtt

def rand_str(N):
    return ''.join(random.choice(string.ascii_lowercase+string.digits) for i in range(N))

def rand_num(N):
    return ''.join(random.choice(string.digits) for i in range(N))

def rand_norm(mu, sig):
    res = random.gauss(mu, sig)
    return round(res, 3)

def euler2quat(x, y, z):
    quat = []
    quat += [sin(x/2)*cos(y/2)*cos(z/2) - cos(x/2)*sin(y/2)*sin(z/2)]
    quat += [cos(x/2)*sin(y/2)*cos(z/2) + sin(x/2)*cos(y/2)*sin(z/2)]
    quat += [cos(x/2)*cos(y/2)*sin(z/2) - sin(x/2)*sin(y/2)*cos(z/2)]
    quat += [cos(x/2)*cos(y/2)*cos(z/2) + sin(x/2)*sin(y/2)*sin(z/2)]
    return quat

class Camera(object):
    def __init__(self, name, scene, color):
        super().__init__()
        self.name = f"camera_{rand_num(4)}_{name}"
        self.scene = scene
        self.pos = [0,3,0]
        self.rot = [0,0,0,0]
        self.color = color
        self.lat_total = 0
        self.lat_cnt = 0
        self.lat = -1
        self.client = mqtt.Client(self.name, clean_session=True, transport="websockets")
        self.client.on_message = self.on_message

    def connect(self, broker, port):
        self.client.connect(broker, port)
        self.client.loop_start()
        self.client.subscribe(f"realm/s/{self.scene}/#")

    def disconnect(self):
        self.client.loop_stop()
        self.client.disconnect()

    def on_message(self, client, userdata, message):
        arena_json = json.loads(message.payload.decode())
        if arena_json["object_id"] == self.name:
            self.lat_total += (time.time() - arena_json["timestamp"]) * 1000 # ms
            self.lat_cnt += 1
            self.lat = self.lat_total / self.lat_cnt

    def move(self):
        self.pos[0] += rand_norm(0, 0.1)
        self.pos[1] += rand_norm(0, 0.05)
        self.pos[2] += rand_norm(0, 0.1)

        quat = euler2quat(rand_norm(0, 6.28), rand_norm(0, 6.28), rand_norm(0, 6.28))
        self.rot = quat

        arena_json = self.create_json()
        self.client.publish(f"realm/s/{self.scene}/{self.name}", arena_json)

    def create_json(self):
        res = {}
        res["object_id"] = self.name
        res["action"] = "create"
        res["type"] = "object"
        res["timestamp"] = time.time()    # 2020-07-21T00:28:03.364Z

        res["data"] = {}
        res["data"]["object_type"] = "camera"
        res["data"]["color"] = self.color

        res["data"]["position"] = {}
        res["data"]["position"]["x"] = self.pos[0]
        res["data"]["position"]["y"] = self.pos[1]
        res["data"]["position"]["z"] = self.pos[2]

        res["data"]["rotation"] = {}
        res["data"]["rotation"]["x"] = self.rot[0]
        res["data"]["rotation"]["y"] = self.rot[1]
        res["data"]["rotation"]["z"] = self.rot[2]
        res["data"]["rotation"]["w"] = self.rot[3]

        return json.dumps(res)
