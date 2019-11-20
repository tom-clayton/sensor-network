import paho.mqtt.client as mqtt
import datetime, time

broker_address = "localhost"
log_period = 3600 # in seconds
absent_sensor_timeout = 120 # in seconds

sensor_locations = ["frontroom"] 
received = []

class Sensor:
    def __init__(self, location, client, index):
        self.location = location
        self.client = client
        self.last_message_id = None
        self.last_message = None
        self.index = index
        
    def subscribe(self):
        self.client.suscribe(self.location + "/output")
    
    def received(self, message):
        message_id, payload = message.split(':')
        if self.last_message_id == message_id:
            return
        self.last_message = payload.strip()
        self.last_message_id = message_id
        log(self)

    def publish(self, message):
        client.publish(self.location + "/input", 
                       payload=message, 
                       qos=0, 
                       retain=False)

    def acknowledge(self):
        self.publish("ack")
	
    def reset(self):
        self.publish("reset")

    def __str__(self):
        return self.location
    
def log(sensor):
    global received
    if sensor not in recieved:
        received.append(sensor)
    print (f"data received: {sensor}, {sensor.last_message}")
    if len(received) == len(sensor_locations):
        print ("all data received, writing to disk")
        received.sort(key=lambda x: x.index)
        with open("results", "a") as fo:
            fo.write(datetime.datetime.now()\
                     .replace(microsecond=0).isoformat(" "))
            for rcvd in received:
                fo.write(", " + rcvd[1].decode())
            fo.write("\n")
        received = []

def on_connect(client, sensors, flags, rc):
    print("Connected")
    for sensor in sensors:
        sensor.subscribe()

def on_message(client, sensors, msg):
    for sensor in sensors:
        if msg.topic.split('/')[0] == sensor.location:
            sensor.receved(msg.payload)
            sensor.acknowledge()

if __name__ == '__main__':
    client = mqtt.Client()
    sensors = [Sensor(loc, client, index) \
               for index, loc in enumerate(sensor_locations)]
    print(sensors)
    client.on_connect = on_connect(sensors)
    client.on_message = on_message(sensors)

    client.connect(broker_address, 1883, 60)

    # syncronise sensors:
    for sensor in sensors:
        sensor.reset()
        
    # Blocking call that processes network traffic, dispatches callbacks and
    # handles reconnecting.
    # Other loop*() functions are available that give a threaded interface and a
    # manual interface.

    # this ^ and data arrival checking
    
    client.loop_forever()

