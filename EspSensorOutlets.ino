/*
 ESP8266 4-outlet controller + temp/humidity sensor
 Endpoint for HA+MQTT
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>  /* https://pubsubclient.knolleary.net/api.html */
#include "dht.h"

#include "local-config.h"

/****************************************************************************************/
#define MQTT_KEEPALIVE 5*60+10  // overrides PubSubClient.h
#define MQTT_PUB_SPAN_MS 100  // publishing messages in rapid success seems to be problematic?
const unsigned long ENV_UPDATE_SECS = MQTT_KEEPALIVE-10;
#define DHT11_PIN 5
const unsigned int OUTLET_PIN [] = {14, 4, 0, 2};
#define OUTLET_CT 4


/****************************************************************************************/
dht DHT;
WiFiClient wifiClient;
void mqtt_callback(char* topic, byte* payload, unsigned int length);
PubSubClient mqttClient(MQTT_HOST, MQTT_PORT, mqtt_callback, wifiClient);
float lastPubTime = 0;
// Humidity sensor reads low.  Until we know more about its (in)accuracy, performing a
// crude correction to the value to known value.
// nov 2017 reads 21, actual is 45
#define DHT_HUMID_OFFSET 24
/****************************************************************************************/

/****************************************************************************************/
/****************************************************************************************/
void setup() {
  Serial.begin(115200);
  delay(10);

  for (int i=0; i<OUTLET_CT; i++) {
    pinMode(OUTLET_PIN[i], OUTPUT);
    digitalWrite(OUTLET_PIN[i], HIGH);
  }

  connectWifi();
  connectMqtt();
  delay(50);

//// NOW: Publish off state for all outlets so any clients watching state are aware.
////      We don't care if we aren't listening yet, this set of messages aren't for us :-)
//// FUTURE: Save outlet values in EPROM so we can power up to last known state.
  lastPubTime = millis();  // force a delay before we send - was having problems here
  publishFloat(TOPIC_OUTLET1, 0);
  mqttClient.loop();
  publishFloat(TOPIC_OUTLET2, 0);
  mqttClient.loop();
  publishFloat(TOPIC_OUTLET3, 0);
  mqttClient.loop();
  publishFloat(TOPIC_OUTLET4, 0);
  mqttClient.loop();
}
/****************************************************************************************/


void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] [");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println("]");

  // Switch on the LED if an 1 was received as first character
  int i = 0;
  if (0 == strcmp(topic, TOPIC_OUTLET1)) {
    i = 1;
  } else if (0 == strcmp(topic, TOPIC_OUTLET2)) {
    i = 2;
  } else if (0 == strcmp(topic, TOPIC_OUTLET3)) {
    i = 3;
  } else if (0 == strcmp(topic, TOPIC_OUTLET4)) {
    i = 4;
  } 
  if (0 < i && i <= OUTLET_CT) {  // starting at one b/c invalid data could potentially present as zero
      int val = ((int) payload[0]) - 48;
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
  Serial.print("): ");
  Serial.println((state) ? "LOW" : "HIGH");
}


float ctof(float c) {
  return (c * 1.8) + 32;
}


void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

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
    Serial.print("Connecting to MQTT server.....");
    char willTopic[27];
    sprintf(willTopic, "/status/%s", MQTT_CLIENT_ID);
    const char *willMsg = "Disconnected";
    
    mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_P, willTopic, 0, 1, willMsg);
    if (mqttClient.connected()) {
      Serial.println("OK");
    } else {
      Serial.println("FAILED!");
      Serial.print("MQTT state = ");
      Serial.println(mqttClient.state()); // neg=lost connection, pos=never had one
    }
    mqttClient.subscribe(TOPIC_OUTLET1);
    mqttClient.subscribe(TOPIC_OUTLET2);
    mqttClient.subscribe(TOPIC_OUTLET3);
    mqttClient.subscribe(TOPIC_OUTLET4);
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

  connectMqtt();  // leave the connection open for All of Time

  if (readDht11()) {
    publishFloat(TOPIC_ENV_TEMP, ctof(DHT.temperature));
    float h = DHT.humidity + DHT_HUMID_OFFSET;
    if (h > 100) {
      h = 99;
    }
    publishFloat(TOPIC_ENV_HUMID, h);
  }
}
    

void publishFloat(const char * topic, const float val) {
  char sval[10];
  dtostrf(val, 4, 2, sval);
  publishString(topic, sval);
}

void publishInt(const char * topic, const int val) {
  char sval[10];
  itoa(val, sval, 10);
  publishString(topic, sval);
}

void publishString(const char * topic, const char * val) {
  Serial.print("MQTT client is connected?  ");
  Serial.println(mqttClient.connected());
  Serial.print("Publish to topic [");
  Serial.print(topic);
  Serial.print("] value [");
  Serial.print(val);
  Serial.print("] ");
  while (millis() < lastPubTime + MQTT_PUB_SPAN_MS) {
    delay(20);
  }
  if (mqttClient.publish(topic, val)) {
    lastPubTime = millis();
    Serial.println("succeeded");
  } else {
    Serial.println("FAILED!");
//    mqttClient.disconnect();
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
//  delay(250);
  mqttClient.loop();
  if (millis() > timeToSendEnvReadings) {
    Serial.println("Time to update environment readings.....");
    sendEnvReadings();
    timeToSendEnvReadings = millis() + (1000 * ENV_UPDATE_SECS);
  }
}
