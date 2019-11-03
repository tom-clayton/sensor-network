
import paho.mqtt.client as mqtt 
import datetime, time

broker_address = "broker.mqttdashboard.com"
poll_timeout = 10
userdata = {'status': 'idle'}

# add connect callback, subscribe here

def on_message(client, userdata, message):
    print ("rcvd")
    dt = datetime.datetime.now().replace(microsecond=0).isoformat(" ")
    m = str(message.payload.decode("utf-8"))
    print(m)
    if m == "ack":
        userdata['status'] = 'idle'
    else:
        with open("results", "a") as fo:
            fo.write(dt + ", " + m + "\n")
        userdata['status'] = 'received'
    

client = mqtt.Client("logger")
client.connect(broker_address)
client.subscribe("location1/output")
client.on_message = on_message(userdata)
client.loop_start()

prev_hour = datetime.datetime.now().hour

while True:
    hour = datetime.datetime.now().hour
    if hour != prev_hour:
        # on the hour, take a reading:
        client.publish("location1/input", "poll")
        userdata['status'] = "polled"
        timer = time.time()
    if userdata['status'] == "polled" and (time.time() - timer) > poll_timeout:
        # poll timed out, try again:
        client.publish("location1/input", "poll")
        timer = time.time()
        # data received, send acknowledgement:
    if userdata['status'] == "recieved":
        client.publish("location1/input", "ack")

client.loop_stop()

