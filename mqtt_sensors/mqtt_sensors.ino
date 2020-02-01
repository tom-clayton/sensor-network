
/*
 * Esp8266 MQTT temperature, humidity and dewpoint sensor. Tom Clayton 2019.
 * 
 * Modes:
 * 
 *   Demand:    
 *        Set by poll period = 0.
 *        Sensor only takes readings when externally polled.
 *        Sensor can be requested to sleep (turn off wifi) for a period after sending data.
 *              
 *   Scheduled: 
 *        Set by poll period > 0.
 *        On boot, sensor will send it's location in a message to topic 'sensor_register' 
 *        until acknowledged with 'ack_reg'.
 *        Sensor will then wait for a 'reset' command before sending readings.
 *        Sensor will send readings every set period to 'stamped' topic.              
 *        Data will be stamped with an ID number.
 *        Sensor will re-send data with same ID until acknowledged with 'ack_data'.
 *        Sensor will sleep between readings if sleep mode is set.
 *        Sensor will wait for a set period before sleeping to allow reseting or 
 *        firmware updates. <----TODO
 *                
 * Commands(send to 'input' topic):              
 *      
 *   POLL:
 *        Data will be sent on 'unstamped' topic.
 *        Sensor will not wait for acknowledgement.
 *        Sensor will not sleep.
 *                
 *   Px(where x is an integer):
 *        Demand mode only.
 *        Data will be sent on 'stamped' topic.
 *        Data will be stamped with an ID number.
 *        Sensor will re-send data with same ID until acknowledged.
 *        Sensor will sleep for x seconds after acknowledgement.
 *                
 *    Rx:
 *        Scheduled mode only.
 *        Sensor will send a reading then revert to scheduled readings.
 *                              
 *                 
 * All topics are pre-pended with the sensor's location. i.e location/input
 * 
 * 
 * Led signalling:
 *    Constant flashing:  Connecting to Wifi.
 *    Single Flash:       Connecting to MQTT.
 *    Double Flash:       Sending data.
 */

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <FS.h>
#include <Sodaq_SHT2x.h>
#include <Wire.h>
#include <ArduinoJson.h>

#define LED           2
#define WAKE_PERIOD   120000

struct Data{
  int temperature,
      humidity,
      dew_point;
};

enum MessageType{
  STAMPED,
  RETRY,
  UNSTAMPED,
  REG,
  ACK
};

enum ackType{
  DATA_ACK,
  REG_ACK
};

char ssid[16];
char password[16];
char mqtt_server[32];
char location[16];
unsigned long poll_period;
unsigned long ack_timeout;

unsigned long sleep_period = 0;
bool demand_mode;
bool sleep_mode;
bool taking_readings = false;
bool data_ack = true;
bool reg_ack = false;
bool turn_off_wifi = false;

unsigned long poll_timer;
unsigned long ack_timer; 
unsigned long sleep_timer;
unsigned long wake_timer;

char last_message[32];
int message_id = 0;
int last_reset_id = -1;

WiFiClient espClient;
PubSubClient client(espClient);

/* 
 *        on_ota_start
 *    
 *    Initialises over the air programming capabitities. 
 */ 
void on_ota_start() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
      SPIFFS.end();
    }
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
}

/*
 *      load_config
 *   
 *   Loads settings from config file.
 *   
 */
int load_config() {

  File file;
  const size_t capacity = JSON_OBJECT_SIZE(7) + 150;
  DynamicJsonDocument doc(capacity);

  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return 0;
  }

  file = SPIFFS.open("/config.json", "r");
  if(!file){
    Serial.println("Failed to open file for reading");
    return 0;
  }
  deserializeJson(doc, file);
  
  strcpy(ssid, doc["ssid"]);  
  strcpy(password, doc["password"]); 
  strcpy(mqtt_server, doc["mqtt server"]); 
  strcpy(location, doc["location"]);

  long poll_period_sec = doc["poll period"];
  if (poll_period_sec){
    poll_period = poll_period_sec * 1000;
    demand_mode = false;
  }
  else{
    demand_mode = true;
  }
  
  ack_timeout = doc["ack timeout"]; 
  sleep_mode = doc["sleep mode"];
  
  file.close();
  
  Serial.println();
  
  //Serial.println(ssid);
  //Serial.println(password);
  //Serial.println(mqtt_server);

  //Serial.println(poll_period_sec);
  //Serial.println(poll_period);
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
void connect_to_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    flash_led();
    delay(500);
    Serial.print(".");
  }
  
}

/*
 *      connect_to_mqtt
 * 
 * 
 *   Connects to the wireless newtork given in settings.
 *   Flashes LED once every 4 seconds when trying to connect.
 *   
 */
void connect_to_mqtt() {
  Serial.print("Attempting MQTT connection...");
  
  // Attempt to connect
  if (client.connect(location)) {
    Serial.println("connected");
    char topic[32];
    sprintf(topic, "%s/input", location);
    client.subscribe(topic);
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
    // Wait 5 seconds before retrying
    ArduinoOTA.handle();
    flash_led();
    delay(4000);
  }
}

/*
 * 
 *        acquire_data
 *        
 *    acquires data from SHT21 sensor.
 *    Returns data structure.
 *    
 */
Data acquire_data()
{
  Data output = {
    (int)SHT2x.GetTemperature(),
    (int)SHT2x.GetHumidity(),
    (int)SHT2x.GetDewPoint(),
  };
  return output;
}

/*
 *        send message
 *        
 *    Sends mqtt message to relevent topic depending on message type.
 *    Flashes LED twice.    
 *    
 */
void send_message(MessageType type){
  Data data;
  send_message(type, data);
}

void send_message(MessageType type, Data data){
  char topic[32];
  char message[32];
  switch (type){
    case STAMPED:{
      sprintf (message, "%d: %d, %d, %d", message_id++, 
                                          data.temperature, 
                                          data.humidity, 
                                          data.dew_point);
      sprintf (topic, "%s/stamped", location);
      strcpy(last_message, message);
      data_ack = false;
      poll_timer = ack_timer = millis();
      break;
    }
    case RETRY:{
      sprintf (topic, "%s/stamped", location);
      strcpy(message, last_message);
      ack_timer = millis();
      break;
    }
    case UNSTAMPED:{
      sprintf (message, "%d, %d, %d", data.temperature, 
                                      data.humidity, 
                                      data.dew_point);
      sprintf (topic, "%s/unstamped", location);
      break;
    }
    case REG:{
      sprintf(message, location);
      sprintf(topic, "sensor_register");
      ack_timer = millis();
      break;
    }
    case ACK:{
      sprintf(message, location);
      sprintf(topic, "acknowledgements");
      break;
    }
  }
  client.publish(topic, message);
  Serial.print("Message sent [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
  flash_led();
  delay(500);
  flash_led();
}

/*
 *        on_reset
 *        
 *    Takes and sends an imediate reading. 
 *    Starts taking scheduled readings.
 */
void on_reset(int id)
{
  if (id != last_reset_id){
    Data data = acquire_data();
    send_message(STAMPED, data);
    taking_readings = true;
    last_reset_id = id;
  }
}

/*
 *        on_ack
 *        
 *    Stops re-sending messages.
 *    Sleeps if in sleep mode.
 */
void on_ack(ackType ack_type)
{
  if (ack_type == DATA_ACK){
    data_ack = true;
    if (turn_off_wifi) {
      WiFi.mode(WIFI_OFF);
      Serial.println("Wifi Disconnected");
      turn_off_wifi = false;
    }
  }
  else if (ack_type = REG_ACK){
    reg_ack = true;
  }
}

/*
 *        on_unstamped_poll
 *        
 *    Takes reading and sends data.
 */
void on_unstamped_poll()
{
  Data data = acquire_data();
  send_message(UNSTAMPED, data);
}

/*        
 *         on_stamped_poll
 *     
 *     Takes reading and sends data.
 *     Sets up sleep period if requested.
 */
void on_stamped_poll(long sleep_sec)
{
  Data data = acquire_data();
  send_message(STAMPED, data);
  if (sleep_sec){
    turn_off_wifi = true;      
    sleep_period = sleep_sec * 1000;
    Serial.print("Turning off for ");
    Serial.print(sleep_sec);
    Serial.println(" seconds after ack");
    sleep_timer = millis(); 
  }
}

/*
 *        on_standby
 *        
 *    Sends acknoledgement.    
 *    stops taking readings.
 */
 void on_standby()
 {
  send_message(ACK);
  taking_readings = false;
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
  String message("");
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);
  
  if (message.startsWith("R") && !demand_mode){
    message.remove(1, 0);
    on_reset(message.toInt());
  }
  else if (message.startsWith("ack")){
    message.remove(0, 4);
    if (message == "data"){
      on_ack(DATA_ACK);
    }
    else if (message == "reg"){
      on_ack(REG_ACK);
    }
  }
  else if (message == "POLL"){
    on_unstamped_poll();
  }
  else if (message.startsWith("P")){
    message.remove(0, 1);
    on_stamped_poll(message.toInt());
  }
  else if (message == "standby"){
    on_standby();
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
  Wire.begin();
  
  // Setup wifi:
  if (!load_config()){
    Serial.println("Config file error. exiting");
    while(1){
      delay(1000);
    }
  }
  Serial.println(""); 
  Serial.print("Sensor location: ");
  Serial.println(location);
  
  digitalWrite(LED, HIGH);
  
  connect_to_wifi();
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Setup ota:
  ArduinoOTA.onStart(on_ota_start);
  ArduinoOTA.begin();

  // Setup mqtt:
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

/*
 *        loop
 *        
 *    Main loop.
 *    
 */
void loop() 
{
  // reconect wifi after timer:
  if (WiFi.status() == WL_DISCONNECTED && 
      (unsigned long)(millis() - sleep_timer) >= sleep_period){
    connect_to_wifi();
    sleep_period = 0;      
  }
  
  // reconect mqtt:
  if (WiFi.status() == WL_CONNECTED && !client.connected()) {
    connect_to_mqtt(); 
  }

  // send / re-send register message until acknowledgement:
  if (!demand_mode && !reg_ack && ((unsigned long)(millis() - ack_timer) >= ack_timeout)){
    send_message(REG);
  }
  // re-send data until acknowledgement:
  else if (!data_ack && ((unsigned long)(millis() - ack_timer) >= ack_timeout)){
    send_message(RETRY);
  }
  // send data every poll_period mS if nessesary: 
  else if (taking_readings && (unsigned long)(millis() - poll_timer) >= poll_period){
    Data data = acquire_data();
    send_message(STAMPED, data);
    turn_off_wifi = sleep_mode ? true : false;
  }

  client.loop();
  ArduinoOTA.handle();
  delay(1000);
}
