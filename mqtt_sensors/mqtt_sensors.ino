
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <FS.h>
#include <Sodaq_SHT2x.h>
#include <Wire.h>
#include <ArduinoJson.h>

struct Data{
  int temperature,
      humidity,
      dew_point;
};

enum MessageType{
  SCHEDULED,
  RETRY,
  ON_DEMAND
};

char ssid[16];
char password[16];
char mqtt_server[32];
char location[16];
unsigned long poll_period;
unsigned long ack_timeout;
bool demand_only;

bool acknowledged = true;

unsigned long poll_timer = 0;
unsigned long ack_timer; 

char last_message[32];
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
  const size_t capacity = JSON_OBJECT_SIZE(6) + 150;
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
    demand_only = false;
  }
  else{
    demand_only = true;
  }
  
  ack_timeout = doc["ack timeout"]; 
  
  file.close();
  
  Serial.println();
  
  //Serial.println(ssid);
  //Serial.println(password);
  //Serial.println(mqtt_server);

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
    char topic[32];
    sprintf(topic, "%s/input", location);
    client.subscribe(topic);
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
    // Wait 5 seconds before retrying
    ArduinoOTA.handle();
    delay(5000);
  }
}

Data aquire_data()
{
  Data output = {
    (int)SHT2x.GetTemperature(),
    (int)SHT2x.GetHumidity(),
    (int)SHT2x.GetDewPoint(),
  };
  return output;
}

void send_message(MessageType type, Data data){
  char topic[32];
  char message[32];
  switch (type){
    case SCHEDULED:{
      sprintf (message, "%d: %d, %d, %d", message_id++, 
                                          data.temperature, 
                                          data.humidity, 
                                          data.dew_point);
      sprintf (topic, "%s/scheduled", location);
      strcpy(last_message, message);
      acknowledged = false;
      poll_timer = ack_timer = millis();
      break;
    }
    case RETRY:{
      sprintf (topic, "%s/scheduled", location);
      strcpy(message, last_message);
      ack_timer = millis();
      break;
    }
    case ON_DEMAND:{
      sprintf (message, "%d, %d, %d", data.temperature, 
                                      data.humidity, 
                                      data.dew_point);
      sprintf (topic, "%s/ondemand", location);
      break;
    }
  }
  client.publish(topic, message);
  Serial.print("Message sent [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
}

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
  
  if (message == "reset" && !demand_only){
    Data data = aquire_data();
    send_message(SCHEDULED, data);
  }
  else if (message == "ack"){
    acknowledged = true; 
  }
  else if (message == "poll"){
    Data data = aquire_data();
    send_message(ON_DEMAND, data); 
  }
}

void setup() 
{
  Serial.begin(115200);
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

void loop() 
{
  if (!client.connected()) {
    connect_to_mqtt(); 
  }

  // send data every poll_period mS: 
  if (!demand_only && (unsigned long)(millis() - poll_timer) >= poll_period){
    Data data = aquire_data();
    send_message(SCHEDULED, data);
  }

  // re-send data every ack_timeout mS until acknowledgement:
  else if (!acknowledged && ((unsigned long)(millis() - ack_timer) >= ack_timeout)){
    Data data;
    send_message(RETRY, data);
  }
  
  client.loop();
  ArduinoOTA.handle();
  delay(5000);
}
