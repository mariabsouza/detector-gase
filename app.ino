#include <WiFi.h>
#include <PubSubClient.h>
#include <DHTesp.h>
#include <MQUnifiedsensor.h>

// Definições para o ESP32
#define placa "ESP-32"
#define Voltage_Resolution 3.3
#define MQ135_PIN 35
#define MQ2_PIN 34
#define MQ7_PIN 32
#define type_MQ135 "MQ-135"
#define type_MQ2 "MQ-2"
#define type_MQ7 "MQ-7"
#define ADC_Bit_Resolution 12
#define RatioMQ135CleanAir 3.6
#define RatioMQ2CleanAir 9.83
#define RatioMQ7CleanAir 27.5

#define LED_PIN 25
#define CO2_LIMIT 1000
#define GLP_LIMIT 200
#define CO_LIMIT 10

const int DHT_PIN = 15;

MQUnifiedsensor MQ135_CO2(placa, Voltage_Resolution, ADC_Bit_Resolution, MQ135_PIN, type_MQ135);
MQUnifiedsensor MQ2(placa, Voltage_Resolution, ADC_Bit_Resolution, MQ2_PIN, type_MQ2);
MQUnifiedsensor MQ7(placa, Voltage_Resolution, ADC_Bit_Resolution, MQ7_PIN, type_MQ7);

DHTesp dht;

const char* ssid = "";
const char* password = "";
const char* mqtt_server = "test.mosquitto.org";

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi conectado");
  Serial.println("Endereço IP: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mensagem recebida [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Tentando realizar a conexão MQTT...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("Conectado!");
    } else {
      Serial.print("Falhou, rc=");
      Serial.print(client.state());
      Serial.println(" tente novamente em 5 segundos");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  dht.setup(DHT_PIN, DHTesp::DHT22);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  initAndCalibrateSensor(MQ135_CO2, 110.47, -2.862, RatioMQ135CleanAir); // CO2
  initAndCalibrateSensor(MQ2, 574.25, -2.222, RatioMQ2CleanAir);        // GLP
  initAndCalibrateSensor(MQ7, 99.042, -1.518, RatioMQ7CleanAir);        // CO
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  if (now - lastMsg > 2000) { 
    lastMsg = now;
    TempAndHumidity data = dht.getTempAndHumidity();

    // Publica temperatura e umidade
    String temp = String(data.temperature, 2);
    client.publish("/Bia/temp", temp.c_str());
    String hum = String(data.humidity, 1);
    client.publish("/Bia/hum", hum.c_str());

    Serial.print("Temperatura: ");
    Serial.println(temp);
    Serial.print("Umidade: ");
    Serial.println(hum);

    double co2 = publishGasReading(MQ135_CO2, "/Bia/co2", "CO2");

    double glp = publishGasReading(MQ2, "/Bia/glp", "GLP");

    double co = publishGasReading(MQ7, "/Bia/co", "CO");

    // Verifica se algum gás está acima dos limites e acende o LED
    if (co2 > CO2_LIMIT || glp > GLP_LIMIT || co > CO_LIMIT) {
      digitalWrite(LED_PIN, HIGH); 
    } else {
      digitalWrite(LED_PIN, LOW);
    }
  }

  delay(500);
}

void initAndCalibrateSensor(MQUnifiedsensor &sensor, float a, float b, float cleanAirRatio) {
  sensor.setRegressionMethod(1); // _PPM = a*ratio^b
  sensor.setA(a);
  sensor.setB(b);
  sensor.init();
  Serial.print("Calibrando ");
  float calcR0 = 0;
  for (int i = 1; i <= 10; i++) {
    sensor.update();
    calcR0 += sensor.calibrate(cleanAirRatio);
    Serial.print(".");
  }
  sensor.setR0(calcR0 / 10);
  Serial.println(" concluído!");

  if (isinf(calcR0)) {
    Serial.println("Aviso: Problema de conexão detectado, R0 é infinito (circuito aberto detectado), por favor verifique a fiação e a alimentação.");
    while (1);
  }

  if (calcR0 == 0) {
    Serial.println("Aviso: Problema de conexão detectado, R0 é zero (pino analógico com curto-circuito ao terra), por favor verifique a fiação e a alimentação.");
    while (1);
  }
}


double publishGasReading(MQUnifiedsensor &sensor, const char* topic, const char* gasName) {
  sensor.update();
  double reading = sensor.readSensor();
  String readingString = String(reading, 2);
  client.publish(topic, readingString.c_str());

  Serial.print(gasName);
  Serial.print(": ");
  Serial.print(reading);
  Serial.println(" ppm");

  return reading;
}
