# sensor-network
esp8266 sensor network

sensor instructions:

  
Modes:
  
 *   Demand:    
         Set by poll period = 0.
         Sensor only takes readings when externally polled.
         Sensor can be requested to sleep (turn off wifi) for a period after sending data.
               
 *   Scheduled: 
         Set by poll period > 0.
         On boot, sensor will send it's location in a message to topic 'sensor_register' 
         until acknowledged with 'ack_reg'.
         Sensor will then wait for a 'reset' command before sending readings.
         Sensor will send readings every set period to 'stamped' topic.              
         Data will be stamped with an ID number.
         Sensor will re-send data with same ID until acknowledged with 'ack_data'.
         Sensor will sleep between readings if sleep mode is set.
         Sensor will wait for a set period before sleeping to allow reseting or 
         firmware updates. <----TODO
                 
 Commands(send to 'input' topic):              
       
 *   POLL:
         Data will be sent on 'unstamped' topic.
         Sensor will not wait for acknowledgement.
         Sensor will not sleep.
                 
 *   Px(where x is an integer):
         Demand mode only.
         Data will be sent on 'stamped' topic.
         Data will be stamped with an ID number.
         Sensor will re-send data with same ID until acknowledged.
         Sensor will sleep for x seconds after acknowledgement.
                 
 *    RESET:
         Scheduled mode only.
         Sensor will send a reading then revert to scheduled readings.
                               
                  
 All topics are pre-pended with the sensor's location. i.e location/input
  
 
 Led signalling:
     Constant flashing:  Connecting to Wifi.
     Single Flash:       Connecting to MQTT.
     Double Flash:       Sending data.
 
