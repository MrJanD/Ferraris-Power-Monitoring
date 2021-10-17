#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <iostream>
#include <string>
#include<bits/stdc++.h>

#ifdef __AVR__
#include <avr/power.h>
#endif

#define MQTT_CONN_KEEPALIVE 300

String buildDateTime = __TIMESTAMP__;

// OTA Variables
const int port = 80;
const char* updatePath = "/firmware";
const char* otaUser = "";
const char* otaPassword = "";
ESP8266WebServer httpServer(port);
ESP8266HTTPUpdateServer httpUpdater;

// WiFi settings
const char* ssid = "";
const char* password = "";
WiFiClient client;
int32_t rssi = 0;
unsigned long publishRssiAt = 0;
unsigned long rssiCycleTime = 600000;

// MQTT variables
const char* mqttServer = "";
uint16_t mqttPort = 1883;
// UNIQUE ID MUST BE CHANGED FOR EACH NEW DEVICE IN YOUR NETWORK!
const char* host = "FerrisReader";
// The base topic for MQTT messages
String Topic = "Basement/FerrisReader/";
PubSubClient pubSubClient(client);

bool publishHealth = false;
bool publishConsumption = false;

// Defines the maximum consumption. Values above are handled as outliers and are discarded.
float maxCurrentConsumption = 20 * 3 * 230;

// The infra red sensor sample interval in ms
int irSampleInterval = 10;

// An consumption update will be published on th MQTT broker if the consumption measured is
// higher or lower than 100% +/- deviationPercentage.
int deviationPercentage = 5;

// The turns it takes for the power meter for each consumed kWh.
uint8_t turnsPerkWh = 120;

// If some essential values are of particular interest for debuggin purpose, they will be
// published if set to true.
bool publishDebug = false;

// Latter variables are not that relevant.
unsigned long irSampleAt = 0;
unsigned long lastFerrisTurn = millis();
bool ferrisRed = false;
std::array<int, 200> ringBuffer;
int currentRingBufferIndex = 0;
float currentConsumption = 0;
float lastPublishedConsumption = 0;
uint8_t highValueCounter = 0;
uint8_t lowValueCounter = 0;
bool highState = false;
bool arrayFull = false;
bool debug = false;
int analogValue = 0;
int processingTime = 0;
float runningAverage = 0;
uint ferrisCounter = 0;
uint8_t thresholdHigh = 3;
uint8_t thresholdLow = 10;

void setup()
{
    Serial.begin(115200);
    delay(10);

    manageWifi();

    // Print the addresses
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());

    httpUpdater.setup(&httpServer, updatePath, otaUser, otaPassword);
    httpServer.begin();

    String updateSite = "http://" + WiFi.localIP().toString() + ":" + port + updatePath;

    Serial.print("HTTPUpdateServer ready! Open ");
    Serial.print(updateSite);
    Serial.print(" with username '");
    Serial.print(otaUser);
    Serial.print("' and password '");
    Serial.print(otaPassword);
    Serial.println("'");

    pubSubClient.setServer(mqttServer, mqttPort);
    pubSubClient.setCallback(callback);
    pubSubConnect();

    pubSubClient.publish((Topic + "FirmwarePath").c_str(), updateSite.c_str(), true);
    pubSubClient.publish((Topic + "User").c_str(), otaUser, true);
    pubSubClient.publish((Topic + "Password").c_str(), otaPassword, true); // ATTENTION: Clear text passwords should only be sent over secured connections!
    pubSubClient.publish((Topic + "IP").c_str(), WiFi.localIP().toString().c_str(), true);
    pubSubClient.publish((Topic + "Availability").c_str(), "ONLINE", true);
    pubSubClient.publish((Topic + "Build").c_str(), buildDateTime.c_str(), true);
    pubSubClient.publish((Topic + "Topic").c_str(), Topic.c_str(), true);
}

void loop()
{
    manageWifi();
    httpServer.handleClient();

    pubSubConnect();
    pubSubStats();

    timeServant();
}

float getStableMedian()
{
    std::array<int, 200> sortedRingBuffer = ringBuffer;
    std::sort(sortedRingBuffer.begin(), sortedRingBuffer.end());
    int mid = sortedRingBuffer.size() / 2;

    return (
        sortedRingBuffer[mid + 10]
        + sortedRingBuffer[mid - 10]
        + sortedRingBuffer[mid + 20]
        + sortedRingBuffer[mid - 20]
        + sortedRingBuffer[mid + 30]
        + sortedRingBuffer[mid - 30]
        + sortedRingBuffer[mid + 40]
        + sortedRingBuffer[mid - 40]
        ) / 8.0;
}

float getSimpleAverage()
{
    float sum = 0;
    for (int i = 0; i < ringBuffer.size(); i++) {
        sum += ringBuffer[i];
    }
    return (sum / ringBuffer.size());
}

void sampleFerris()
{
    analogValue = analogRead(A0);

    if (currentRingBufferIndex >= ringBuffer.size())
    {
        currentRingBufferIndex = 0;
        arrayFull = true;
    }

    ringBuffer[(currentRingBufferIndex++)] = analogValue;

    runningAverage = getStableMedian();

    if (analogValue >= runningAverage + 2.5)
    {
        lowValueCounter = 0;
        highValueCounter++;

        if ((highValueCounter >= thresholdHigh) && !highState)
        {
            unsigned long currentTime = millis();
            currentConsumption = ((3600 * 1000 * 1000) / (turnsPerkWh * (currentTime - lastFerrisTurn)));

            if (currentConsumption <= maxCurrentConsumption)
            {
                highState = true;
                ferrisCounter++;
                publishConsumption = ((abs(currentConsumption - lastPublishedConsumption) >= (lastPublishedConsumption * deviationPercentage / 100)) && arrayFull && (lastFerrisTurn != 0)) ? true : false;
                lastFerrisTurn = currentTime;
            }
        }
    }
    else
    {
        highValueCounter = 0;
        lowValueCounter++;

        if ((lowValueCounter >= thresholdLow) && highState)
        {
            highState = false;
        }
    }
}

void timeServant()
{
    unsigned long currentTime = millis();

    if (currentTime >= publishRssiAt)
    {
        rssi = WiFi.RSSI();
        publishHealth = true;
        publishRssiAt += rssiCycleTime;
    }

    if (currentTime >= irSampleAt)
    {
        if (debug)
        {
            ulong start = millis();
            sampleFerris();
            processingTime = (int)(millis() - start);
            publishDebug = true;
        }
        else
        {
            sampleFerris();
        }

        irSampleAt += irSampleInterval;
    }
}

void manageWifi()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        WiFi.mode(WIFI_STA);

        WiFi.begin(ssid, password);

        while (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi Error Code: " + WiFi.status());
            delay(500);
        }

        Serial.println("WiFi connected");
    }
}

void pubSubConnect()
{
    if (!pubSubClient.connected())
    {
        if (pubSubClient.connect(host, (Topic + "Availability").c_str(), 0, true, "OFFLINE"))
        {
            pubSubClient.subscribe((Topic + "set").c_str());
            Serial.println("connected");
        }
    }
    pubSubClient.loop();
}

void callback(char* topic, byte* payload, unsigned int length)
{
    if (strcmp(topic, (Topic + "set").c_str()) == 0)
    {
        std::string payloadString = "";
        for (int i = 0; i < length; i++) {
            payloadString += (char)payload[i];
        }

        std::transform(payloadString.begin(), payloadString.end(), payloadString.begin(),
            [](unsigned char c) { return std::tolower(c); });

        std::string delimiter = " ";
        size_t pos = payloadString.find(delimiter);
        std::string key = payloadString.substr(0, pos);
        int value = atoi(payloadString.substr(pos + delimiter.length(), length).c_str());
        Serial.println((key + " Received").c_str());

        if (key == "interval")
        {
            Serial.println("interval");
            irSampleInterval = value;
        }
        else if (key == "debug")
        {
            debug = !debug;
        }
        else if (key == "deviationPercentage")
        {
            deviationPercentage = value;
        }
    }
}

void pubSubStats()
{
    if (pubSubClient.connected())
    {
        if (publishHealth)
        {
            char rssiVal[4];
            sprintf(rssiVal, "%d", rssi);
            if (pubSubClient.publish((Topic + "rssi").c_str(), rssiVal))
            {
                Serial.println("RSSI published");
                publishHealth = false;
            }
            else
            {
                Serial.println("Could not publish RSSI");
            }

            if (pubSubClient.publish((Topic + "Availability").c_str(), "ONLINE", true))
            {
                Serial.println("Avalability published");
                publishHealth = false;
            }
            else
            {
                Serial.println("Could not publish availability");
            }
        }

        if (publishConsumption)
        {
            char cc[8];
            sprintf(cc, "%.2f", currentConsumption);

            if (pubSubClient.publish((Topic + "PowerConsumption").c_str(), cc))
            {
                lastPublishedConsumption = currentConsumption;
                publishConsumption = false;
            }

            char tc[8];
            sprintf(tc, "%.2f", ferrisCounter / (float)turnsPerkWh);

            if (pubSubClient.publish((Topic + "TotalConsumption").c_str(), tc))
            {
                lastPublishedConsumption = currentConsumption;
                publishConsumption = false;
            }
        }

        if (publishDebug)
        {
            char analogChar[4];
            sprintf(analogChar, "%d", analogValue);
            char processingTimeChar[4];
            sprintf(processingTimeChar, "%d", processingTime);
            char avg[6];
            sprintf(avg, "%.1f", runningAverage);

            pubSubClient.publish((Topic + "runningAvg").c_str(), avg);
            pubSubClient.publish((Topic + "a0").c_str(), analogChar);
            pubSubClient.publish((Topic + "processingTime").c_str(), processingTimeChar);

            publishDebug = false;
        }
    }
}
