#include <Arduino.h>
#include <AzIoTSasToken.h>
#include <SerialLogger.h>
#include <WiFi.h>
#include <az_core.h>
#include <azure_ca.h>
#include <ctime>
#include "WiFiClientSecure.h"

#include "ArduinoJson.h"
#include "DHTesp.h"
#include "PubSubClient.h"

/* Azure auth data */

#define HOST ".."		 //[Azure IoT host name].azure-devices.net
#define DEVICE_KEY ".."	 // Azure Primary key for device
#define TOKEN_DURATION 60

/* MQTT data for IoT Hub connection */
const char* mqttBrokerURI = HOST;  // MQTT host = IoT Hub link
const int mqttPort = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;	// Secure MQTT port
const char* mqttC2DTopic = AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC;	// Topic where we can receive cloud to device messages

// These three are just buffers - actual clientID/username/password is generated
// using the SDK functions in initIoTHub()
char mqttClientId[128];
char mqttUsername[128];
char mqttPassword[200];
char publishTopic[200];

/* Auth token requirements */

uint8_t sasSignatureBuffer[256];  // Make sure it's of correct size, it will just freeze otherwise :/

az_iot_hub_client client;
AzIoTSasToken sasToken(
	&client, AZ_SPAN_FROM_STR(DEVICE_KEY),
	AZ_SPAN_FROM_BUFFER(sasSignatureBuffer),
	AZ_SPAN_FROM_BUFFER(
		mqttPassword));	 // Authentication token for our specific device

const char* deviceId = "..";  // Device ID as specified in the list of devices on IoT Hub

/* Pin definitions and library instance(s) */
const int lightPin = 35;
const int dhtPin = 23; // It used to be tied to 21, but 21 and 22 are default pins for I2C communication (for OLED screens eg.), so we will leave them unused  

const int buttonHappyPin = 2;
const int buttonSadPin = 4;
const int buttonNeutralPin = 5;
char lastSentimentStatus = 'N';
char neutralStatus = 'N', happyStatus = 'H', sadStatus = 'S';

DHTesp dht;

/* WiFi things */

WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

const String deviceID = "Dev001";

const char* ssid = "";
const char* pass = "";
short timeoutCounter = 0;

void setupWiFi() {
	Logger.Info("Connecting to WiFi");

	wifiClient.setCACert((const char*)ca_pem); // We are using TLS to secure the connection, therefore we need to supply a certificate (in the SDK)

	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, pass);

	while (WiFi.status() != WL_CONNECTED) { // Wait until we connect...
		Serial.print(".");
		delay(500);

		timeoutCounter++;
		if (timeoutCounter >= 20) ESP.restart(); // Or restart if we waited for too long, not much else can you do
	}

	Logger.Info("WiFi connected");
}

void IRAM_ATTR buttonISR(void* status) { // We will use a single ISR to handle all three buttons and will change the current status depending on the argument that's passed in
	char* statusPtr = (char*)status;  // Convert void pointer to a char pointer
	char statusValue = *statusPtr;	 // Get the value that the pointer points to

	lastSentimentStatus = statusValue;
}

void setupButtonInterrupts() { // We will be setting up all the buttons in PULLUP state (so that their default state will be HIGH) and then attaching a FALLING interrupt when they are actually clicked
	pinMode(buttonHappyPin, INPUT_PULLUP);
	pinMode(buttonSadPin, INPUT_PULLUP);
	pinMode(buttonNeutralPin, INPUT_PULLUP);

	attachInterruptArg(buttonHappyPin, buttonISR, &happyStatus, FALLING); // Each button gets attached to the same ISR (fired when the voltage starts falling = button is pressed)
	attachInterruptArg(buttonSadPin, buttonISR, &sadStatus, FALLING); // A char argument is passed to the ISR ('S', 'N' or 'H') 
  attachInterruptArg(buttonNeutralPin, buttonISR, &neutralStatus, FALLING);
}

void initializeTime() {	 // MANDATORY or SAS tokens won't generate
  Logger.Info("Setting time using SNTP");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(NULL);
  std::tm tm{};
  tm.tm_year = 2023; // Define a date on 1.1.2023. and wait until the current time has the same year (by default it's 1.1.1970.)

  while (now < std::mktime(&tm)) // Since we are using an Internet clock, it may take a moment for clocks to sychronize
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
}

void setupDHTSensor() { // NOTE: change to DHT11 if you are using the blue temp/humidity sensor
  dht.setup(dhtPin, DHTesp::DHT22);
}

void setupLightSensor() {
  	pinMode(lightPin, INPUT);
}

// MQTT is a publish-subscribe based, therefore a callback function is called whenever something is published on a topic that device is subscribed to
void callback(char *topic, byte *payload, unsigned int length) { 
  payload[length] = '\0'; // It's also a byte-safe protocol, therefore instead of transfering text, bytes are transfered and they aren't null terminated - so we need ot add \0 to terminate the string
  String message = String((char *)payload); // After it's been terminated, it can be converted to String

  Logger.Info("Callback:" + String(topic) + ": " + message);
}

bool connectMQTT() {
  mqttClient.setBufferSize(1024); // The default size is defined in MQTT_MAX_PACKET_SIZE to be 256 bytes, which is too small for Azure MQTT messages, therefore needs to be increased or it will just crash without any info

  Logger.Info("Generating SAS token");
  if (sasToken.Generate(TOKEN_DURATION) != 0) // SAS tokens need to be generated in order to generate a password for the connection
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

void mqttReconnect() {
  while (!mqttClient.connected()) {
    Logger.Info("Attempting MQTT connection...");
    const char *mqttPassword = (const char *)az_span_ptr(sasToken.Get()); // Just in case that the SAS token has been regenerated since the last MQTT connection, get it again
    
    if (mqttClient.connect(mqttClientId, mqttUsername, mqttPassword)) { // Either connect or wait for 5 seconds (we can block here since without IoT Hub connection we can't do much)
      Logger.Info("MQTT connected");
      mqttClient.subscribe(mqttC2DTopic); // If connected, (re)subscribe to the topic where we can receive messages sent from the IoT Hub 
    } else {
      Logger.Info("Trying again in 5 seconds");
      delay(5000);
    }
  }
}

int getLightValue() { // Get the light value by reading the voltage on the light pin using analogRead()
  int lightSensorPin = lightPin;
  int lightValue = analogRead(lightSensorPin); // More light = lower voltage = lower value
  return map(lightValue, 0, 4096, 100, 0); // analogRead() returns values in range 0-4096, therefore we are scalling to 0-100
}

StaticJsonDocument<128> doc; // Create a JSON document we'll reuse to serialize our data into JSON
String output = "";

String getTelemetryData() { // Get the data and pack it in a JSON message
	doc["Sentiment"]["Status"] = (String)lastSentimentStatus;

	JsonObject Ambient = doc.createNestedObject("Ambient");
	Ambient["Temperature"] = dht.getTemperature();
	Ambient["Humidity"] = dht.getHumidity();
	Ambient["Light"] = getLightValue();

	doc["DeviceID"] = deviceID;

	serializeJson(doc, output);

	Logger.Info(output);
  return output;
}

void sendTelemetryData() {
  String telemetryData = getTelemetryData();
  mqttClient.publish(publishTopic, telemetryData.c_str());
}

long lastTime, currentTime = 0;
int interval = 5000;
void checkTelemetry() { // Do not block using delay(), instead check if enough time has passed between two calls using millis() 
  currentTime = millis();

  if (currentTime - lastTime >= interval) { // Subtract the current elapsed time (since we started the device) from the last time we sent the telemetry, if the result is greater than the interval, send the data again
    Logger.Info("Sending telemetry...");
    sendTelemetryData();

    lastTime = currentTime;
  }
}

void sendTestMessageToIoTHub() {
  az_result res = az_iot_hub_client_telemetry_get_publish_topic(&client, NULL, publishTopic, 200, NULL ); // The receive topic isn't hardcoded and depends on chosen properties, therefore we need to use az_iot_hub_client_telemetry_get_publish_topic()
  Logger.Info(String(publishTopic));
  
  mqttClient.publish(publishTopic, deviceID.c_str()); // Use https://github.com/Azure/azure-iot-explorer/releases to read the telemetry
}

bool initIoTHub() {
  az_iot_hub_client_options options = az_iot_hub_client_options_default(); // Get a default instance of IoT Hub client options

  if (az_result_failed(az_iot_hub_client_init( // Create an instnace of IoT Hub client for our IoT Hub's host and the current device
          &client,
          az_span_create((unsigned char *)HOST, strlen(HOST)),
          az_span_create((unsigned char *)deviceId, strlen(deviceId)),
          &options)))
  {
    Logger.Error("Failed initializing Azure IoT Hub client");
    return false;
  }

  size_t client_id_length;
  if (az_result_failed(az_iot_hub_client_get_client_id(
          &client, mqttClientId, sizeof(mqttClientId) - 1, &client_id_length))) // Get the actual client ID (not our internal ID) for the device
  {
    Logger.Error("Failed getting client id");
    return false;
  }

  size_t mqttUsernameSize;
  if (az_result_failed(az_iot_hub_client_get_user_name(
          &client, mqttUsername, sizeof(mqttUsername), &mqttUsernameSize))) // Get the MQTT username for our device
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

void setup() {
  setupWiFi();
  initializeTime();

  if (initIoTHub()) {
    connectMQTT();
    delay(200);
    mqttReconnect();
    delay(200);
  }

  setupLightSensor();
	setupDHTSensor();
	setupButtonInterrupts();

  sendTestMessageToIoTHub();
  Logger.Info("Setup done");
}


void loop() { // No blocking in the loop, constantly check if we are connected and gather the data if necessary
	if (!mqttClient.connected()) {
    mqttReconnect();
  }

  if (sasToken.IsExpired()) {
    connectMQTT();
  }

  mqttClient.loop();
  checkTelemetry();
}