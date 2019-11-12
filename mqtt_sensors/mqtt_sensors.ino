
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>
#include <FS.h>
#include <Sodaq_SHT2x.h>
#include <Wire.h>
#include <ArduinoJson.h>

#define POLL_PERIOD     10000 // in mS

char ssid[16];
char password[16];
char mqtt_server[32];
char location[16];
//char in_topic[32];
//char out_topic[32];

unsigned long last_poll = 0;


WiFiClient esp_client;
Adafruit_MQTT_Client *mqtt; 
Adafruit_MQTT_Publish *output;
Adafruit_MQTT_Subscribe *input;

void on_ota_start() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    SPIFFS.end();
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
  //sprintf(in_topic, "%s/input", location);
  //sprintf(out_topic, "%s/output", location);
   
  file.close();
  /*
  Serial.println();
  Serial.println(ssid);
  Serial.println(password);
  Serial.println(mqtt_server);
  
  Serial.println(location);
  //Serial.println(in_topic);
  //Serial.println(out_topic);
  */
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

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt->connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt->connect()) != 0) { // connect will return 0 for connected
       Serial.println(mqtt->connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt->disconnect();
       delay(5000);  // wait 5 seconds
       retries--;
       if (retries == 0) {
         // basically die and wait for WDT to reset me
         while (1);
       }
  }
  Serial.println("MQTT Connected!");
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  // Read config file:
  if (!get_config()){
    while(1){
      delay(1000);
    }
  }
  
  // Setup wifi:
  connect_to_wifi();
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  
  // Setup ota:
  ArduinoOTA.onStart(on_ota_start);
  //ArduinoOTA.onEnd(on_ota_end);
  //ArduinoOTA.onProgress(on_ota_progress);
  //ArduinoOTA.onError(on_ota_error);
  ArduinoOTA.begin();

  // Setup mqtt:
  mqtt = new Adafruit_MQTT_Client(&esp_client, mqtt_server, 1883);
  char buf[32];
  sprintf(buf, "%s/output", location);
  output = new Adafruit_MQTT_Publish(mqtt, buf);
  sprintf(buf, "%s/input", location);
  input = new Adafruit_MQTT_Subscribe(mqtt, buf);
  mqtt->subscribe(input);

}

void loop() {
  MQTT_connect();

  if ((unsigned long)(millis() - last_poll) >= POLL_PERIOD){
    output->publish("test");
  }
  Adafruit_MQTT_Subscribe *subscription;
  
  while ((subscription = mqtt->readSubscription(5000))) {
    if (subscription == input) {
      Serial.println((char *)input->lastread);
    } 
  }

}
