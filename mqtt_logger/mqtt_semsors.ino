lude <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <FS.h>
#include <Sodaq_SHT2x.h>
#include <Wire.h>

char ssid[32];
char password[32];
char mqtt_server[32];

bool started = false;
int start_time = 0;
int poll_period = 60000; // in ms.

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

int apply_settings() {

  File file;

  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return 0;
  }

  file = SPIFFS.open("/settings", "r");
  if(!file){
    Serial.println("Failed to open file for reading");
    return 0;
  }
  int win = 0;
  int word_len = file.readBytesUntil('\n', ssid, sizeof(ssid));
  if (ssid[word_len - 1] == '\r') {
    win = 1;
  }
  ssid[word_len - win] = 0;
  
  word_len = file.readBytesUntil('\n', password, sizeof(password));
  password[word_len - win] = 0;

  word_len = file.readBytesUntil('\n', mqtt_server, sizeof(mqtt_server));
  mqtt_server[word_len - win] = 0;
  
  file.close();
  /*
  Serial.println();
  Serial.println(ssid);
  Serial.println(password);
  Serial.println(mqtt_server);
  */
  return(1);
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
  // Create a random client ID
  String clientId = "ESP8266Client-";
  clientId += String(random(0xffff), HEX);
  // Attempt to connect
  if (client.connect(clientId.c_str())) {
    Serial.println("connected");
    // Once connected, publish an announcement...
    // client.publish("output", "Ready");
    // ... and resubscribe
    client.subscribe("command");
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
    // Wait 5 seconds before retrying
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

  if (message.equals("start")) {
    send_data();
    started = true;
    start_time = millis();
  }
}

void send_data(){
  int temperature = (int)SHT2x.GetTemperature();
  int humidity = (int)SHT2x.GetHumidity();
  char buf[16];
 
  snprintf (buf, 16, "%d, %d", temperature, humidity);
  Serial.println(buf);
  client.publish("output", buf);
}

void setup() {

  Serial.begin(115200);
  Serial.println("Booting");
  //pinMode(LED_BUILTIN, OUTPUT);
  randomSeed(micros());
  Wire.begin();
  
  // Setup wifi:
  apply_settings();
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
    ArduinoOTA.handle();
  }
  client.loop();
  if (started && (millis() > start_time + poll_period)) {
    send_data();
    start_time = millis();
  }
}
