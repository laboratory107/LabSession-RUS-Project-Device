#include <Arduino.h>
#include <az_core.h>
#include <az_iot.h>
#include "WiFiClient.h"
#include "WiFiClientSecure.h"
#include "WiFi.h"
#include "PubSubClient.h"
#include <ctime>

#include <azure_ca.h>

#include <SerialLogger.h>
#include <AzIoTSasToken.h>


WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

az_iot_hub_client client;

#define HOST "rus22btomas.azure-devices.net"                  //[Azure IoT host name].azure-devices.net
#define DEVICE_KEY "S38l1tt9EHko43+Un7A0d+KKiv/rBNaQKpiP9lh2HA0=" // Azure Primary key for device
#define TOKEN_DURATION 60

const char *azureHost = HOST;
const char *deviceId = "rus22-btomas-esp32";

const char *labSSID = "TheLabIOT";
const char *labPass = "Yaay!ICanTalkNow";

const char *mqttBrokerURI = HOST;
const int mqttPort = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;
const char *mqttC2DTopic = AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC;

char mqttClientId[128];
char mqttUsername[128];
char mqttPassword[200];
uint8_t sasSignatureBuffer[256]; // Make sure it's of correct size, it will just freeze otherwise :/

AzIoTSasToken sasToken(
    &client,
    AZ_SPAN_FROM_STR(DEVICE_KEY),
    AZ_SPAN_FROM_BUFFER(sasSignatureBuffer),
    AZ_SPAN_FROM_BUFFER(mqttPassword));

bool initIoTHub()
{

  az_iot_hub_client_options options = az_iot_hub_client_options_default();

  if (az_result_failed(az_iot_hub_client_init(
          &client,
          az_span_create((unsigned char *)azureHost, strlen(azureHost)),
          az_span_create((unsigned char *)deviceId, strlen(deviceId)),
          &options)))
  {
    Logger.Error("Failed initializing Azure IoT Hub client");
    return false;
  }

  size_t client_id_length;
  if (az_result_failed(az_iot_hub_client_get_client_id(
          &client, mqttClientId, sizeof(mqttClientId) - 1, &client_id_length)))
  {
    Logger.Error("Failed getting client id");
    return false;
  }

  size_t mqttUsernameSize;
  if (az_result_failed(az_iot_hub_client_get_user_name(
          &client, mqttUsername, sizeof(mqttUsername), &mqttUsernameSize)))
  {
    Logger.Error("Failed to get MQTT username ");
    return false;
  }
  Logger.Info("Great success");
  Logger.Info("Client ID: " + String(mqttClientId));
  Logger.Info("Username: " + String(mqttUsername));
  Logger.Info("Password: " + (String)mqttPassword);

  return true;
}

void connectToWiFi()
{
  
}

void mqttReconnect()
{

}
bool connectMQTT()
{
  return true;
}
void callback(char *topic, byte *payload, unsigned int length)
{
}
void initializeTime()
{
  Logger.Info("Setting time using SNTP");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(NULL);
  std::tm tm{};
  tm.tm_year = 2022;

  while (now < std::mktime(&tm))
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
}

void setup() {
  connectToWiFi();
  initializeTime();

  if (initIoTHub())
    connectMQTT();
    //TODO: publish online status
}

void loop() {
  // put your main code here, to run repeatedly:
}