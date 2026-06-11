/*
  ============================================================================
  ESP32 EMISOR V3 - ESP-NOW BROADCAST
  ============================================================================

  IDEA GENERAL DEL PROYECTO
  ---------------------------------------------------------------------------

  Este archivo corresponde a la placa EMISORA, que en este caso es una ESP32.

  Su trabajo es leer entradas físicas conectadas a sus GPIO:

    - pulsadores
    - llaves
    - potenciómetros
    - sensores analógicos simples

  Después empaqueta esa información en un mensaje y la envía por ESP-NOW.

  ---------------------------------------------------------------------------

  DIAGRAMA SIMPLE

      Entradas físicas
      botones / llaves / potenciómetro
              |
              v
      ESP32 EMISORA
              |
              |  ESP-NOW BROADCAST
              v
      ESP8266 RECEPTORA
      web + configuración + salidas

  ---------------------------------------------------------------------------

  QUÉ SIGNIFICA BROADCAST

  En esta versión, la ESP32 no envía a una MAC específica.
  Envía a:

      FF:FF:FF:FF:FF:FF

  Esa dirección significa "mandar a todos".

  Es parecido a una radio:
    - el emisor transmite
    - cualquier receptor en el mismo canal puede escuchar

  Después, el receptor decide si acepta o rechaza el mensaje.
  Esa decisión se hace con el filtro MAC del receptor.

  ---------------------------------------------------------------------------

  QUÉ DATOS ENVÍA

  Cada mensaje ESP-NOW contiene:

    inputType:
      indica si la entrada es digital o analógica

    inputPin:
      indica qué GPIO del emisor generó el evento

    digitalValue:
      indica si una entrada digital está prendida o apagada

    analogValue:
      indica el valor analógico leído, de 0 a 4095

  Ejemplo botón:

      inputType    = digital
      inputPin     = 32
      digitalValue = true
      analogValue  = 4095

  Ejemplo potenciómetro:

      inputType    = analógico
      inputPin     = 34
      digitalValue = depende del umbral
      analogValue  = valor entre 0 y 4095

  ---------------------------------------------------------------------------

  CONEXIONES USADAS EN ESTA VERSIÓN

  Botón 1:
      GPIO32 ---- pulsador ---- GND

  Botón 2:
      GPIO33 ---- pulsador ---- GND

  Botón 3:
      GPIO25 ---- pulsador ---- GND

  Potenciómetro:
      3V3   ---- extremo 1
      GND   ---- extremo 2
      GPIO34 ---- pin central

  ---------------------------------------------------------------------------

  SOBRE INPUT_PULLUP

  Las entradas digitales usan INPUT_PULLUP.

  Eso significa:

    - Sin apretar el botón, el pin lee HIGH.
    - Al apretar el botón, el pin se conecta a GND y lee LOW.

  En el código se invierte esa lectura para que sea más fácil pensar:

    - botón apretado = true
    - botón suelto   = false

  ---------------------------------------------------------------------------

  OBJETIVO DIDÁCTICO

  El código está organizado en secciones para que alguien de primer año pueda
  seguir el flujo:

    1. Librerías
    2. Constantes
    3. Pines
    4. Estructura del mensaje
    5. Variables de estado
    6. Función de envío
    7. setup()
    8. loop()

  ============================================================================
*/

// WiFi.h:
// permite configurar la ESP32 en modo WiFi.
// ESP-NOW usa el hardware WiFi, aunque no nos conectemos a internet.

// esp_now.h:
// contiene las funciones principales de ESP-NOW:
// iniciar ESP-NOW, agregar peer y enviar mensajes.

// esp_wifi.h:
// permite fijar el canal WiFi.
// Emisor y receptor deben estar en el mismo canal.
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ============================================================
// CONFIGURACIÓN GENERAL
// ============================================================

// Canal usado por ESP-NOW.
 // El receptor debe usar el mismo canal.
 //
 // Si el emisor usa canal 1 y el receptor usa canal 6, no se escuchan.
 // Por eso dejamos fijo el canal 1 en ambas placas.
#define ESPNOW_CHANNEL 1

// ============================================================
// CANTIDAD DE ENTRADAS DEL EMISOR
// ============================================================

// Cantidad de botones digitales conectados.
 // Si agregás otro botón, cambiá este número y agregá su GPIO
 // en el array digitalInputs[].
#define NUM_DIGITAL_INPUTS 3

// Cantidad de entradas analógicas conectadas.
 // En esta versión usamos una sola: un potenciómetro en GPIO34.
#define NUM_ANALOG_INPUTS 1

// ============================================================
// PINES DIGITALES
// ============================================================

// Cada botón va conectado entre el GPIO y GND.
// Usamos INPUT_PULLUP, por eso no hace falta resistencia externa.
#define INPUT_DIGITAL_1 32
#define INPUT_DIGITAL_2 33
#define INPUT_DIGITAL_3 25

const uint8_t digitalInputs[NUM_DIGITAL_INPUTS] = {
  INPUT_DIGITAL_1,
  INPUT_DIGITAL_2,
  INPUT_DIGITAL_3
};

// ============================================================
// PINES ANALÓGICOS
// ============================================================

// GPIO34 es una buena entrada analógica en ESP32.
// Sirve para potenciómetro.
// Conexión pote:
// 3V3  -> extremo 1
// GND  -> extremo 2
// GPIO34 -> pin central
#define INPUT_ANALOG_1 34

const uint8_t analogInputs[NUM_ANALOG_INPUTS] = {
  INPUT_ANALOG_1
};

// ============================================================
// TIPOS DE PIN
// ============================================================

#define PIN_DIGITAL 0
#define PIN_ANALOGICO 1

// ============================================================
// MAC BROADCAST
// ============================================================

// FF:FF:FF:FF:FF:FF significa enviar a todos.
uint8_t broadcastMac[] = {
  0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF
};

// ============================================================
// PAQUETE ESPNOW
// ============================================================

/*
  EspNowMsg

  Esta estructura es el "paquete" que viaja por ESP-NOW.

  MUY IMPORTANTE:
  La estructura debe ser igual en el emisor y en el receptor.
  Si se cambia el orden o el tipo de una variable, el receptor puede
  interpretar mal los datos.

  Ejemplo:
  Si el emisor manda inputPin como uint8_t, el receptor también tiene
  que recibir inputPin como uint8_t.
*/
struct EspNowMsg {
  uint8_t inputType;     // 0 digital, 1 analógico.
  uint8_t inputPin;      // GPIO de entrada.
  bool digitalValue;     // true/false.
  uint16_t analogValue;  // valor analógico 0 a 4095.
};

// ============================================================
// VARIABLES DE CONTROL
// ============================================================

// Guarda el último estado conocido de cada botón.
 // Esto permite detectar cambios:
 // por ejemplo, cuando pasa de suelto a apretado.
bool lastDigital[NUM_DIGITAL_INPUTS];

// Guarda el momento del último cambio de cada botón.
 // Sirve para implementar antirrebote.
unsigned long lastChange[NUM_DIGITAL_INPUTS];

// Guarda el último valor analógico enviado.
 // Así evitamos enviar datos si el potenciómetro casi no cambió.
uint16_t lastAnalog[NUM_ANALOG_INPUTS];

// Tiempo de antirrebote para botones, en milisegundos.
 // Los pulsadores mecánicos pueden generar varios cambios falsos
 // al apretarse o soltarse. Este tiempo filtra esos rebotes.
const unsigned long debounceMs = 70;

// Umbral mínimo para reenviar valores analógicos.
 // El potenciómetro puede variar algunos puntos aunque no lo toquemos.
 // Solo enviamos si la diferencia supera este valor.
const int analogThreshold = 80;

// ============================================================
// ENVIAR MENSAJE ESPNOW
// ============================================================

/*
  enviarMensaje(msg)

  Envía un paquete EspNowMsg por ESP-NOW.

  Como usamos broadcast, el destino es FF:FF:FF:FF:FF:FF.
  No hace falta conocer la MAC del receptor.

  Además imprime por Monitor Serie lo que se envió, para poder depurar.
*/
void enviarMensaje(EspNowMsg msg) {
  // esp_now_send recibe:
  // 1) MAC de destino
  // 2) puntero a los datos a enviar
  // 3) tamaño del paquete en bytes
  //
  // Convertimos msg a uint8_t* porque ESP-NOW envía bytes.
  esp_err_t result = esp_now_send(
    broadcastMac,
    (uint8_t *)&msg,
    sizeof(msg)
  );

  Serial.print("ENVIO | pin ");
  Serial.print(msg.inputPin);

  Serial.print(" | tipo ");
  Serial.print(msg.inputType == PIN_DIGITAL ? "DIGITAL" : "ANALOGICO");

  Serial.print(" | digital ");
  Serial.print(msg.digitalValue);

  Serial.print(" | analog ");
  Serial.print(msg.analogValue);

  Serial.print(" | resultado ");
  Serial.println(result == ESP_OK ? "OK" : "ERROR");
}

// ============================================================
// SETUP
// ============================================================

/*
  setup()

  Se ejecuta una sola vez cuando prende o reinicia la ESP32.

  Orden de trabajo:
    1. Inicia el Monitor Serie.
    2. Configura botones como INPUT_PULLUP.
    3. Lee valores iniciales.
    4. Pone WiFi en modo estación.
    5. Fija el canal ESP-NOW.
    6. Inicia ESP-NOW.
    7. Agrega el peer broadcast.
*/
void setup() {
  Serial.begin(115200);

  // ------------------------------------------------------------
  // Configurar botones
  // ------------------------------------------------------------
  for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
    pinMode(digitalInputs[i], INPUT_PULLUP);

    // Con INPUT_PULLUP:
    // botón suelto = HIGH
    // botón apretado = LOW
    //
    // Por eso comparamos con LOW:
    // si lee LOW, lo guardamos como true.
    lastDigital[i] = digitalRead(digitalInputs[i]) == LOW;

    lastChange[i] = 0;
  }

  // ------------------------------------------------------------
  // Leer valores iniciales analógicos
  // ------------------------------------------------------------
  for (int i = 0; i < NUM_ANALOG_INPUTS; i++) {
    lastAnalog[i] = analogRead(analogInputs[i]);
  }

  // ------------------------------------------------------------
  // Configurar WiFi para ESP-NOW
  // ------------------------------------------------------------
  WiFi.mode(WIFI_STA);

  // Fijamos canal para que coincida con el receptor.
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  Serial.print("MAC ESP32 EMISOR: ");
  Serial.println(WiFi.macAddress());

  // ------------------------------------------------------------
  // Iniciar ESP-NOW
  // ------------------------------------------------------------
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: no se pudo iniciar ESP-NOW");
    return;
  }

  // ------------------------------------------------------------
  // Registrar destino BROADCAST
  // ------------------------------------------------------------
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastMac, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("ERROR: no se pudo agregar peer broadcast");
    return;
  }

  Serial.println("ESP32 EMISOR V3 LISTO - BROADCAST");
}

// ============================================================
// LOOP
// ============================================================

/*
  loop()

  Se ejecuta todo el tiempo.

  En cada vuelta:
    1. Revisa si cambió algún botón.
    2. Si cambió, envía un mensaje digital.
    3. Revisa si cambió el potenciómetro.
    4. Si cambió lo suficiente, envía un mensaje analógico.

  No se envía constantemente sin necesidad:
  solo se envía cuando hay cambios.
*/
void loop() {
  // ------------------------------------------------------------
  // Leer entradas digitales
  // ------------------------------------------------------------
  for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
    bool actual = digitalRead(digitalInputs[i]) == LOW;

    // Si cambió el estado, aplicamos antirrebote.
    if (actual != lastDigital[i]) {
      if (millis() - lastChange[i] > debounceMs) {
        lastDigital[i] = actual;
        lastChange[i] = millis();

        // Armamos el mensaje digital.
        //
        // inputPin permite que el receptor sepa
        // qué GPIO del emisor generó el evento.
        //
        // analogValue se completa también para mantener
        // el paquete siempre con la misma forma.
        EspNowMsg msg;
        msg.inputType = PIN_DIGITAL;
        msg.inputPin = digitalInputs[i];
        msg.digitalValue = actual;
        msg.analogValue = actual ? 4095 : 0;

        enviarMensaje(msg);
      }
    }
  }

  // ------------------------------------------------------------
  // Leer entradas analógicas
  // ------------------------------------------------------------
  for (int i = 0; i < NUM_ANALOG_INPUTS; i++) {
    uint16_t actual = analogRead(analogInputs[i]);

    // Solo enviamos si cambió lo suficiente.
    if (abs((int)actual - (int)lastAnalog[i]) > analogThreshold) {
      lastAnalog[i] = actual;

      // Armamos el mensaje analógico.
      //
      // analogValue lleva el valor real del potenciómetro.
      //
      // digitalValue también se completa usando un umbral:
      // si el valor supera la mitad del rango, lo tomamos como true.
      // Esto puede servir si una salida digital quiere reaccionar
      // ante una entrada analógica.
      EspNowMsg msg;
      msg.inputType = PIN_ANALOGICO;
      msg.inputPin = analogInputs[i];
      msg.digitalValue = actual > 2048;
      msg.analogValue = actual;

      enviarMensaje(msg);
    }
  }

  delay(20);
}
