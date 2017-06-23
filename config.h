// MQTT settings
#define AIO_SERVER      "MQTT_BROKER_ADDRESS"
#define AIO_SERVERPORT  7906
#define AIO_SERVERPORTSECURE  7901
#define AIO_USERNAME    "MQTT_USER"
#define AIO_PASSWORD    "MQTT_PASSWORD"
#define AIO_CERT_FILE "/client_certificate.crt.der"
#define AIO_KEY_FILE "/client_certificate.key.der"
#define AIO_CERT_FINGERPRINT "xx:xx:xx....."
// MQTT topic
const char *MQTT_TOPIC_PREFIX = "mobile/esp_";

// Access Points
const char *ssid[1] = {"AP_1"};
const char *pass[1] = {"password_AP1"};
const int apCount = 1;

// LED
#define LED 0 // GPIO 0
#define LED_BLUE 2  // onboard blue led
boolean useOnBoardLed=true;

// DHT sensor
#define DHTPIN 4        // GPIO 4 == D2
#define DHTTYPE DHT11   // DHT 11

