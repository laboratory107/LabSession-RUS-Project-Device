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

#define HOST ".."                  //[Azure IoT host name].azure-devices.net
#define DEVICE_KEY ".." // Azure Primary key for device
#define TOKEN_DURATION 60

const char *azureHost = HOST;
const char *deviceId = "..";

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

void connectToWiFi()
{
  Logger.Info("Connecting to WIFI SSID " + String(labSSID));

  wifiClient.setCACert((const char *)ca_pem);

  WiFi.mode(WIFI_STA);
  WiFi.begin(labSSID, labPass);
  Logger.Info("Connecting");

  short timeoutCounter = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);

    timeoutCounter++;
    if (timeoutCounter >= 20)
      ESP.restart();
  }

  Logger.Info("WiFi connected, IP address: " + WiFi.localIP().toString());
}

void mqttReconnect()
{
  while (!mqttClient.connected())
  {
    Logger.Info("Attempting MQTT connection...");
    const char *mqttPassword = (const char *)az_span_ptr(sasToken.Get());
    Logger.Info(mqttClientId);
    Logger.Info(mqttPassword);
    Logger.Info(mqttUsername);
    Logger.Info(String(mqttPort));
    if (mqttClient.connect(mqttClientId, mqttUsername, mqttPassword))
    {
      Logger.Info("MQTT connected");
      mqttClient.subscribe(mqttC2DTopic);
    }
    else
    {
      Logger.Info("Trying again in 5 seconds");
      delay(5000);
    }
  }
}

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

void callback(char *topic, byte *payload, unsigned int length)
{
  payload[length] = '\0';
  String message = String((char *)payload);

  Logger.Info("Callback:" + String(topic) + ": " + message);
}

void initializeTime() // MANDATORY or SAS tokens won't generate
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

bool connectMQTT()
{
  mqttClient.setBufferSize(1024);

  Logger.Info("Generating SAS token");
  if (sasToken.Generate(TOKEN_DURATION) != 0)
  {
    Logger.Error("Failed generating SAS token");
    return false;
  }
  else
    Logger.Info("SAS token generated");

  mqttClient.setServer(mqttBrokerURI, mqttPort);
  mqttClient.setCallback(callback);

  return true;
}

void setup()
{
 connectToWiFi();
  initializeTime();

  if (initIoTHub())
  {
    connectMQTT();
    delay(200);
    mqttReconnect();
    delay(200);
  }
  char topic[200];
  
  az_iot_hub_client_telemetry_get_publish_topic(&client,NULL, topic, 200, NULL );
  Logger.Info(String(topic));
  mqttClient.publish(topic,"Device001");//https://github.com/Azure/azure-iot-explorer/releases
  Logger.Info("Setup done");
}

void loop()
{
  if (!mqttClient.connected())
    mqttReconnect();
  if (sasToken.IsExpired())
    connectMQTT();

  mqttClient.loop();
}
