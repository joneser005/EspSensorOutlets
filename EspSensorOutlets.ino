/*
 ESP8266 4-outlet controller + temp/humidity sensor
 Endpoint for HA+MQTT
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>  /* https://pubsubclient.knolleary.net/api.html */
#include "dht.h"

#include <local-config.h>

/****************************************************************************************/
#define MQTT_KEEPALIVE 15*60+10  /* override PubSubClient.h */
const unsigned long ENV_UPDATE_SECS = MQTT_KEEPALIVE-10;
#define DHT11_PIN 5
const unsigned int OUTLET_PIN [] = {14, 4, 0, 2};
#define OUTLET_CT 4


/****************************************************************************************/
dht DHT;
WiFiClient wifiClient;
void mqtt_callback(char* topic, byte* payload, unsigned int length);
PubSubClient mqttClient(MQTT_HOST, MQTT_PORT, mqtt_callback, wifiClient);
/****************************************************************************************/

/****************************************************************************************/
/****************************************************************************************/
void setup() {
  Serial.begin(115200);
  delay(10);

  for (int i=0; i<OUTLET_CT; i++) {
    pinMode(OUTLET_PIN[i], OUTPUT);
    digitalWrite(OUTLET_PIN[i], LOW);
  }
  pinMode(BUILTIN_LED, OUTPUT);

  connectWifi();
}
/****************************************************************************************/


void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  if (2 != length) {
    Serial.println("mqtt_callback: Bad length.");
    return; 
  }

  // Switch on the LED if an 1 was received as first character
  int i = ((int) payload[0]) - 48;
  Serial.print("i=");
  Serial.println(i);
  if (0 < i && i <= OUTLET_CT) {  // starting at one b/c invalid data could potentiall present as zero
      int val = ((int) payload[1]) - 48;
      Serial.print("val=");
      Serial.println(val);
      setOutlet(i-1, (val==1));
  }
}


void setOutlet(unsigned int n, bool state) {
  if (n >= OUTLET_CT) {
    Serial.print("Validation error: Invalid outlet requested: ");
    Serial.println(n);
    return;
  }

  digitalWrite(OUTLET_PIN[n], (state) ? LOW: HIGH);
  Serial.print("digitalWrite(index/pin:");
  Serial.print(n);
  Serial.print("/");
  Serial.print(OUTLET_PIN[n]);
  Serial.print(", ");
  Serial.println((state) ? "LOW" : "HIGH");
}


float ctof(float c) {
  return (c * 1.8) + 32;
}


void connectWifi() {
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_P);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("-");
  }
 
  Serial.println("");
  Serial.println("WiFi connected");  
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Netmask: ");
  Serial.println(WiFi.subnetMask());
  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());
}

void connectMqtt() {
  if (!mqttClient.connected()) {
    char willTopic[27];
    sprintf(willTopic, "/status/%s", MQTT_CLIENT_ID);
    const char *willMsg = "Disconnected";
    
    mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_P, willTopic, 0, 1, willMsg);
    mqttClient.subscribe(TOPIC_OUTLET);
  }
}


bool readDht11() {
  bool result = false;
  int chk = DHT.read11(DHT11_PIN);
  switch (chk) {
    case DHTLIB_OK:  
      Serial.println("DHT11 READ OK"); 
      result = true;
      break;
    case DHTLIB_ERROR_CHECKSUM: 
      Serial.println("DHT11 Checksum error"); 
      break;
    case DHTLIB_ERROR_TIMEOUT: 
      Serial.println("DHT11 Time out error"); 
      break;
    default: 
      Serial.println("DHT11 Unknown error"); 
      break;
  }
  return result;
}


void sendEnvReadings() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Not connected?  Trying to connect.....");
    connectWifi();
  }

  connectMqtt();

  if (readDht11()) {
    char payload[24];
    
    float temperature = ctof(DHT.temperature);
    char sTemp[10];
    dtostrf(temperature, 4, 2, sTemp);

    float humidity = DHT.humidity;
    char sHumidity[10];
    dtostrf(humidity, 4, 2, sHumidity);
    
    sprintf(payload, "%s %s", sTemp, sHumidity);
    Serial.println(payload);
    Serial.print("MQTT client is connected?  ");
    Serial.println(mqttClient.connected());
    if (mqttClient.publish(TOPIC_ENV, payload)) {
      Serial.println("Publish succeeded");
    } else {
      Serial.println("Publish FAILED!");
      mqttClient.disconnect();
    }
  }
}


float lastRecordedTemp = 0.0;
float lastRecordedHumidity = 0.0;
float tempChangeThreshold = 0.5;
float humidityChangeThreshold = 1.0;

unsigned long timeToSendEnvReadings = 0;
/****************************************************************************************/
/****************************************************************************************/
void loop() {
  delay(500); // dht11 fastest poll frequency is 1 second.  This will also limit polling for outlet mqtt messages.
  mqttClient.loop();
  if (millis() > timeToSendEnvReadings) {
    Serial.println("Time to update environment readings.....");
    sendEnvReadings();
    timeToSendEnvReadings = millis() + (1000 * ENV_UPDATE_SECS);
  }
}
