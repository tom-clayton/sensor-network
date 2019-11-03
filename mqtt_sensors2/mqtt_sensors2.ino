
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <FS.h>
#include <Sodaq_SHT2x.h>
#include <Wire.h>
#include <ArduinoJson.h>

char ssid[16];
char password[16];
char mqtt_server[32];
char location[16];
char in_topic[32];
char out_topic[32];

bool ack = true;

WiFiClient espClient;
PubSubClient client(espClient);

// OTA Functions:

void on_ota_start() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    SPIFFS.end();
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
}

void on_ota_end() {
   Serial.println("\nEnd");
}

void on_ota_progress(unsigned int progress, unsigned int total) {
   Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
}

void on_ota_error(ota_error_t error){
   Serial.printf("Error[%u]: ", error);
   if (error == OTA_AUTH_ERROR) {
     Serial.println("Auth Failed");
   } else if (error == OTA_BEGIN_ERROR) {
     Serial.println("Begin Failed");
   } else if (error == OTA_CONNECT_ERROR) {
     Serial.println("Connect Failed");
   } else if (error == OTA_RECEIVE_ERROR) {
     Serial.println("Receive Failed");
   } else if (error == OTA_END_ERROR) {
     Serial.println("End Failed");
   }
}

int get_config() {

  File file;
  const size_t capacity = JSON_OBJECT_SIZE(4) + 150;
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
  strcpy(mqtt_server, doc["mqtt_server"]); 
  strcpy(location, doc["location"]);
  sprintf(in_topic, "%s/input", location);
  sprintf(out_topic, "%s/output", location);
   
  file.close();
  
  Serial.println();
  /*
  Serial.println(ssid);
  Serial.println(password);
  Serial.println(mqtt_server);
  */
  Serial.println(location);
  Serial.println(in_topic);
  Serial.println(out_topic);
  
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

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  String message("");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    message += (char)payload[i];
  }
  Serial.println();
  if (message == "poll"){
    send_data();
    ack = false;
  }
  else if (message == "ack"){
    ack = true;
    client.publish(out_topic, "ack"); 
  }
}

void send_data(){
  int temperature = (int)SHT2x.GetTemperature();
  int humidity = (int)SHT2x.GetHumidity();
  int dew_point = (int)SHT2x.GetDewPoint();
  char out_buffer[16];

  sprintf (out_buffer, "%d, %d, %d", temperature, humidity, dew_point);
  Serial.println(out_buffer);
  client.publish(out_topic, out_buffer);
}

void setup() {

  Serial.begin(115200);
  Wire.begin();
  
  // Setup wifi:
  if (!get_config()){
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
  ArduinoOTA.onEnd(on_ota_end);
  ArduinoOTA.onProgress(on_ota_progress);
  ArduinoOTA.onError(on_ota_error);
  ArduinoOTA.begin();

  // Setup mqtt:
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    connect_to_mqtt(); 
  }
  if (!ack){
    send_data();
  }
  client.loop();
  ArduinoOTA.handle();
  delay(1000);
}




