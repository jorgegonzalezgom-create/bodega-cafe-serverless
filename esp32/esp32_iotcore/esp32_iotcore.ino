// ===== Librerias =====
#include <WiFi.h>
#include <WiFiClientSecure.h>   // conexion cifrada (TLS) con certificados
#include <PubSubClient.h>       // cliente MQTT
#include <DHT.h>
#include "secrets.h"

// ===== Topics MQTT =====
const char* TOPIC_LECTURAS = "bodega1/lecturas";  // ESP32 -> AWS (lecturas del sensor)
const char* TOPIC_LED      = "bodega1/led";       // AWS -> ESP32 (comando ON/OFF del actuador)

// ===== Hardware =====
#define DHTPIN 4          // datos del DHT11 en D4
#define DHTTYPE DHT11
#define LED_PIN 2         // actuador: LED en D2
DHT dht(DHTPIN, DHTTYPE);

WiFiClientSecure net;           // "tuberia" cifrada hacia AWS
PubSubClient mqtt(net);         // MQTT viaja por esa tuberia
unsigned long ultimoEnvio = 0;  // para enviar cada 10 s sin bloquear
bool ledEncendido = false;

// Regla del cafe: 15-25 C y 50-65 % HR
String calcularEstado(float t, float h) {
  return (t >= 15 && t <= 25 && h >= 50 && h <= 65) ? "OK" : "ALERTA";
}

// Se ejecuta cuando llega un mensaje (comando del dashboard)
void alRecibir(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim(); msg.toUpperCase();
  Serial.printf("Comando recibido [%s]: %s\n", topic, msg.c_str());
  if (msg == "ON")  { digitalWrite(LED_PIN, HIGH); ledEncendido = true;  }
  if (msg == "OFF") { digitalWrite(LED_PIN, LOW);  ledEncendido = false; }
}

void conectarWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Conectando WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.print("\nWiFi OK. IP: ");
  Serial.println(WiFi.localIP());
}

// Conecta a AWS IoT Core presentando los certificados
void conectarAWS() {
  while (!mqtt.connected()) {
    Serial.print("Conectando a AWS IoT Core... ");
    if (mqtt.connect(THING_NAME)) {          // el nombre debe ser bodega-esp32 (politica)
      Serial.println("OK");
      mqtt.subscribe(TOPIC_LED);             // escuchar comandos del actuador
    } else {
      Serial.printf("fallo (estado %d), reintento en 5 s\n", mqtt.state());
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  dht.begin();
  conectarWiFi();

  // Certificados para TLS mutuo: el ESP32 verifica a AWS y AWS verifica al ESP32
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  mqtt.setServer(AWS_ENDPOINT, 8883);   // 8883 = MQTT sobre TLS
  mqtt.setCallback(alRecibir);
  mqtt.setBufferSize(512);              // mensajes de hasta 512 bytes
  mqtt.setKeepAlive(60);                // mantiene viva la conexion
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) conectarWiFi();   // reconexion WiFi
  if (!mqtt.connected()) conectarAWS();                // reconexion MQTT
  mqtt.loop();                                         // revisa si llegaron comandos

  // Cada 10 s: leer y publicar
  if (millis() - ultimoEnvio >= 10000) {
    ultimoEnvio = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (isnan(t) || isnan(h)) { Serial.println("Error sensor"); return; }

    String json = "{\"dispositivo\":\"" + String(THING_NAME) + "\"" +
                  ",\"temperatura\":" + String(t, 1) +
                  ",\"humedad\":" + String(h, 1) +
                  ",\"estado\":\"" + calcularEstado(t, h) + "\"" +
                  ",\"led\":\"" + (ledEncendido ? "ON" : "OFF") + "\"}";

    bool ok = mqtt.publish(TOPIC_LECTURAS, json.c_str());
    Serial.printf("%s -> %s\n", json.c_str(), ok ? "publicado" : "fallo");
  }
}
