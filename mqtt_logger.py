#!/usr/bin/env python3

import paho.mqtt.client as mqtt
import time
import queue

#broker_address = "localhost"
broker_address = "broker.mqttdashboard.com"

sensor_locations = ("frontroom", "backroom", "backbedroom", "utilityroom") 

sensor_timeout = 5 * 60
global_sleep_time = 50 * 60

response_queue = queue.Queue(10)

class Sensor:
    def __init__(self, location, client, index):
        self.location = location
        self.client = client
        self.last_message_id = None
        self.last_message = None
        self.index = index
        self.retries = 3

    def subscribe(self):
        self.client.subscribe(f"{self.location}/stamped")
        print(f"Subscribing: {self.location}/stamped")    

    def receive(self, message):
        message_id, payload = message.split(':')
        if self.last_message_id == message_id:
            return
        self.last_message = payload.strip()
        self.last_message_id = message_id
        response_queue.put(self)

    def publish(self, message):
        client.publish(f"{self.location}/input", 
                       payload=message, 
                       qos=0, 
                       retain=False)

    def poll_sleep(self, sleep_time):
        self.publish(f"P{sleep_time}")

    def acknowledge(self):
        self.publish("ack")
	
    def reset(self, reset_id):
        self.publish("r" + str(reset_id))
        print(f"resetting {self}")

    def retry(self):
        sleep_time = global_sleep_time - sensor_timeout * (4 - self.retries)
        self.poll_sleep(sleep_time)
        print(f"{self.location} retry: P{sleep_time}")
        self.retries -= 1

    def no_data(self):
        self.last_message = ", , "
        print(f"No data from {self}")

    def __str__(self):
        return self.location
    

def write_to_file(sensors):
    print ("Writing to disk")
    sensors.sort(key=lambda x: x.index)
    with open("results", "a") as fo:
        fo.write(time.strftime('%Y-%m-%d %H:00',
                               time.localtime(time.time())))
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
            sensor.acknowledge("data")

if __name__ == '__main__':
    client = mqtt.Client()
    sensors = [Sensor(loc, client, index) \
               for index, loc in enumerate(sensor_locations)]
    client.on_connect = on_connect
    client.on_message = on_message
    client.user_data_set(sensors)
    client.connect(broker_address, 1883, 60)
    prev_hour = time.localtime(time.time()).tm_hour   
    sensors_responded = []
    polling = False
    client.loop_start()
    
    while True:
        # Poll sensors on hour:
        hour = time.localtime(time.time()).tm_hour
        if hour != prev_hour:
            sensors_responded = []
            polling = True
            for sensor in sensors:
                sensor.retries = 3
                sensor.poll_sleep(global_sleep_time)
            sensor_timer = time.time()
            prev_hour = hour

        # Check for responses:
        try:
            sensor = response_queue.get(block=False)
            if sensor not in sensors_responded:
                sensors_responded.append(sensor)     
        except queue.Empty:
            pass

        # Check timeout:
        if polling and time.time() - sensor_timer >= sensor_timeout:
            for sensor in (s for s in sensors if s not in sensors_responded):
                if sensor.retries:
                    sensor.retry()
                else:
                    sensor.no_data()
                    sensors_responded.append(sensor)

            sensor_timer = time.time()
                    
         # Check all responses in:
         if polling and len(sensors_responded) == len(sensors):
            write_to_file(sensors_responded)
            polling = False           

    

