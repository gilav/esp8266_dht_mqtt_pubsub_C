
C code to read DHT11/22 temperature and humidity values and send them to MQTT server
What it does is:
- scan WIFI for known access points
- if AP found, connect wifi, set time using NTP server, connect to MQTT server using TLS and start poling DHT sensor and publish temperature and humidity. 
- publish also free ram and uptime, for test purpose
- blinking LED as in the NodeMcu project.

NOTE: Still don't connect if server require certificate. But at least dont crash..
Using ESP8266 2.3.0 board installed using GIT clone.

Lavaux Gilles 2017/05
