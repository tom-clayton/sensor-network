
/*
 * Esp8266 MQTT temperature, humidity and dewpoint sensor. Tom Clayton 2019.
 * 
 * Operating modes:
 * 
 *   Demand:    
 *     Set by poll period = 0.
 *     Sensor only takes readings when externally polled.
 *     No sleep, stamped or wait modes.
 *              
 *   Scheduled: 
 *     Set by poll period > 0.
 * 
 *     Sensor takes and sends a reading every poll period.
 *     Can be reset to immediately take a reading for syncing of
 *     sensors.
 * 
 *     Sleep mode turns off Wifi between readings.
 *     Stamped mode will ID every reading and resend until acknowledged.
 *     Wait mode will not send anything until reset.
 *             
 *     Note: can't reset during sleep mode. (excluding first reset in
 *     wait mode.)
 *    
 * Commands:              
 *   
 *  (P)oll:
 *     Sensor will send a reading.
 *     Sensor will not sleep.
 *     Re-poll if nothing received.
 *  
 *  (R)eset:
 *     Sensor will send a reading then revert to timed readings.
 * 
 *  [ID number]:
 *     Sensor will stop sending reading with given ID number.
 *            
 * Config json file settings:
 *     SSID - Wifi name (string)
 *     password - Wifi password (string)
 *     mqtt server - server address (string)
 *     name - unique name for sensor (string)
 *     poll period - time between readings (int: seconds)
 *     sleep mode - sleep between readings (int: 1, 0) 
 *     stamped mode - ID readings and retry until acknowledged (int: 1, 0)
 *     wait mode - wait for reset before starting readings (int: 1, 0)
 *     retry period - time before retrying if in stamped mode (int: seconds)
 *                   
 * Topic Naming (name as given in config file):
 *     name/input                 
 *     name/output
 * 
 * Led signalling:
 *     Fast flashing:   Connecting to Wifi.
 *     Slow flashing:   Connecting to MQTT.
 *     Double flash:    Sending data.
 * 
 * OTA:
 *     Over the air re-programming is currently unavailable.
 */

#include <ESP8266WiFi.h>
//#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <FS.h>
#include <LittleFS.h>
#include <Sodaq_SHT2x.h>  
#include <Wire.h>         
#include <ArduinoJson.h>

#define LED           2
#define N_SETTINGS    9
#define MQTT_PORT     1883

// buffers:
char ssid[16];
char password[16];
char mqtt_server[32];
char name[16];
char in_topic[32];
char out_topic[32];
char out_message[32];
char reading[32];

// settings:
unsigned long poll_period;
unsigned long retry_period;
bool demand_mode;
bool sleep_mode;
bool stamped_mode;
bool wait_mode;

// status:
bool taking_readings;
bool data_ack;
int message_id;

// timers:
unsigned long poll_timer;
unsigned long retry_timer; 

// singletons:
WiFiClient espClient;
PubSubClient client(espClient);

/* 
 *        on_ota_start
 *    
 *    Initialises over the air programming capabitities. 
 */ 
/*
void on_ota_start() {
    if (ArduinoOTA.getCommand() != U_FLASH){
      SPIFFS.end();
    }
    
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
      SPIFFS.end();
    }
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
}
*/
/*
 *      load_config
 *   
 *   Loads settings from config file.
 *   
 */
int load_config() {

  File file;
  const size_t capacity = JSON_OBJECT_SIZE(N_SETTINGS) + 150;
  DynamicJsonDocument doc(capacity);
  
  Serial.println("\nMounting filesystem...");

  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting filesystem");
    return 0;
  }
  
  file = LittleFS.open("/config.json", "r");
  if(!file){
    Serial.println("Failed to open file for reading");
    return 0;
  }

  /*
  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return 0;
  }

  file = SPIFFS.open("/config.json", "r");
  if(!file){
    Serial.println("Failed to open file for reading");
    return 0;
  }*/

  deserializeJson(doc, file);
  
  strcpy(ssid, doc["ssid"]);  
  strcpy(password, doc["password"]); 
  strcpy(mqtt_server, doc["mqtt server"]);
  strcpy(name, doc["name"]);
  sprintf(in_topic, "%s/input", name);
  sprintf(out_topic, "%s/output", name);

  unsigned long poll_period_sec = doc["poll period"];
  
  if (poll_period_sec){
    poll_period = poll_period_sec * 1000;
    demand_mode = false;
    sleep_mode = doc["sleep mode"];
    stamped_mode = doc["stamped mode"];
    wait_mode = doc["wait mode"];
        
    if (stamped_mode){
      unsigned long retry_period_sec = doc["retry period"];
      retry_period = retry_period_sec * 1000; 
    }
  } else {
    demand_mode = true;
    sleep_mode = false;
    stamped_mode = false;
    wait_mode = false;
  }

  file.close();
  Serial.println("Config loaded.");
  Serial.println(poll_period);
  return 1;
}

/*
 *        flash_led
 *    
 *    flashes LED once.    
 *    
 */
void flash_led(){
    digitalWrite(LED, LOW);
    delay(500);
    digitalWrite(LED, HIGH);
}

/*
 * 
 *      connect_to_wifi
 *      
 *    Connects to the wireless newtork given in settings.
 *    Flashed LED constantly when trying to connect.
 *    
 */
void connect_to_wifi() 
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    flash_led();
    delay(500);
    Serial.print(".");
  }
  Serial.println("Wifi connected");
}

/*
 *      connect_to_mqtt
 * 
 * 
 *   Connects to the mqtt newtork given in settings.
 *   Flashes LED once every 5 seconds when trying to connect.
 *   
 */
void connect_to_mqtt() {
  Serial.print("Attempting MQTT connection...");
  
  while (!client.connected()){
    // Attempt to connect
    if (client.connect(name)) {
      Serial.println("connected");
      client.subscribe(in_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      //ArduinoOTA.handle();
      flash_led();
      delay(5000);
    }
  }
  Serial.println("MQTT connected");
}



/*
 * 
 *        acquire_data
 *        
 *    acquires data from SHT21 sensor.
 *    Returns data structure.
 *    
 */
void acquire_data() 
{
  sprintf (
    reading, 
    "%d, %d",
    (int)SHT2x.GetTemperature(),
    (int)SHT2x.GetHumidity()
  );
}

/*
 *        sleep
 *        
 *    turn off Wifi.
 */
void sleep()
{
    WiFi.mode(WIFI_OFF);
    Serial.println("Wifi Disconnected");
    digitalWrite(LED, LOW);
}

/*
 *        publish
 *
 *      send message in buffer.
 */
void publish()
{
  client.publish(out_topic, out_message);
  Serial.print("Message sent [");
  Serial.print(out_topic);
  Serial.print("]: ");
  Serial.println(out_message);
  flash_led();
  delay(500);
  flash_led();
  
  if (stamped_mode) {
    data_ack = false;
    retry_timer = millis();
  } else if (sleep_mode) {
    sleep();
  }
}

/*
 *        send reading
 *        
 *    aquire data.
 *    copy to buffer with stamp if required.
 *    publish message.   
 *    
 */
void send_reading()
{
  acquire_data(); 
  if (stamped_mode){
    sprintf(
      out_message,
      "%d: %s",
      message_id++,
      reading
    );
  } else {
    strcpy(out_message, reading);
  }
  publish();
  poll_timer = millis();
}

/*
 *        callback
 *        
 *    Receives and parses incomming mqtt messages.
 *    
 */
void callback(char* topic, byte* payload, unsigned int length) 
{
  Serial.print("Message received [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println((char *)payload);
  
  switch (payload[0]){
    case 'P': case 'p':{
      send_reading();
      break;
    }
    case 'R': case 'r':{
      send_reading();
      taking_readings = true;
      break;
    }
    default:{
      if (atoi((char *)payload) == message_id){
        data_ack = true;
        if (sleep_mode) {
          sleep();
        }
      }
      break;
    }
  }
}

/*
 *        setup
 *        
 *    Initialisations.
 *    
 */
void setup() 
{
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
  Wire.begin();
  
  // read config:
  if (!load_config()){
    Serial.println("Config file error. Exiting");
    while(1){
      delay(1000);
    }
  }
  Serial.println(""); 
  Serial.print("Sensor: ");
  Serial.println(name);
  
  // setup wifi:
  connect_to_wifi();
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // setup ota:
  //ArduinoOTA.onStart(on_ota_start);
  //ArduinoOTA.begin();

  // setup mqtt:
  client.setServer(mqtt_server, MQTT_PORT);
  client.setCallback(callback);
  connect_to_mqtt();
  Serial.print("Connected to ");
  Serial.println(mqtt_server);
  

  
  // initial status:
  Serial.print("demand mode: ");
  Serial.println(demand_mode ? "on" : "off");
  Serial.print("stamped mode: ");
  Serial.println(stamped_mode ? "on" : "off");
  Serial.print("sleep mode: ");
  Serial.println(sleep_mode ? "on" : "off");
  Serial.print("wait mode: ");
  Serial.println(wait_mode ? "on" : "off");
  taking_readings = !wait_mode;
  data_ack = true;
  message_id = 0;
  if (taking_readings){
    send_reading();
  }
}

/*
 *        loop
 *        
 *    Main loop.
 * 
 *    Checks if readings need to be sent.
 *    
 */
void loop() 
{  
  // send data every poll_period mS if nessesary:
  if (taking_readings && (unsigned long)(millis() - poll_timer) >= poll_period){
    // connect to wifi if sleeping:
    if (WiFi.status() == WL_DISCONNECTED){
      connect_to_wifi();
    }
    // connect to mqtt if disconnected:
    if (!client.connected()) {
      connect_to_mqtt(); 
    }
    send_reading();
  }

  // re-send data until acknowledgement:
  else if (!data_ack && ((unsigned long)(millis() - retry_timer) >= retry_period)){
    publish();
  }

  client.loop();
  //ArduinoOTA.handle();
  delay(100);
}
