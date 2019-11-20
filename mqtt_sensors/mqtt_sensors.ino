
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <FS.h>
#include <Sodaq_SHT2x.h>
#include <Wire.h>
#include <ArduinoJson.h>

#define ACK_DELAY 5000 // time to wait for acknowledgement before re-sending data in mS

char ssid[16];
char password[16];
char mqtt_server[32];
char location[16];
char in_topic[32];
char out_topic[32];
unsigned long poll_period;

bool acknowledged = true;

unsigned long poll_timer = 0;
unsigned long ack_timer; 

char message[32];
int message_id = 0;

WiFiClient espClient;
PubSubClient client(espClient);

// OTA Functions: 

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

int load_config() {

  File file;
  const size_t capacity = JSON_OBJECT_SIZE(5) + 150;
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
  poll_period = poll_period_sec * 1000;
  
  sprintf(in_topic, "%s/input", location);
  sprintf(out_topic, "%s/output", location);
   
  file.close();
  
  Serial.println();
  
  //Serial.println(ssid);
  //Serial.println(password);
  //Serial.println(mqtt_server);
  
  //Serial.println(location);
  //Serial.println(in_topic);
  //Serial.println(out_topic);
  //Serial.println(poll_period_sec);
  //Serial.println(poll_period);
  return 1;
}

void connect_to_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
}

void connect_to_mqtt() {
  Serial.print("Attempting MQTT connection...");
  
  // Attempt to connect
  if (client.connect(location)) {
    Serial.println("connected");
    // Once connected, publish an announcement...
    client.publish(out_topic, "Ready");
    // ... and resubscribe
    client.subscribe(in_topic);
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
    // Wait 5 seconds before retrying
    ArduinoOTA.handle();
    delay(5000);
  }
}

void aquire_data(){
  int temperature = (int)SHT2x.GetTemperature();
  int humidity = (int)SHT2x.GetHumidity();
  int dew_point = (int)SHT2x.GetDewPoint();
  sprintf (message, "%d: %d, %d, %d", message_id++, temperature, humidity, dew_point);
  acknowledged = false;
  poll_timer = millis();
}

void send_message(){
  Serial.print("sending: ");
  Serial.println(message);
  client.publish(out_topic, message);
  ack_timer = millis();
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  String message("");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    message += (char)payload[i];
  }
  Serial.println(message);
  
  if (message == "reset"){
    aquire_data();
    send_message();
  }
  else if (message == "ack"){
    acknowledged = true; 
  }
}

void setup() {

  Serial.begin(115200);
  Wire.begin();
  
  // Setup wifi:
  if (!load_config()){
    Serial.println("Config file error. exiting");
    while(1){
      delay(1000);
    }
  }
  connect_to_wifi();
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Setup ota:
  ArduinoOTA.onStart(on_ota_start);
  /*
  ArduinoOTA.onEnd(on_ota_end);
  ArduinoOTA.onProgress(on_ota_progress);
  ArduinoOTA.onError(on_ota_error);
  */
  ArduinoOTA.begin();

  // Setup mqtt:
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    connect_to_mqtt(); 
  }

  // send data every poll_period mS: 
  if ((unsigned long)(millis() - poll_timer) >= poll_period){
    aquire_data();
    send_message();
  }

  // re-send data every ACK_DELAY mS until acknowledgement:
  else if (!acknowledged && ((unsigned long)(millis() - ack_timer) >= ACK_DELAY)){
    send_message();
  }
  
  client.loop();
  ArduinoOTA.handle();
  delay(1000);
}




