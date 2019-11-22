#!/usr/bin/env python3

import paho.mqtt.client as mqtt
import datetime
import time
import queue

#broker_address = "localhost"
broker_address = "test.mosquitto.org"
sensor_timeout = 60 # in seconds
max_retries = 3

sensor_locations = ("location1",) 
data_queue = queue.Queue(10)

class Sensor:
    def __init__(self, location, client, index):
        self.location = location
        self.client = client
        self.last_message_id = None
        self.last_message = None
        self.index = index
        self.retries = 0

    def subscribe(self):
        self.client.subscribe(f"{self.location}/output")
        print(f"Subscribing: {self.location}/output")    

    def receive(self, message):
        message_id, payload = message.split(':')
        if self.last_message_id == message_id:
            return
        self.last_message = payload.strip()
        self.last_message_id = message_id
        data_queue.put(self)
        self.retries = 0

    def publish(self, message):
        client.publish(f"{self.location}/input", 
                       payload=message, 
                       qos=0, 
                       retain=False)

    def acknowledge(self):
        self.publish("ack")
	
    def reset(self):
        if self.retries < max_retries:
            self.publish("reset")
            self.retries += 1
        else:
            self.last_message = ",,"
            self.retries = 0
            data_queue.put(self)
            # log sensor failure

    def __str__(self):
        return self.location
    

def write_to_file(sensors):
    print ("All sensor data received, writing to disk")
    sensors.sort(key=lambda x: x.index)
    with open("results", "a") as fo:
        fo.write(datetime.datetime.now()\
                     .replace(microsecond=0).isoformat(" "))
        for sensor in sensors:
            fo.write(f", {sensor.last_message}")
        fo.write("\n")

def on_connect(client, sensors, flags, rc):
    print("Connected")
    for sensor in sensors:
        sensor.client = client
        sensor.subscribe()

def on_message(client, sensors, msg):
    print (f"message received {msg.topic} {msg.payload}")
    for sensor in sensors:
        if msg.topic.split('/')[0] == sensor.location:
            #print (f"sensor: {sensor}")
            sensor.receive(msg.payload.decode())
            sensor.acknowledge()

if __name__ == '__main__':
    client = mqtt.Client()
    sensors = [Sensor(loc, client, index) \
               for index, loc in enumerate(sensor_locations)]
    client.on_connect = on_connect
    client.on_message = on_message
    client.user_data_set(sensors)
    client.connect(broker_address, 1883, 60)

    # syncronise sensors:
    for sensor in sensors:
        sensor.reset()
        
    data_received = []

    while True:
        try:
            sensor = data_queue.get(block=False)
            if sensor not in data_received:
                data_received.append(sensor)
            
            if len(data_received) == len(sensor_locations):
                write_to_file(data_received)
                data_received = []
            else:
                # start/re-start the timeout timer
                timeout_timer = time.time()

        except queue.Empty:
            if data_received and \
            (time.time() - timeout_timer) >= sensor_timeout:
                absent_sensors = [s for s in sensors if s not in data_received]
                for sensor in absent_sensors:
                    sensor.reset()

        client.loop()

