//
// Read DHT11/22 temperature and humidity values and send them to MQTT server
// What it does is:
//   scan WIFI for known access points
//   if AP found, connect wifi, retrieve time from NTP server, connect to MQTT server using TLS and start poling DHT sensor and publish temperature and humidity.
//   publish also free ram and uptime, for test purpose
//   blinking LED as in the NodeMcu project.
//
// Lavaux Gilles 2017-06
//

#include <Time.h>
// DHT libs:
#include <DHT.h>
#include <DHT_U.h>
// ESP8266:
#include <ESP8266WiFi.h>
// MQTT libs:
//#include "Adafruit_MQTT.h"
//#include "Adafruit_MQTT_Client.h"
#include <PubSubClient.h>
// flash filesystem lib:
#include "FS.h"
// the MQTT and  AP configuration
#include "config.h"


#define VERSION "V:0.6.12 Lavaux Gilles 2017-08-04. Last change: first indor TLS/NON_TLS version"



// status: used in loop to blink led
#define NO_WIFI 1
#define AP_FOUND 10
#define IP_OK 40
#define MQTT_OK 100

int status=NO_WIFI;

// LED status
boolean toggle=false;

// Access Point lists and password
boolean apFound = false;
const char *apUsed;

// loops:
long baseLoopInterval = 50;
long previousBaseLoopInterval = 0;
// AP interval used to scan AP + read DHT: 10 sec
long apLoopInterval = 10000;
long previousLoopMillis = 0;
// mqtt ping interval: 1 min
long pingInterval = 60000;
long previousPingMillis = 0;
// mqtt publish interval: 60 sec
long publishInterval = 60000;
long previousPublishMillis = 0;
unsigned long publishCount = 0;
// temperature and humidity
float temp;
float humi;
bool valuesOk = false;
//
unsigned long currentMillis;
unsigned long oldMillis;
float uptime;


// init dht object
DHT dht(DHTPIN, DHTTYPE, 20);


// wifi client: unsecure or secure
WiFiClient wifiClient;
WiFiClientSecure wifiClientSecure;
// mqtt client
#ifdef USE_TLS
  PubSubClient mqttClient(wifiClientSecure);
#else
  PubSubClient mqttClient(wifiClient);
#endif

// mqtt feeds
String fullTopicTemp = MQTT_TOPIC_PREFIX + String(ESP.getChipId(), HEX) + String("/temp");
String fullTopicHumi = String("mobile/esp_") + String(ESP.getChipId(), HEX) + String("/humi");
String fullTopicUp = String("mobile/esp_") + String(ESP.getChipId(), HEX) + String("/uptime");
String fullTopicMem = String("mobile/esp_") + String(ESP.getChipId(), HEX) + String("/mem");
String clientId = String("ESP_client_") + String(ESP.getChipId());



//
// setup serial and wifi mode
//
void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println();
  Serial.println(VERSION);

  #ifdef USE_TLS
    Serial.println(" SECURE VERSION !!!");
  #else
    Serial.println(" NON SECURE VERSION !!!");
  #endif

  Serial.printf(" the ESP8266 chip ID as a 32-bit integer: %08X\n", ESP.getChipId());
  Serial.printf(" the flash chip ID as a 32-bit integer: %08X\n", ESP.getFlashChipId());
  Serial.printf(" flash chip frequency: %d (Hz)\n", ESP.getFlashChipSpeed());
  Serial.print(" will publish on topics:");
  Serial.print("  ");
  Serial.println(fullTopicTemp);
  Serial.print("  ");
  Serial.println(fullTopicHumi);
  Serial.print("  ");
  Serial.println(fullTopicUp);
  Serial.print("  ");
  Serial.println(fullTopicMem);

  // use onboard led?
  if(useOnBoardLed){
    Serial.print(" using buildin led:");
    Serial.print(LED_BLUE);
    Serial.println(" as output.");
    pinMode(LED_BLUE, OUTPUT);
  }else{
    Serial.print(" don't use buildin led, but led at gpio:");
    Serial.println(LED);
  }

  // wifi
  WiFi.mode(WIFI_STA);
  Serial.println(" WIFI set in station mode");
  WiFi.disconnect();
  Serial.println(" WIFI reset");

  // mqtt
  #ifdef USE_TLS
    mqttClient.setServer(AIO_SERVER, AIO_SERVERPORTSECURE);
  #else
    mqttClient.setServer(AIO_SERVER, AIO_SERVERPORT);
  #endif
  mqttClient.setCallback(mqtt_callback);
  delay(100);
}

//
// change status, reset previousBaseLoopInterval in order to activate base loop test
//
void changeStatus(int s){
  if(s==status){
    return;
  }
  Serial.print(" ##################### change status from: ");
  Serial.print(status);
  Serial.print(" to: ");
  Serial.println(s);
  status=s;
  previousBaseLoopInterval =  millis();
}

//
// get free heap 
//
String getFreeHeap(){
  long  fh = ESP.getFreeHeap();
  char  fhc[20];
  ltoa(fh, fhc, 10);
  return String(fhc);
}

//
// list content of the flash /data storage
// where the certificates are
//
void listDir() {
  char cwdName[2];
  Serial.println("");
  strcpy(cwdName,"/");
  Dir dir=SPIFFS.openDir(cwdName);
  while( dir.next()) {
    String fn, fs;
    fn = dir.fileName();
    //fn.remove(0, 1);
    fs = String(dir.fileSize());
    Serial.println(" - a flash data file:" + fn + "; size=" + fs);
  } // end while
}

//
// set time based on SNTP server
//
boolean setTime(int timezone, uint8_t r){
  //int timezone = 2;
  // Synchronize time useing SNTP. This is necessary to verify that
  // the TLS certificates offered by the server are currently valid.
  Serial.println("");
  Serial.print(" setting time[");
  Serial.print(r);
  Serial.print("] using SNTP ");
  configTime(timezone * 3600, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  uint8_t wait = 10;
  while (now < 1000) {
    delay(500);
    Serial.print("_-");
    now = time(nullptr);
    wait--;
    if (wait == 0) {
       return false;
    }
  }
  // digital clock display of the time
  Serial.print(" date is now: ");
  Serial.println(ctime(&now)); 
  Serial.println(" setTime done");
  return true;
}


//
// get uptime in mins
//
void getUptime(){
    currentMillis = millis();
    if (currentMillis < oldMillis){
        Serial.print(" !! millis() has rolled over after:");
        Serial.print(oldMillis);
    }
    oldMillis=currentMillis;
    uptime = currentMillis/60000.0;
    Serial.print(" uptime (mins):");
    Serial.println(uptime);
}


//
// load certificates into client
//
void loadCerts(){
  if (!SPIFFS.begin()) {
    Serial.println(" !! Failed to mount file system !!");
    return;
  }
  listDir();
  File crt = SPIFFS.open(AIO_CERT_FILE, "r");
  if (!crt) {
    Serial.println(" !! failed to open crt file !!");
  }else{
    Serial.print(" successfully opened crt file:");
    Serial.println(AIO_CERT_FILE);
    wifiClientSecure.loadCertificate(crt);
    Serial.println(" crt file loaded in client");
  }

  File key = SPIFFS.open(AIO_KEY_FILE, "r");
  if (!key) {
    Serial.println(" !! failed to open key file !!");
  }else{
    Serial.print(" successfully opened key file:");
    Serial.println(AIO_KEY_FILE);
    wifiClientSecure.loadPrivateKey(key);
    Serial.println(" key file loaded in client");
  }
}

//
// read DHT
//
void readDht(){
      Serial.print(" readDht values: ");
      dht.begin();
      // Reading temperature or humidity takes about 250 milliseconds!
      // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
      float humi1 = dht.readHumidity();
      // Read temperature as Celsius
      float temp1 = dht.readTemperature();
      // Check if any reads failed and exit early (to try again).
      if (isnan(humi1) || isnan(temp1)) {
        Serial.println(" !! failed to read from DHT sensor !!");
        Serial.println(humi1);
        Serial.println(temp1);
        valuesOk = false;
        return;
      }
      valuesOk = true;
      temp = temp1;
      humi = humi1;
      Serial.print(" humidity: "); 
      Serial.print(humi);
      Serial.print(" %\t");
      Serial.print(" temperature: "); 
      Serial.print(temp);
      Serial.println(" *C ");
}

//
//
//
String getMqttSatusString(){
  int s = mqttClient.state();
  switch (s) {
    case MQTT_CONNECTION_TIMEOUT:
      return "MQTT_CONNECTION_TIMEOUT";
      break;
    case MQTT_CONNECTION_LOST:
      return "MQTT_CONNECTION_LOST";
      break;
    case MQTT_CONNECT_FAILED:
      return "MQTT_CONNECT_FAILED";
      break;
    case MQTT_DISCONNECTED:
      return "MQTT_DISCONNECTED";
      break;
    case MQTT_CONNECTED:
      return "MQTT_CONNECTED";
      break;
    case MQTT_CONNECT_BAD_PROTOCOL:
      return "MQTT_CONNECT_BAD_PROTOCOL";
      break;
    case MQTT_CONNECT_BAD_CLIENT_ID:
      return "MQTT_CONNECT_BAD_CLIENT_ID";
      break;
    case MQTT_CONNECT_UNAVAILABLE:
      return "MQTT_CONNECT_UNAVAILABLE";
      break;
    case MQTT_CONNECT_BAD_CREDENTIALS:
      return "MQTT_CONNECT_BAD_CREDENTIALS";
      break;
    case MQTT_CONNECT_UNAUTHORIZED:
      return "MQTT_CONNECT_UNAUTHORIZED";
      break;
    default:
      return "UNKNOWN STATE!";
      break;
  }
}

//
//
//
boolean test_wifi_and_mqtt_state(){
  boolean ok = true;
  if (mqttClient.state()!=MQTT_CONNECTED) {
    ok = false;
    Serial.print(" !mqtt is not connected but in state ");
    Serial.print(mqttClient.state());
    Serial.print(" == ");
    Serial.println(getMqttSatusString());
    if( WiFi.status() != WL_CONNECTED){
       Serial.println(" !wifi is offline");
       apFound=false;
       changeStatus(NO_WIFI);
       scanAp();
    }else{
      changeStatus(IP_OK);
      ok = mqtt_connect();
      if(publishCount==0){
        readDht();
        if(valuesOk){
          doPublish();
        }
      }
    }
  }
  return ok;
}



//
// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
//
boolean mqtt_connect() {
  int8_t ret;

  // Stop if already connected.
  Serial.print("  mqtt_connect start; client state: ");
  Serial.println(mqttClient.state());
  if (mqttClient.state()==MQTT_CONNECTED) {
    return true;
  }
  changeStatus(IP_OK);

  // set system time
  uint8_t retries = 3;
  boolean ok=false;
  while (!ok && retries>0){
    ok = setTime(2, -(retries-3));
    delay(2000);
    retries--;
  }

  // load certificates
  #ifdef USE_TLS
    loadCerts();
  #endif

  /*Serial.print("  connecting to secure server ");
  Serial.print(AIO_SERVER);
  Serial.print(":");
  Serial.print(AIO_SERVERPORTSECURE);
  Serial.print("... ");

  uint8_t retries = 3;
  int res;
  while (( res = wifiClient.connect(AIO_SERVER, AIO_SERVERPORTSECURE)) == 0) { // wifi client connect will return 0 if success
       Serial.print(" #### !! wifi client connect error: ");
       Serial.print(res);
       Serial.println(" !!");
       Serial.println("  retrying wifi connection in 5 seconds...");
       unsigned long msecLimit = millis() + 5000;
       while(baseAction() < msecLimit){
          delay(100);
       }
       retries--;
       if (retries == 0) {
         return false;
       }
  }
  Serial.println(" ok");
  //wifiClient.stop();
  */

  Serial.print("  connecting to secure mqtt client ");
  Serial.print(AIO_SERVER);
  Serial.print(":");
  Serial.print(AIO_SERVERPORTSECURE);
  Serial.print("... ");
  retries = 3;
  while (!mqttClient.connect((char*) clientId.c_str(), AIO_USERNAME, AIO_PASSWORD)) { // mqtt client connect will return true if success
       Serial.print("  !! MQTT connect error: ");
       Serial.print(mqttClient.state());
       Serial.println(" !!");
       Serial.println("  retrying MQTT connection in 5 seconds...");
       unsigned long msecLimit = millis() + 5000;
       while(baseAction() < msecLimit){
          delay(100);
       }
       retries--;
       if (retries == 0) {
         return false;
       }
  }
  //Serial.print("  mqtt_connect end: client state:");
  //Serial.println(mqttClient.state());
  Serial.println(" connected !");
  changeStatus(MQTT_OK);
  return true;
}

//
// publish something to MQTT
//
boolean doPublish(){
  if (mqttClient.state()!=MQTT_CONNECTED){
    return false;
  }

  // heap
  String heap = getFreeHeap();
  Serial.print("  publishing free mem[");
  Serial.print(publishCount);
  Serial.print("]:");
  Serial.print(heap.c_str());
  if (! mqttClient.publish(fullTopicMem.c_str(), heap.c_str()) ) {
    Serial.println(F(" Failed !!"));
  } else {
    Serial.println(F(" OK"));
  }
  baseAction();

  // uptime
  Serial.print("  publishing uptime[");
  Serial.print(publishCount);
  Serial.print("]:");
  Serial.print(uptime);
  if (! mqttClient.publish(fullTopicUp.c_str(), String(uptime).c_str()) ) {
    Serial.println(F(" Failed !!"));
  } else {
    Serial.println(F(" OK"));
  }
  baseAction();
  
  // temp
  Serial.print("  publishing temp[");
  Serial.print(publishCount);
  Serial.print("]:");
  Serial.print(temp);
  if (! mqttClient.publish(fullTopicTemp.c_str(), String(temp).c_str()) ) {
    Serial.println(F(" Failed !!"));
  } else {
    Serial.println(F(" OK"));
  }
  baseAction();
  
  // humi
  Serial.print("  publishing humi[");
  Serial.print(publishCount);
  Serial.print("]:");
  Serial.print(humi);
  if (! mqttClient.publish(fullTopicHumi.c_str(), String(humi).c_str()) ) {
    Serial.println(F(" Failed !!"));
  } else {
    Serial.println(F(" OK"));
  }
  baseAction();
  publishCount++;
  return true;
}

//
// mqtt callback function for arriving messages
//
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

//
// use an access point
//
boolean useAp(const char *ssid, const char *password){
  changeStatus(AP_FOUND);
  Serial.println();
  Serial.print("   connecting to ");
  Serial.print(ssid);
  Serial.print(" with password **** :");
  //Serial.print(password);
  Serial.print(" ");
  int limit=15;
  WiFi.begin(ssid, password);
  while ((WiFi.status() != WL_CONNECTED) && (limit > 0)) {
    unsigned long msecLimit = millis() + 1000;
    while(baseAction() < msecLimit){
       delay(100);
       //Serial.println("  wifi waiting loop");
    }
    Serial.print("_-");
    limit--;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("    WiFi connected");
    delay(10);
    apUsed=ssid;
    return true;
  }else if (limit==0){
    Serial.println("");
    Serial.println("    too many WiFi retry");
    delay(10);
    return false;
  }
}


//
// scan access points and connect to known one
//
void scanAp(){
  int n = WiFi.scanNetworks();
  Serial.println("  scan done");
  if (n == 0)
    Serial.println("  no networks found");
  else
  {
    Serial.print("  ");
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n && !apFound; ++i)
    {
      // Print SSID and RSSI for each network found
      Serial.print(" ");
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print("): ");
      if(WiFi.encryptionType(i) == ENC_TYPE_NONE){
        Serial.println("open");
      }else{
        Serial.println(WiFi.encryptionType(i));
      }
      for(int j = 0; j < apCount; ++j){
        baseAction();
        Serial.print(" ");
        Serial.print(" testing known AP[");
        Serial.print(j);
        Serial.print("]:");
        Serial.print(ssid[j]);
        Serial.print(" VS ");
        Serial.print(WiFi.SSID(i));
        if(WiFi.SSID(i)==ssid[j]){
          Serial.println("  known AP found !!");
          apFound = useAp(ssid[j], pass[j]);
          if(apFound){
            changeStatus(IP_OK);
            break;
          }
        }else{
          Serial.println();
        }
      }
    }
  }
  Serial.println("");
}


//
// base loop action: handle led blinking
//
unsigned long baseAction(){
    //
    mqttClient.loop();
    
    unsigned long currentMillis = millis();
    if(currentMillis - previousBaseLoopInterval > baseLoopInterval*status){
      previousBaseLoopInterval = currentMillis;
      // togle led
      if (toggle) {
        if(useOnBoardLed){
          digitalWrite(LED_BLUE, HIGH);
        }else{
          digitalWrite(LED, HIGH);
        }
        toggle = false;
      } else {
        if(useOnBoardLed){
          digitalWrite(LED_BLUE, LOW);
        }else{
          digitalWrite(LED, LOW);
        }
        toggle = true;
      }
    }
    return currentMillis;
}


//
// main loop
//
void loop(){
    //unsigned long currentMillis = millis();
    unsigned long currentMillis = baseAction();
    if(publishCount==0){
      test_wifi_and_mqtt_state();
    }
    if(currentMillis - previousLoopMillis > apLoopInterval) {
      previousLoopMillis = currentMillis;
      test_wifi_and_mqtt_state();
    }else if(currentMillis - previousLoopMillis <0){
      Serial.println("### millisecond ROLLOVER !!!");
      previousLoopMillis=0;
      previousPublishMillis=0;
      previousPingMillis=0;
    }
    if(currentMillis - previousPublishMillis > publishInterval) {
      previousPublishMillis = currentMillis;
      getUptime();
      readDht();
      if(valuesOk){
        doPublish();
      }else{
        Serial.println(" don't publish because no DHT data readed");
      }
    }
}
