# sensor-network
esp8266 sensor network

Sensor Instructions:

* Operating modes:
 
	Demand:    
		Set by poll period = 0.
		Sensor only takes readings when externally polled.
		No sleep, stamped or wait modes.
        
	Scheduled: 
		Set by poll period > 0.

		Sensor takes and sends a reading every poll period.
		Can be reset to immediately take a reading for syncing of sensors.
 
		Sleep mode turns off Wifi between readings.
		Stamped mode will ID every reading and resend until acknowledged.
		Wait mode will not send anything until reset.
        
		Note: can't reset during sleep mode. (excluding first reset in wait mode.)
  
* Commands:              
	
	(P)oll:
		Sensor will send a reading.
		Sensor will not sleep.
		Re-poll if nothing received.

	(R)eset:
		Sensor will send a reading then revert to timed readings.

	(ID number):
		Sensor will stop sending reading with given ID number.
          
* Config json file settings:

		SSID - Wifi name (string)
		password - Wifi password (string)
		mqtt server - server address (string)
		name - unique name for sensor (string)
		poll period - time between readings (int: seconds)
		sleep mode - sleep between readings (int: 1, 0) 
		stamped mode - ID readings and retry until acknowledged (int: 1, 0)
		wait mode - wait for reset before starting readings (int: 1, 0)
		retry period - time before retrying if in stamped mode (int: seconds)
					
* Topic Naming (name as given in config file):
		name/input                 
		name/output

* Led signalling:
		Fast flashing:   Connecting to Wifi.
		Slow flashing:   Connecting to MQTT.
		Double flash:    Sending data.
 

 
