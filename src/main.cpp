#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <Wire.h>
#include <AM2320.h>
#include <SoftwareSerial.h>
#include <MHZ.h>
#include <base64.h>

#include "./secret.h"

#define MIC_ARR_SIZE 1024
#define MIC_DC_OFFSET 780

#define AM2320_INTERVAL 500
#define MIC_INTERVAL 20
#define POST_INTERVAL 1000

#define ANALOG_PIN A0
#define MUX_RST_PIN D6
#define MUX_A_CH_SELECT D5
#define AM2320_SCL_PIN D1
#define AM2320_SDA_PIN D2
#define MHZ_TX D5
#define MHZ_RX D8

//#define DEBUG_LOG

AM2320 sensor;
MHZ co2_sensor(MHZ_RX, MHZ_TX, MHZ14A);

int smoothMicArr[MIC_ARR_SIZE] = {MIC_DC_OFFSET};

WiFiClientSecure client;
String auth;

const char* HOST = "simsva.se";
const char* PATH = "/api/aidb/add_data";
const int PORT = 443;
const char FINGERPRINT[] PROGMEM = "CE 11 C9 02 AF 21 4F 7E DA 4E A4 94 42 17 7A B5 82 65 B3 DA";

// Data
int volume = -1024, light = -1024, co2 = -1024;
float temp = -1024.0f, humidity = -1024.0f;

time_t am2320_last_update, co2_last_update, mic_last_update, last_post;

void add_data(int volume=-1024, int light=-1024, int co2=-1024, float temp=-1024.0f, float humidity=-1024.0f) {
    String data;
    if(volume != -1024) { data += "volume="; data += volume; data += '&'; }
    if(co2 != -1024) { data += "co2="; data += co2; data += '&'; }
    if(light != -1024) { data += "light="; data += light; data += '&'; }
    if(temp != -1024.0) { data += "temp="; data += temp; data += '&'; }
    if(humidity != -1024.0) { data += "humidity="; data += humidity; }
#ifdef DEBUG_LOG
    Serial.println(data);
#endif

    if(!client.connected()) {
        Serial.print("Connecting to host");
        int r = 0; // Retries
        while(!client.connect(HOST, PORT) && r < 30) {
            delay(100);
            Serial.print('.');
            r++;
        }
        Serial.println();
        if(r == 30) {
            Serial.println("Connection failed!");
        } else {
            Serial.println("Connected!");
        }
    }

    client.printf("POST %s HTTP/1.1\n", PATH);
    client.printf("Host: %s\n", HOST);
    client.printf("Authorization: Basic %s\n", auth.c_str());
    client.println("Content-Type: application/x-www-form-urlencoded");
    client.printf("Content-Length: %i\n", data.length());
    client.println();
    client.println(data);

    // Wait for response
    String line;
    while (client.connected()) {
        line = client.readStringUntil('\n');
#ifdef DEBUG_LOG
        Serial.println(line);
#endif
        if (line == "\r") break;
    }
    while(client.available()){
        line = client.readStringUntil('\n');
#ifdef DEBUG_LOG
        Serial.println(line);
#endif
    }
}

void addMicInput(int val)
{
    for(int i = MIC_ARR_SIZE - 1; 0 < i; i--)
    {
        smoothMicArr[i] = smoothMicArr[i - 1];
    }
    smoothMicArr[0] = val;
}

int smoothMic() {
    long unsigned int sum = 0;
    for(int i = 0; i < MIC_ARR_SIZE; i++)
    {
        sum += (smoothMicArr[i] - MIC_DC_OFFSET) * (smoothMicArr[i] - MIC_DC_OFFSET) * (((float) MIC_ARR_SIZE - i) / (float) MIC_ARR_SIZE);
    }
    return sum / sqrt(sum);
}

int readChannel(bool channel)
{
    digitalWrite(MUX_RST_PIN, true);
    delay(1);
    digitalWrite(MUX_A_CH_SELECT, channel);
    delay(1);
    digitalWrite(MUX_RST_PIN, false);
    delay(1);

    int analogVal = analogRead(ANALOG_PIN);
    return analogVal;
}

void setup()
{
    Serial.begin(921600);
    while(!Serial) {
        delay(10);
    }
    pinMode(MUX_RST_PIN, OUTPUT);
    pinMode(MUX_A_CH_SELECT, OUTPUT);
    sensor.begin(AM2320_SDA_PIN, AM2320_SCL_PIN);

    if(co2_sensor.isPreHeating()) {
        Serial.println("Waiting for pre-heat");
        while(co2_sensor.isPreHeating()) {
            Serial.println(millis() / 1000);
            delay(1000);
        }
        Serial.println("Pre-heat done!");
    }

    WiFi.begin(WIFI_SSID, WIFI_PASS);

    Serial.print("Connecting");
    while(WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }
    Serial.println();

    Serial.print("Connected, IP address: ");
    Serial.println(WiFi.localIP());

    Serial.print("MAC address: ");
    String mac = WiFi.macAddress();
    for(int i = 14; i > 1; i -= 3) mac.remove(i, 1);
    Serial.println(mac);

    Serial.print("Auth: ");
    auth = base64::encode(mac + ":" + API_SECRET);
    Serial.println(auth);

    Serial.print("Fingerprint: ");
    Serial.println(FINGERPRINT);
    client.setFingerprint(FINGERPRINT);
    client.setTimeout(15000);
}

void loop()
{
    if(millis() > am2320_last_update + AM2320_INTERVAL) {
        sensor.measure();
        temp = sensor.getTemperature();
        humidity = sensor.getHumidity();
        am2320_last_update = millis();
    }
    if(millis() > mic_last_update + MIC_INTERVAL) {
        addMicInput(readChannel(1));
        mic_last_update = millis();
    }
    if(millis() > last_post + POST_INTERVAL) {
        volume = smoothMic();
        light = readChannel(0);
        co2 = co2_sensor.readCO2UART();
        if(co2 <= 0) co2 = -1024;

        time_t start = millis();
        Serial.printf("plot: %8i, %8i, %8i, %8.1f, %8.1f\n", volume, light, co2, temp, humidity);
        add_data(volume, light, co2, temp, humidity);
        Serial.printf("Done: %lims\n", (time_t)millis() - start);

        volume = light = co2 = -1024;
        temp = humidity = -1024.0f;
        last_post = millis();
    }
}
