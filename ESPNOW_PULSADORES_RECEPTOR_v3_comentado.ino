/*
  ============================================================================
  ESP8266 RECEPTOR V3 - WEB + ESP-NOW
  ============================================================================

  IDEA GENERAL DEL PROYECTO
  ---------------------------------------------------------------------------

  Este archivo corresponde a la placa RECEPTORA, que en este caso es una
  ESP8266.

  La otra placa, una ESP32, funciona como EMISORA. Esa placa lee entradas
  físicas, por ejemplo:

    - pulsadores
    - llaves
    - potenciómetros

  y envía esos datos por ESP-NOW.

  Esta ESP8266 recibe esos mensajes y, según la configuración guardada desde
  la web, activa salidas físicas:

    - salidas digitales ON/OFF
    - salidas PWM para variar brillo o intensidad

  ---------------------------------------------------------------------------

  DIAGRAMA SIMPLE

      ESP32 EMISORA
      botones / llaves / potenciómetro
              |
              |  ESP-NOW BROADCAST
              v
      ESP8266 RECEPTORA
      web + configuración + salidas

  ---------------------------------------------------------------------------

  QUÉ HACE ESTA ESP8266

  1) Crea una red WiFi propia.
     El nombre de la red se arma como:

        ESP-REMOTE-XXXX

     donde XXXX son los últimos 4 caracteres de la MAC.
     Esto permite distinguir varias placas si hay más de una encendida.

  2) Levanta un servidor web en:

        192.168.4.1

  3) Muestra dos vistas web:

     - Dashboard:
       vista general con cards pequeñas para prender/apagar salidas.

     - Configuración:
       vista de detalle donde se crean y editan las cards.

  4) Recibe mensajes ESP-NOW desde el emisor.

  5) Busca qué card coincide con el pin de entrada recibido.

  6) Ejecuta la salida configurada en esa card.

  ---------------------------------------------------------------------------

  QUÉ ES UNA CARD

  Una card representa una relación entre una entrada y una salida.

  Ejemplo digital:

      Entrada GPIO32 de ESP32  --->  Salida GPIO5 de ESP8266
      Botón                    --->  LED / Relé

  Ejemplo analógico:

      Entrada GPIO34 de ESP32  --->  Salida GPIO14 PWM de ESP8266
      Potenciómetro            --->  Brillo de LED

  Cada card guarda:
    - nombre
    - tipo de entrada
    - pin de entrada
    - tipo de salida
    - pin de salida
    - modo llave/pulsador
    - retención
    - invertir lógica

  ---------------------------------------------------------------------------

  SOBRE EL FILTRO MAC

  El emisor manda por broadcast, como una radio.
  Eso significa que no le manda a una MAC específica, sino "al aire".

  El receptor puede trabajar de dos formas:

    - Sin filtro:
      acepta mensajes de cualquier emisor.

    - Con filtro:
      acepta solamente mensajes de la MAC escrita en la web.

  ---------------------------------------------------------------------------

  SOBRE LOS PINES

  La web muestra pines posibles según la placa elegida.
  También existe modo manual para escribir un GPIO a mano.

  Importante:
  Algunos pines del ESP8266 pueden afectar el arranque si se conectan mal.
  Por eso esta herramienta permite experimentar, pero la conexión física debe
  hacerse con cuidado.

  ---------------------------------------------------------------------------

  OBJETIVO DIDÁCTICO

  El código está organizado en secciones para que alguien de primer año pueda
  ubicar rápidamente qué parte hace cada cosa:

    1. Librerías
    2. Constantes
    3. Estructuras de datos
    4. EEPROM
    5. Control de salidas
    6. ESP-NOW
    7. Web
    8. Setup
    9. Loop

  ============================================================================
*/

// Librerías principales.
// ESP8266WiFi: permite crear la red WiFi propia.
// ESP8266WebServer: permite crear la web de configuración.
// espnow: permite recibir datos por ESP-NOW.
// EEPROM: permite guardar la configuración aunque se reinicie la placa.
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <espnow.h>
#include <EEPROM.h>

extern "C" {
  #include <user_interface.h>
}

// ============================================================
// CONFIGURACIÓN GENERAL
// ============================================================

// Tamaño de EEPROM que reservamos para guardar configuración.
#define EEPROM_SIZE 2048

// Cantidad máxima técnica de cards.
// No es por cantidad de pines, es para cuidar memoria del ESP8266.
#define MAX_CARDS 12

// Canal WiFi usado por ESP-NOW.
#define ESPNOW_CHANNEL 1

// Clave del WiFi creado por el ESP8266.
#define WIFI_PASS "12345678"

// Firma para saber si la EEPROM ya tiene datos válidos.
#define CONFIG_MAGIC 12345678

// Tipos de pin.
#define PIN_DIGITAL 0
#define PIN_ANALOGICO 1

// Modos de salida digital.
#define MODO_LLAVE 0
#define MODO_PULSADOR 1

// ============================================================
// SERVIDOR WEB
// ============================================================

ESP8266WebServer server(80);

// ============================================================
// ESTRUCTURAS DE DATOS
// ============================================================

/*
  EspNowMsg

  Estructura del paquete que llega desde la ESP32 emisora.

  MUY IMPORTANTE:
  esta estructura debe ser igual en el emisor y en el receptor.
  Si se cambia el orden, el tipo o la cantidad de campos, ESP-NOW
  va a recibir datos mal interpretados.

  Campos:
    inputType:
      0 = digital
      1 = analógico

    inputPin:
      GPIO de la placa emisora que generó el mensaje.

    digitalValue:
      true/false para botones o llaves.

    analogValue:
      valor analógico. En ESP32 normalmente va de 0 a 4095.
*/

struct EspNowMsg {
  uint8_t inputType;
  uint8_t inputPin;
  bool digitalValue;
  uint16_t analogValue;
};

/*
  CardConfig

  Esta estructura guarda la configuración de UNA card.

  Una card es una regla de conexión lógica:

      entrada recibida  --->  salida física

  Por ejemplo:
      GPIO32 digital    --->  GPIO5 digital
      GPIO34 analógico  --->  GPIO14 PWM

  El array config.cards[] contiene varias CardConfig.
*/

struct CardConfig {
  bool active;             // Si la card está activa o borrada.
  char nombre[28];         // Nombre visible, por ejemplo "Lámpara".
  uint8_t inputType;       // 0 digital, 1 analógico.
  uint8_t inputPin;        // GPIO de entrada recibido desde el emisor.
  uint8_t outputType;      // 0 digital, 1 PWM.
  uint8_t outputPin;       // GPIO de salida local del ESP8266.
  uint8_t modo;            // 0 llave, 1 pulsador.
  bool invertir;           // Invierte lógica.
  unsigned long retencion; // Tiempo en ms para modo pulsador.
};

/*
  Config

  Esta es la configuración completa del sistema.

  Se guarda entera en EEPROM para que la ESP8266 recuerde
  la configuración después de apagarse o reiniciarse.
*/

struct Config {
  uint32_t magic;
  bool filtroMacActivo;    // false = acepta cualquiera, true = filtra por MAC.
  char macPermitida[18];   // MAC del emisor autorizado.

  // Estas variables guardan qué placa eligió el usuario en la web.
  // Sirven para mostrar los GPIO posibles en los selectores.
  // No cambian mágicamente el hardware: solo ayudan a configurar bien.
  char placaEmisora[10];   // "esp32" o "esp8266"
  char placaReceptora[10]; // "esp32" o "esp8266"

  CardConfig cards[MAX_CARDS];
};

Config config;

// Estado actual de cada card.
// Esto sirve para mostrar si está prendida/apagada y para retención.
bool estadoDigital[MAX_CARDS];
uint16_t estadoAnalogico[MAX_CARDS];
unsigned long tiempoApagado[MAX_CARDS];

// ============================================================
// FUNCIONES DE EEPROM
// ============================================================

/*
  guardarConfig()

  Guarda toda la estructura "config" en la EEPROM.

  La EEPROM funciona como una pequeña memoria persistente:
  si la placa se apaga o se reinicia, los datos siguen guardados.

  En este proyecto se usa para guardar:
    - filtro MAC
    - MAC permitida
    - placa emisora/receptora elegida en la web
    - configuración de cada card
*/
void guardarConfig() {
  EEPROM.put(0, config);
  EEPROM.commit();
  Serial.println("CONFIGURACION GUARDADA");
}

/*
  cargarDefault()

  Carga una configuración inicial cuando la EEPROM está vacía
  o cuando todavía no hay una configuración válida.

  Esto evita que el sistema arranque con datos basura.

  Se crea una primera card de ejemplo:
    Entrada digital GPIO32 -> Salida digital GPIO5
*/
void cargarDefault() {
  config.magic = CONFIG_MAGIC;
  config.filtroMacActivo = false;
  strcpy(config.macPermitida, "");

  // Valores iniciales:
  // Emisor ESP32 y receptor ESP8266, que es el montaje que venimos usando.
  strcpy(config.placaEmisora, "esp32");
  strcpy(config.placaReceptora, "esp8266");

  for (int i = 0; i < MAX_CARDS; i++) {
    config.cards[i].active = false;
    strcpy(config.cards[i].nombre, "Nuevo switch");
    config.cards[i].inputType = PIN_DIGITAL;
    config.cards[i].inputPin = 32;
    config.cards[i].outputType = PIN_DIGITAL;
    config.cards[i].outputPin = 5;
    config.cards[i].modo = MODO_PULSADOR;
    config.cards[i].invertir = false;
    config.cards[i].retencion = 1000;

    estadoDigital[i] = false;
    estadoAnalogico[i] = 0;
    tiempoApagado[i] = 0;
  }

  // Creamos una card inicial para que la web no arranque vacía.
  config.cards[0].active = true;
  strcpy(config.cards[0].nombre, "Luz principal");
  config.cards[0].inputType = PIN_DIGITAL;
  config.cards[0].inputPin = 32;
  config.cards[0].outputType = PIN_DIGITAL;
  config.cards[0].outputPin = 5;
  config.cards[0].modo = MODO_PULSADOR;
  config.cards[0].invertir = false;
  config.cards[0].retencion = 1000;

  guardarConfig();
}

/*
  cargarConfig()

  Lee la configuración guardada en EEPROM.

  Si la firma CONFIG_MAGIC no coincide, significa que:
    - la EEPROM está vacía
    - o viene de una versión vieja
    - o tiene datos inválidos

  En ese caso se carga la configuración por defecto.
*/
void cargarConfig() {
  EEPROM.get(0, config);

  if (config.magic != CONFIG_MAGIC) {
    cargarDefault();
  }

  // Si venimos de una EEPROM de versión anterior, estos campos pueden estar vacíos.
  // Los completamos con valores seguros para esta versión.
  if (strlen(config.placaEmisora) == 0) {
    strcpy(config.placaEmisora, "esp32");
  }

  if (strlen(config.placaReceptora) == 0) {
    strcpy(config.placaReceptora, "esp8266");
  }
}

// ============================================================
// NOMBRE WIFI ÚNICO
// ============================================================

/*
  getWifiName()

  Genera el nombre del WiFi de esta ESP8266.

  Ejemplo:
    MAC: CC:50:E3:55:E9:7B
    WiFi: ESP-REMOTE-E97B

  Esto hace que cada receptor tenga un nombre único.
*/
String getWifiName() {
  // Tomamos la MAC del ESP8266.
  String mac = WiFi.macAddress();

  // Borramos los dos puntos.
  mac.replace(":", "");

  // Tomamos los últimos 4 caracteres.
  String ultimos4 = mac.substring(mac.length() - 4);

  // Armamos nombre único.
  return "ESP-REMOTE-" + ultimos4;
}

// ============================================================
// CONTROL DE SALIDAS
// ============================================================

/*
  prepararSalida(pin)

  Configura un GPIO como salida.

  Se llama cuando se carga la configuración y también cuando
  el usuario guarda cambios desde la web.
*/
void prepararSalida(int pin) {
  pinMode(pin, OUTPUT);
}

// Salida digital ON/OFF.
/*
  setSalidaDigital(index, encender)

  Activa o desactiva una salida digital.

  index:
    posición de la card dentro del array config.cards[]

  encender:
    true  -> salida encendida
    false -> salida apagada

  Si la card tiene "invertir lógica" activado, el valor físico del pin
  se invierte. Esto sirve para relés que funcionan con lógica inversa.
*/
void setSalidaDigital(int index, bool encender) {
  CardConfig &c = config.cards[index];

  bool valorFinal = c.invertir ? !encender : encender;

  digitalWrite(c.outputPin, valorFinal ? HIGH : LOW);

  estadoDigital[index] = encender;
  estadoAnalogico[index] = encender ? 1023 : 0;

  Serial.print("SALIDA DIGITAL | card ");
  Serial.print(index);
  Serial.print(" | GPIO ");
  Serial.print(c.outputPin);
  Serial.print(" | estado ");
  Serial.println(encender ? "ON" : "OFF");
}

// Salida PWM.
// En ESP8266 analogWrite usa valores de 0 a 1023.
/*
  setSalidaAnalogica(index, value)

  Controla una salida PWM.

  En ESP8266 analogWrite usa valores de 0 a 1023:
    0    -> apagado
    1023 -> máximo brillo/intensidad

  Si la card tiene "invertir lógica", se invierte el valor:
    0 pasa a 1023
    1023 pasa a 0
*/
void setSalidaAnalogica(int index, int value) {
  CardConfig &c = config.cards[index];

  value = constrain(value, 0, 1023);

  int valorFinal = c.invertir ? 1023 - value : value;

  analogWrite(c.outputPin, valorFinal);

  estadoAnalogico[index] = value;
  estadoDigital[index] = value > 0;

  Serial.print("SALIDA PWM | card ");
  Serial.print(index);
  Serial.print(" | GPIO ");
  Serial.print(c.outputPin);
  Serial.print(" | valor ");
  Serial.println(value);
}

// Aplica valor recibido del emisor a la salida configurada.
/*
  aplicarDesdeMensaje(index, msg)

  Toma un mensaje recibido por ESP-NOW y lo aplica a una card.

  Esta función decide qué hacer según:
    - si la salida es digital o PWM
    - si la entrada recibida es digital o analógica
    - si el modo es llave o pulsador
    - si hay retención configurada

  En modo pulsador:
    - al apretar, prende
    - al soltar, empieza a contar la retención
    - al terminar el tiempo, apaga

  En modo llave:
    - la salida copia directamente el estado recibido
*/
void aplicarDesdeMensaje(int index, EspNowMsg msg) {
  CardConfig &c = config.cards[index];

  // Si la salida es digital.
  if (c.outputType == PIN_DIGITAL) {
    bool valorDigital = msg.digitalValue;

    // Si la entrada era analógica, hacemos un umbral simple.
    if (msg.inputType == PIN_ANALOGICO) {
      valorDigital = msg.analogValue > 2048;
    }

    // Modo pulsador:
    // - Al apretar prende.
    // - Al soltar empieza a contar la retención.
    if (c.modo == MODO_PULSADOR) {
      if (valorDigital) {
        setSalidaDigital(index, true);
        tiempoApagado[index] = 0;
      } else {
        tiempoApagado[index] = millis() + c.retencion;
      }
    }

    // Modo llave:
    // - Lo que llega es lo que se aplica.
    else {
      setSalidaDigital(index, valorDigital);
    }
  }

  // Si la salida es analógica/PWM.
  else {
    int pwmValue = 0;

    if (msg.inputType == PIN_ANALOGICO) {
      // El ESP32 lee 0 a 4095.
      // El ESP8266 escribe PWM 0 a 1023.
      pwmValue = map(msg.analogValue, 0, 4095, 0, 1023);
    } else {
      pwmValue = msg.digitalValue ? 1023 : 0;
    }

    setSalidaAnalogica(index, pwmValue);
  }
}

// ============================================================
// FILTRO POR MAC
// ============================================================

/*
  macEstaPermitida(mac)

  Verifica si el mensaje recibido viene de una MAC autorizada.

  Si el filtro MAC está desactivado:
    acepta cualquier emisor.

  Si el filtro MAC está activado:
    compara la MAC del emisor contra la MAC escrita en la web.
*/
bool macEstaPermitida(uint8_t *mac) {
  // Si el filtro está apagado, acepta cualquier emisor.
  if (!config.filtroMacActivo) return true;

  String macConfig = String(config.macPermitida);
  macConfig.trim();
  macConfig.toUpperCase();

  // Si el filtro está activo pero no hay MAC escrita, no filtramos.
  if (macConfig.length() == 0) return true;

  char macStr[18];

  sprintf(macStr,
          "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2],
          mac[3], mac[4], mac[5]);

  String recibida = String(macStr);
  recibida.toUpperCase();

  return recibida == macConfig;
}

// ============================================================
// CALLBACK ESPNOW
// ============================================================

/*
  onDataRecv(...)

  Esta función se ejecuta automáticamente cada vez que llega
  un mensaje por ESP-NOW.

  Flujo:
    1. Verifica que el tamaño del mensaje sea correcto.
    2. Copia los bytes recibidos a una estructura EspNowMsg.
    3. Revisa el filtro MAC.
    4. Busca una card que coincida con:
         - inputPin
         - inputType
    5. Si la encuentra, aplica la salida configurada.
*/
void onDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  if (len != sizeof(EspNowMsg)) return;

  EspNowMsg msg;
  memcpy(&msg, incomingData, sizeof(msg));


  if (!macEstaPermitida(mac)) {
    Serial.println("MENSAJE RECHAZADO POR FILTRO MAC");
    return;
  }

  Serial.print("RECIBIDO | input GPIO ");
  Serial.print(msg.inputPin);
  Serial.print(" | tipo ");
  Serial.print(msg.inputType == PIN_DIGITAL ? "DIGITAL" : "ANALOGICO");
  Serial.print(" | digital ");
  Serial.print(msg.digitalValue);
  Serial.print(" | analog ");
  Serial.println(msg.analogValue);

  // Buscamos qué card usa esa entrada.
  for (int i = 0; i < MAX_CARDS; i++) {
    CardConfig &c = config.cards[i];

    if (!c.active) continue;

    if (c.inputPin == msg.inputPin && c.inputType == msg.inputType) {
      aplicarDesdeMensaje(i, msg);
    }
  }
}

// ============================================================
// AYUDAS PARA HTML
// ============================================================

/*
  checked(v)

  Función auxiliar para generar HTML.

  Devuelve la palabra "checked" cuando un checkbox debe aparecer marcado.
*/
String checked(bool v) {
  return v ? "checked" : "";
}

/*
  selectedInt(a, b)

  Función auxiliar para generar HTML.

  Devuelve "selected" cuando una opción de un selector debe aparecer elegida.
*/
String selectedInt(int a, int b) {
  return a == b ? "selected" : "";
}

/*
  estadoTexto(i)

  Devuelve el texto visible del estado de una card.

  Ejemplos:
    - Encendida
    - Apagada
    - PWM 500

  Se usa en el dashboard para mostrar el estado actual.
*/
String estadoTexto(int i) {
  if (!config.cards[i].active) return "Sin configurar";

  if (config.cards[i].outputType == PIN_ANALOGICO) {
if (estadoAnalogico[i] == 0) return "Apagada";
return String("PWM ") + String(estadoAnalogico[i]);
  }

  return estadoDigital[i] ? "Encendida" : "Apagada";
}

// ============================================================
// ESTILOS HTML
// ============================================================

/*
  htmlHead(title)

  Genera el inicio de cualquier página web:
    - HTML inicial
    - viewport para celular
    - estilos CSS
    - apertura del contenedor principal

  La mayor parte del diseño visual está definido acá.
*/
String htmlHead(String title) {
  String html = "";

  html += "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>" + title + "</title>";

  html += "<style>";
  html += "*{box-sizing:border-box;}";
  html += "body{margin:0;background:#0b1113;color:#eef5f7;font-family:Arial,Helvetica,sans-serif;}";
  html += ".wrap{max-width:980px;margin:auto;padding:10px;}";
  html += "h1{font-size:22px;margin:8px 0 12px;text-align:center;letter-spacing:.3px;}";
  html += ".dashTop{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px;gap:12px;flex-wrap:wrap;}";
  html += ".dashTitle{font-size:20px;font-weight:bold;}";
  html += ".configBtn{background:#151b1f;border:1px solid #4b5563;color:white;border-radius:20px;padding:15px 24px;font-size:17px;font-weight:bold;text-decoration:none;display:inline-flex;align-items:center;justify-content:center;min-width:190px;box-shadow:none;}";
  html += ".grid{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;}";
  html += "@media(min-width:760px){.grid{grid-template-columns:repeat(4,1fr);}}";
  html += ".mini{background:#24292c;border:1px solid #2f373b;border-radius:18px;padding:10px;min-height:72px;display:flex;align-items:center;gap:9px;color:white;text-decoration:none;cursor:pointer;transition:.15s ease;}";
  html += ".mini.on{background:#27343a;}";
  html += ".mini{border-color:#3b4449;}";
  html += ".mini .dot{background:#6b7280;}";
  html += ".mini.on.c0{border-color:#38bdf8;}.mini.on.c1{border-color:#a78bfa;}.mini.on.c2{border-color:#34d399;}.mini.on.c3{border-color:#f59e0b;}.mini.on.c4{border-color:#fb7185;}.mini.on.c5{border-color:#22d3ee;}.mini.on.c6{border-color:#c084fc;}.mini.on.c7{border-color:#84cc16;}.mini.on.c8{border-color:#f97316;}.mini.on.c9{border-color:#60a5fa;}.mini.on.c10{border-color:#e879f9;}.mini.on.c11{border-color:#2dd4bf;}";
  html += ".mini.on.c0 .dot{background:#38bdf8;}.mini.on.c1 .dot{background:#a78bfa;}.mini.on.c2 .dot{background:#34d399;}.mini.on.c3 .dot{background:#f59e0b;}.mini.on.c4 .dot{background:#fb7185;}.mini.on.c5 .dot{background:#22d3ee;}.mini.on.c6 .dot{background:#c084fc;}.mini.on.c7 .dot{background:#84cc16;}.mini.on.c8 .dot{background:#f97316;}.mini.on.c9 .dot{background:#60a5fa;}.mini.on.c10 .dot{background:#e879f9;}.mini.on.c11 .dot{background:#2dd4bf;}";
  html += ".dot{width:18px;height:18px;border-radius:50%;background:#58666d;flex:0 0 18px;box-shadow:0 0 0 3px rgba(255,255,255,.04);}";
  html += ".mini.on .dot{box-shadow:0 0 12px rgba(255,255,255,.45);}";
  html += ".miniText{flex:1;min-width:0;}";
  html += ".name{font-size:14px;font-weight:bold;line-height:1.15;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}";
  html += ".state{font-size:12px;font-weight:bold;color:#cfd8dc;margin-top:3px;}";
  html += ".muted{color:#8c969b;}";
  html += ".panel,.card{background:#151b1f;border:1px solid #30393e;border-radius:15px;padding:9px;margin-bottom:9px;}";
  html += ".row{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-bottom:7px;}";
  html += "@media(min-width:850px){.row{grid-template-columns:repeat(4,1fr);}}";
  html += "@media(max-width:600px){.row{grid-template-columns:1fr 1fr;}}";
  html += "label{display:block;font-size:11px;color:#aab4b9;margin-bottom:4px;}";
  html += "input,select{width:100%;height:36px;border-radius:10px;border:1px solid #3e4a50;background:#0d1215;color:white;padding:0 8px;font-size:13px;}";
  html += "input[type=number]::-webkit-inner-spin-button,input[type=number]::-webkit-outer-spin-button{-webkit-appearance:none;margin:0;}";
  html += "input[type=number]{-moz-appearance:textfield;}";
  html += ".manualPin{margin-top:6px;display:none;}";
  html += ".checkRow{display:flex;align-items:center;justify-content:space-between;background:#0d1215;border-radius:10px;padding:8px 10px;margin-bottom:8px;font-size:13px;}";
  html += ".checkRow input{width:20px;height:20px;}";
  html += ".btn{height:38px;border:0;border-radius:10px;font-weight:bold;font-size:13px;color:white;background:#2563eb;padding:0 12px;text-decoration:none;display:inline-flex;align-items:center;justify-content:center;}";
  html += ".btn.gray{background:#374151;}";
  html += ".btn.red{background:#dc2626;}";
  html += ".btn.green{background:#22c55e;}";
  html += ".btn.full{width:100%;height:48px;font-size:16px;margin-top:8px;}";
  html += ".actions{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:8px;}";
  html += ".hidden{display:none!important;}";
  html += ".cardTitle{display:grid;grid-template-columns:1fr auto;gap:8px;margin-bottom:10px;}";
  html += ".warn{background:#2b2114;color:#ffd38a;border:1px solid #77551b;padding:9px;border-radius:12px;margin-bottom:10px;font-size:12px;}";
  html += ".rangeBox{margin-top:8px;}";
  html += ".headerRow{grid-template-columns:1fr;}";
  html += ".macStack{display:grid;grid-template-columns:1fr;gap:8px;}";
  html += ".boardStack{display:grid;grid-template-columns:1fr 1fr;gap:8px;}";
  html += "@media(min-width:760px){.headerRow{grid-template-columns:1.15fr 1fr;align-items:start;}}";
  html += ".testOnly{font-size:12px;color:#aab4b9;margin-top:8px;margin-bottom:6px;text-align:center;font-weight:bold;}";
  html += "</style>";

  html += "</head><body><div class='wrap'>";
  return html;
}

/*
  htmlEnd()

  Cierra las etiquetas HTML abiertas en htmlHead().
*/
String htmlEnd() {
  return "</div></body></html>";
}

// ============================================================
// VISTA GENERAL
// ============================================================

/*
  handleHome()

  Ruta web: GET /

  Muestra el dashboard principal.

  Esta vista está pensada para uso rápido:
    - ver las cards
    - ver si están encendidas o apagadas
    - tocar una card para prender/apagar

  No modifica la configuración, solo controla salidas.
*/
void handleHome() {
  String html = htmlHead("ESP Remote");

  html += "<div class='dashTop'>";
  html += "<div class='dashTitle'>ESP Remote</div>";
  html += "<a class='configBtn' href='/config'>Configuración</a>";
  html += "</div>";

  html += "<div class='grid'>";

  bool hayCards = false;

  for (int i = 0; i < MAX_CARDS; i++) {
    CardConfig &c = config.cards[i];

    if (!c.active) continue;

    hayCards = true;

    bool activo = (config.cards[i].outputType == PIN_DIGITAL)
              ? estadoDigital[i]
              : (estadoAnalogico[i] > 0);

String clase = activo
              ? "mini on c" + String(i % 12)
              : "mini c" + String(i % 12);

    html += "<div class='" + clase + "' id='dashCard" + String(i) + "' onclick='toggleCard(" + String(i) + ")'>";

    html += "<div class='dot'></div>";

    html += "<div class='miniText'>";
    html += "<div class='name'>" + String(c.nombre) + "</div>";
    html += "<div class='state' id='dashState" + String(i) + "'>" + estadoTexto(i) + "</div>";
    html += "</div>";

    html += "</div>";
  }

  if (!hayCards) {
    html += "<a class='mini muted' href='/config'>";
    html += "<div class='dot'></div>";
    html += "<div><div class='name'>Sin cards</div><div class='state'>Agregar configuración</div></div>";
    html += "</a>";
  }

  html += "</div>";

  html += "<script>";
  html += "function toggleCard(id){";
  html += "fetch('/toggle?id='+id).then(r=>r.text()).then(t=>{";
  html += "let card=document.getElementById('dashCard'+id);";
  html += "let state=document.getElementById('dashState'+id);";
  html += "if(!card||!state)return;";
  html += "let activo = t.indexOf('ON')>=0 || t.indexOf('Encendida')>=0 || (t.indexOf('PWM ')>=0 && parseInt(t.replace('PWM ',''))>0); if(activo){card.classList.add('on');}else{card.classList.remove('on');} state.innerHTML=t;";
  html += "});";
  html += "}";
  html += "</script>";

  html += htmlEnd();

  server.send(200, "text/html", html);
}

// ============================================================
// VISTA DETALLE / CONFIG
// ============================================================

/*
  cardConfigHtml(i)

  Genera el HTML de configuración de una card.

  Esta función no guarda nada por sí sola:
  solamente dibuja los campos de la web.

  Luego, cuando el usuario toca "Guardar configuración",
  handleSave() lee esos campos y los guarda en EEPROM.
*/
String cardConfigHtml(int i) {
  CardConfig &c = config.cards[i];

  String display = c.active ? "" : "hidden";

  String html = "";

  html += "<div class='card " + display + "' id='card" + String(i) + "'>";

  html += "<input type='hidden' id='active" + String(i) + "' name='active" + String(i) + "' value='";
  html += c.active ? "1" : "0";
  html += "'>";

  html += "<div class='cardTitle'>";
  html += "<input name='nombre" + String(i) + "' value='" + String(c.nombre) + "'>";
  html += "<button type='button' class='btn red' onclick='deleteCard(" + String(i) + ")'>Eliminar</button>";
  html += "</div>";

  html += "<div class='row'>";

  html += "<div>";
  html += "<label>Tipo de entrada</label>";
  html += "<select name='inputType" + String(i) + "' id='inputType" + String(i) + "' onchange='refreshCard(" + String(i) + ")'>";
  html += "<option value='0' " + selectedInt(c.inputType, PIN_DIGITAL) + ">Digital</option>";
  html += "<option value='1' " + selectedInt(c.inputType, PIN_ANALOGICO) + ">Analógica</option>";
  html += "</select>";
  html += "</div>";

  html += "<div>";
  html += "<label>Pin entrada</label>";
  html += "<input type='hidden' name='inputPin" + String(i) + "' id='inputPin" + String(i) + "' value='" + String(c.inputPin) + "'>";
  html += "<select id='inputPinSelect" + String(i) + "' data-value='" + String(c.inputPin) + "' onchange='changePin(" + String(i) + ",\"input\")'></select>";
  html += "<input class='manualPin' type='number' inputmode='numeric' id='inputPinManual" + String(i) + "' value='" + String(c.inputPin) + "' oninput='manualPin(" + String(i) + ",\"input\")'>";
  html += "</div>";

  html += "<div>";
  html += "<label>Tipo de salida</label>";
  html += "<select name='outputType" + String(i) + "' id='outputType" + String(i) + "' onchange='refreshCard(" + String(i) + ")'>";
  html += "<option value='0' " + selectedInt(c.outputType, PIN_DIGITAL) + ">Digital</option>";
  html += "<option value='1' " + selectedInt(c.outputType, PIN_ANALOGICO) + ">Analógica / PWM</option>";
  html += "</select>";
  html += "</div>";

  html += "<div>";
  html += "<label>Pin salida</label>";
  html += "<input type='hidden' name='outputPin" + String(i) + "' id='outputPin" + String(i) + "' value='" + String(c.outputPin) + "'>";
  html += "<select id='outputPinSelect" + String(i) + "' data-value='" + String(c.outputPin) + "' onchange='changePin(" + String(i) + ",\"output\")'></select>";
  html += "<input class='manualPin' type='number' inputmode='numeric' id='outputPinManual" + String(i) + "' value='" + String(c.outputPin) + "' oninput='manualPin(" + String(i) + ",\"output\")'>";
  html += "</div>";

  html += "</div>";

  html += "<div id='digitalBox" + String(i) + "'>";

  html += "<div class='row'>";
  html += "<div>";
  html += "<label>Modo digital</label>";
  html += "<select name='modo" + String(i) + "'>";
  html += "<option value='1' " + selectedInt(c.modo, MODO_PULSADOR) + ">Pulsador</option>";
  html += "<option value='0' " + selectedInt(c.modo, MODO_LLAVE) + ">Llave</option>";
  html += "</select>";
  html += "</div>";

  html += "<div>";
  html += "<label>Retención en milisegundos</label>";
  html += "<input type='number' name='retencion" + String(i) + "' value='" + String(c.retencion) + "'>";
  html += "</div>";
  html += "</div>";

  html += "</div>";

  html += "<div class='checkRow'>";
  html += "<span>Invertir lógica</span>";
  html += "<input type='checkbox' name='invertir" + String(i) + "' " + checked(c.invertir) + ">";
  html += "</div>";

  html += "<div class='testOnly' id='testOnly" + String(i) + "'>Test de salida</div>";
  html += "<div class='actions digitalTest' id='digitalTest" + String(i) + "'>";
  html += "<button type='button' class='btn' onclick='testDigital(" + String(i) + ",1)'>ON</button>";
  html += "<button type='button' class='btn gray' onclick='testDigital(" + String(i) + ",0)'>OFF</button>";
  html += "</div>";

  html += "<div class='rangeBox pwmTest' id='pwmTest" + String(i) + "'>";
  html += "<label>Test PWM</label>";
  html += "<input type='range' min='0' max='1023' value='" + String(estadoAnalogico[i]) + "' oninput='testAnalog(" + String(i) + ",this.value)'>";
  html += "</div>";

  html += "</div>";

  return html;
}

/*
  handleConfig()

  Ruta web: GET /config

  Muestra la pantalla de configuración completa:
    - filtro MAC
    - placa emisora
    - placa receptora
    - cards configurables
    - botones de test
    - slider PWM

  Es la vista donde se edita el comportamiento del sistema.
*/
void handleConfig() {
  String html = htmlHead("Configuracion ESP Remote");

  html += "<h1>Configuración</h1>";

  html += "<div class='dashTop'>";
  html += "<a class='configBtn' href='/'>Dashboard</a>";
  html += "</div>";

  html += "<div class='warn'>";
  html += "Nota: los pines se filtran según la placa elegida y el tipo digital/analógico/PWM. ";
  html += "Si un pin ya está usado en otra card, aparece deshabilitado para evitar repetirlo.";
  html += "</div>";

  html += "<form action='/save' method='POST'>";

  html += "<div class='panel'>";
  html += "<div class='row headerRow'>";

  html += "<div class='macStack'>";

  html += "<div>";
  html += "<label>Filtro por MAC del emisor</label>";
  html += "<select name='filtroMacActivo'>";
  html += "<option value='0' " + selectedInt(config.filtroMacActivo, 0) + ">Sin filtro: acepta cualquier emisor</option>";
  html += "<option value='1' " + selectedInt(config.filtroMacActivo, 1) + ">Con filtro: acepta solo esta MAC</option>";
  html += "</select>";
  html += "</div>";

  html += "<div>";
  html += "<label>MAC permitida</label>";
  html += "<input name='macPermitida' placeholder='AA:BB:CC:DD:EE:FF' value='" + String(config.macPermitida) + "'>";
  html += "</div>";

  html += "</div>";

  html += "<div class='boardStack'>";

  html += "<div>";
  html += "<label>Placa emisora</label>";
  html += "<select name='placaEmisora' id='placaEmisora' onchange='refreshAllPins()'>";
  html += "<option value='esp32' ";
  if (String(config.placaEmisora) == "esp32") html += "selected";
  html += ">ESP32</option>";
  html += "<option value='esp8266' ";
  if (String(config.placaEmisora) == "esp8266") html += "selected";
  html += ">ESP8266</option>";
  html += "</select>";
  html += "</div>";

  html += "<div>";
  html += "<label>Placa receptora</label>";
  html += "<select name='placaReceptora' id='placaReceptora' onchange='refreshAllPins()'>";
  html += "<option value='esp8266' ";
  if (String(config.placaReceptora) == "esp8266") html += "selected";
  html += ">ESP8266</option>";
  html += "<option value='esp32' ";
  if (String(config.placaReceptora) == "esp32") html += "selected";
  html += ">ESP32</option>";
  html += "</select>";
  html += "</div>";

  html += "</div>";

  html += "</div>";
  html += "</div>";

  html += "<div id='cards'>";

  for (int i = 0; i < MAX_CARDS; i++) {
    html += cardConfigHtml(i);
  }

  html += "</div>";

  html += "<button type='button' class='btn full' onclick='addCard()'>+ Agregar card</button>";
  html += "<button type='submit' class='btn green full'>Guardar configuración</button>";

  html += "</form>";

  html += "<script>";

  html += "const MAX_CARDS=" + String(MAX_CARDS) + ";";

  // Listas de GPIO por placa.
  html += "const pinMap={";
  html += "esp32:{digital:[2,4,5,12,13,14,15,16,17,18,19,21,22,23,25,26,27,32,33],analogIn:[32,33,34,35,36,39],pwm:[2,4,5,12,13,14,15,16,17,18,19,21,22,23,25,26,27,32,33]},";
  html += "esp8266:{digital:[0,2,4,5,12,13,14,15,16],analogIn:[0],pwm:[4,5,12,13,14,15]}";
  html += "};";

  html += "function isActive(i){let a=document.getElementById('active'+i);return a&&a.value=='1';}";

  html += "function usedPins(kind,current){";
  html += "let arr=[];";
  html += "for(let i=0;i<MAX_CARDS;i++){";
  html += "if(i==current||!isActive(i))continue;";
  html += "let h=document.getElementById(kind+'Pin'+i);";
  html += "if(h&&h.value!=='')arr.push(String(h.value));";
  html += "}";
  html += "return arr;";
  html += "}";

  html += "function fillSelect(sel,values,current,used){";
  html += "let forcedManual=sel.getAttribute('data-manual')=='1';";
  html += "let found=false;let h='';";
  html += "values.forEach(p=>{";
  html += "let ps=String(p);";
  html += "if(ps==String(current))found=true;";
  html += "let dis=(used.includes(ps)&&ps!=String(current))?'disabled':'';";
  html += "let selected=(!forcedManual && ps==String(current))?'selected':'';";
  html += "h+=`<option value='${p}' ${selected} ${dis}>GPIO ${p}${dis?' - usado':''}</option>`;";
  html += "});";
  html += "h+=`<option value='manual' ${forcedManual||!found?'selected':''}>Manual</option>`;";
  html += "sel.innerHTML=h;";
  html += "}";

  html += "function refreshPins(i){";
  html += "let pe=document.getElementById('placaEmisora').value;";
  html += "let pr=document.getElementById('placaReceptora').value;";
  html += "let it=document.getElementById('inputType'+i);";
  html += "let ot=document.getElementById('outputType'+i);";
  html += "let ips=document.getElementById('inputPinSelect'+i);";
  html += "let ops=document.getElementById('outputPinSelect'+i);";
  html += "let iph=document.getElementById('inputPin'+i);";
  html += "let oph=document.getElementById('outputPin'+i);";
  html += "if(!it||!ot||!ips||!ops||!iph||!oph)return;";
  html += "let inList=it.value=='1'?pinMap[pe].analogIn:pinMap[pe].digital;";
  html += "let outList=ot.value=='1'?pinMap[pr].pwm:pinMap[pr].digital;";
  html += "fillSelect(ips,inList,iph.value,usedPins('input',i));";
  html += "fillSelect(ops,outList,oph.value,usedPins('output',i));";
  html += "syncManual(i,'input');";
  html += "syncManual(i,'output');";
  html += "}";

  html += "function syncManual(i,kind){";
  html += "let sel=document.getElementById(kind+'PinSelect'+i);";
  html += "let man=document.getElementById(kind+'PinManual'+i);";
  html += "let hid=document.getElementById(kind+'Pin'+i);";
  html += "if(!sel||!man||!hid)return;";
  html += "let isManual=sel.value=='manual'||sel.getAttribute('data-manual')=='1';";
  html += "man.style.display=isManual?'block':'none';";
  html += "if(isManual){sel.value='manual';hid.value=man.value;}else{hid.value=sel.value;man.value=sel.value;}";
  html += "}";

  html += "function changePin(i,kind){";
  html += "let sel=document.getElementById(kind+'PinSelect'+i);";
  html += "if(sel&&sel.value=='manual'){sel.setAttribute('data-manual','1');syncManual(i,kind);return;}";
  html += "if(sel)sel.setAttribute('data-manual','0');";
  html += "syncManual(i,kind);";
  html += "refreshAllPins(false);";
  html += "}";

  html += "function manualPin(i,kind){";
  html += "let sel=document.getElementById(kind+'PinSelect'+i);";
  html += "let man=document.getElementById(kind+'PinManual'+i);";
  html += "let hid=document.getElementById(kind+'Pin'+i);";
  html += "if(sel)sel.setAttribute('data-manual','1');";
  html += "if(man&&hid)hid.value=man.value;";
  html += "}";

  html += "function refreshCard(i){";
  html += "refreshPins(i);";
  html += "let type=document.getElementById('outputType'+i)?.value;";
  html += "let box=document.getElementById('digitalBox'+i);";
  html += "let digital=document.getElementById('digitalTest'+i);";
  html += "let pwm=document.getElementById('pwmTest'+i);";
  html += "let to=document.getElementById('testOnly'+i);";
  html += "if(box)box.style.display=type=='0'?'block':'none';";
  html += "if(digital)digital.style.display=type=='0'?'grid':'none';";
  html += "if(pwm)pwm.style.display=type=='1'?'block':'none';";
  html += "if(to)to.style.display=type=='0'?'block':'none';";
  html += "refreshAllPins(false);";
  html += "}";

  html += "function refreshAllPins(loop=true){";
  html += "for(let i=0;i<MAX_CARDS;i++){";
  html += "if(document.getElementById('card'+i)){";
  html += "refreshPins(i);";
  html += "let type=document.getElementById('outputType'+i)?.value;";
  html += "let box=document.getElementById('digitalBox'+i);";
  html += "let digital=document.getElementById('digitalTest'+i);";
  html += "let pwm=document.getElementById('pwmTest'+i);";
  html += "let to=document.getElementById('testOnly'+i);";
  html += "if(box)box.style.display=type=='0'?'block':'none';";
  html += "if(digital)digital.style.display=type=='0'?'grid':'none';";
  html += "if(pwm)pwm.style.display=type=='1'?'block':'none';";
  html += "if(to)to.style.display=type=='0'?'block':'none';";
  html += "}";
  html += "}";
  html += "}";

  html += "function addCard(){";
  html += "for(let i=0;i<MAX_CARDS;i++){";
  html += "let card=document.getElementById('card'+i);let active=document.getElementById('active'+i);";
  html += "if(card&&active&&active.value=='0'){card.classList.remove('hidden');active.value='1';refreshAllPins();return;}";
  html += "}";
  html += "alert('Llegaste al máximo técnico de cards');";
  html += "}";

  html += "function deleteCard(i){document.getElementById('active'+i).value='0';document.getElementById('card'+i).classList.add('hidden');refreshAllPins();}";
  html += "function testDigital(i,v){fetch('/testDigital?id='+i+'&v='+v);}";
  html += "function testAnalog(i,v){fetch('/testAnalog?id='+i+'&v='+v);}";

  html += "refreshAllPins();";

  html += "</script>";

  html += htmlEnd();

  server.send(200, "text/html", html);
}

// ============================================================
// RUTAS WEB DE ACCIÓN
// ============================================================

/*
  handleSave()

  Ruta web: POST /save

  Se ejecuta cuando el usuario toca "Guardar configuración".

  Lee todos los campos enviados por el formulario HTML,
  actualiza la estructura config y luego la guarda en EEPROM.
*/
void handleSave() {
  config.filtroMacActivo = server.arg("filtroMacActivo") == "1";

  String mac = server.arg("macPermitida");
  mac.trim();
  mac.toUpperCase();
  mac.toCharArray(config.macPermitida, 18);

  // Guardamos qué placa eligió el usuario para emisor y receptor.
  // Esto se usa para cargar los selectores de GPIO en la web.
  server.arg("placaEmisora").toCharArray(config.placaEmisora, 10);
  server.arg("placaReceptora").toCharArray(config.placaReceptora, 10);

  for (int i = 0; i < MAX_CARDS; i++) {
    CardConfig &c = config.cards[i];

    c.active = server.arg("active" + String(i)) == "1";

    if (!c.active) continue;

    server.arg("nombre" + String(i)).toCharArray(c.nombre, 28);

    c.inputType = server.arg("inputType" + String(i)).toInt();
    c.inputPin = server.arg("inputPin" + String(i)).toInt();

    c.outputType = server.arg("outputType" + String(i)).toInt();
    c.outputPin = server.arg("outputPin" + String(i)).toInt();

    c.modo = server.arg("modo" + String(i)).toInt();

    c.invertir = server.hasArg("invertir" + String(i));

    unsigned long r = server.arg("retencion" + String(i)).toInt();
    c.retencion = r > 0 ? r : 1000;

    prepararSalida(c.outputPin);
  }

  guardarConfig();

  server.sendHeader("Location", "/config");
  server.send(303);
}

/*
  handleToggle()

  Ruta web: GET /toggle?id=X

  Se usa desde el dashboard cuando se toca una card.

  Cambia el estado de la salida:
    - si estaba apagada, la prende
    - si estaba prendida, la apaga

  Responde con texto simple para que la web pueda actualizar la card
  sin recargar toda la página.
*/
void handleToggle() {
  int id = server.arg("id").toInt();

  if (id < 0 || id >= MAX_CARDS || !config.cards[id].active) {
    server.send(400, "text/plain", "ERROR");
    return;
  }

  if (config.cards[id].outputType == PIN_DIGITAL) {
    setSalidaDigital(id, !estadoDigital[id]);
  } else {
    if (estadoAnalogico[id] > 0) {
      setSalidaAnalogica(id, 0);
    } else {
      setSalidaAnalogica(id, 1023);
    }
  }

  server.send(200, "text/plain", estadoTexto(id));
}

/*
  handleTestDigital()

  Ruta web: GET /testDigital?id=X&v=0/1

  Se usa desde la pantalla de configuración.

  Sirve para probar una salida digital sin depender del emisor.
  No guarda configuración, solo fuerza momentáneamente la salida.
*/
void handleTestDigital() {
  int id = server.arg("id").toInt();
  bool v = server.arg("v").toInt() == 1;

  if (id >= 0 && id < MAX_CARDS && config.cards[id].active) {
    if (config.cards[id].outputType == PIN_DIGITAL) {
      setSalidaDigital(id, v);
    } else {
      setSalidaAnalogica(id, v ? 1023 : 0);
    }
  }

  server.send(200, "text/plain", "OK");
}

/*
  handleTestAnalog()

  Ruta web: GET /testAnalog?id=X&v=VALOR

  Se usa para probar una salida PWM desde el slider.

  VALOR va de 0 a 1023.
*/
void handleTestAnalog() {
  int id = server.arg("id").toInt();
  int v = server.arg("v").toInt();

  if (id >= 0 && id < MAX_CARDS && config.cards[id].active) {
    setSalidaAnalogica(id, v);
  }

  server.send(200, "text/plain", "OK");
}

// ============================================================
// SETUP
// ============================================================

/*
  setup()

  Se ejecuta una sola vez al encender o reiniciar la ESP8266.

  Orden de inicialización:
    1. Inicia Serial para debug.
    2. Inicia EEPROM.
    3. Carga configuración guardada.
    4. Configura pines de salida.
    5. Crea el WiFi propio.
    6. Inicia ESP-NOW.
    7. Registra las rutas web.
    8. Inicia el servidor web.
*/
void setup() {
  Serial.begin(115200);

  EEPROM.begin(EEPROM_SIZE);
  cargarConfig();

  for (int i = 0; i < MAX_CARDS; i++) {
    estadoDigital[i] = false;
    estadoAnalogico[i] = 0;
    tiempoApagado[i] = 0;

    if (config.cards[i].active) {
      prepararSalida(config.cards[i].outputPin);
    }
  }

  WiFi.mode(WIFI_AP_STA);

  String wifiName = getWifiName();

  WiFi.softAP(
    wifiName.c_str(),
    WIFI_PASS,
    ESPNOW_CHANNEL
  );

  wifi_set_channel(ESPNOW_CHANNEL);

  Serial.println("");
  Serial.println("====================================");
  Serial.println("ESP8266 RECEPTOR LISTO");
  Serial.print("WiFi: ");
  Serial.println(wifiName);
  Serial.print("Clave: ");
  Serial.println(WIFI_PASS);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("MAC ESP8266: ");
  Serial.println(WiFi.macAddress());
  Serial.println("====================================");

  if (esp_now_init() != 0) {
    Serial.println("ERROR: no se pudo iniciar ESP-NOW");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(onDataRecv);

  server.on("/", handleHome);
  server.on("/config", handleConfig);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/toggle", handleToggle);
  server.on("/testDigital", handleTestDigital);
  server.on("/testAnalog", handleTestAnalog);

  server.begin();

  Serial.println("WEB OK");
  Serial.println("ESPNOW OK");
}

// ============================================================
// LOOP
// ============================================================

/*
  loop()

  Se ejecuta permanentemente.

  Hace dos cosas:
    1. Atiende clientes web con server.handleClient().
    2. Revisa si alguna salida en modo pulsador debe apagarse
       porque ya terminó su tiempo de retención.
*/
void loop() {
  server.handleClient();

  // Revisión de salidas en modo pulsador.
  for (int i = 0; i < MAX_CARDS; i++) {
    CardConfig &c = config.cards[i];

    if (!c.active) continue;
    if (c.outputType != PIN_DIGITAL) continue;
    if (c.modo != MODO_PULSADOR) continue;

    if (estadoDigital[i] && tiempoApagado[i] > 0) {
      if (millis() > tiempoApagado[i]) {
        setSalidaDigital(i, false);
        tiempoApagado[i] = 0;
      }
    }
  }
}
