# ESP Remote - Sistema de Control Inalámbrico mediante ESP-NOW

## Introducción

ESP Remote es un sistema de automatización distribuido desarrollado utilizando una ESP32 como dispositivo emisor y una ESP8266 como dispositivo receptor.

El objetivo principal es permitir la asociación dinámica entre entradas físicas y salidas remotas sin necesidad de recompilar el firmware cada vez que se desea modificar una configuración.

ESP Remote fue concebido como una herramienta orientada a artistas, performers, músicos, realizadores audiovisuales, estudiantes y usuarios sin conocimientos avanzados de programación.

La propuesta busca que la configuración del sistema pueda realizarse desde una interfaz web, evitando la necesidad de modificar o recompilar el código para cada nuevo montaje o instalación.

La comunicación entre dispositivos se realiza mediante el protocolo ESP-NOW, mientras que la configuración del sistema se lleva a cabo a través de una interfaz web embebida en la ESP8266.

---

# Índice

1. Introducción
2. Objetivos
3. Arquitectura del sistema
4. Tecnologías utilizadas
5. Hardware utilizado
6. Instalación del entorno
7. Funcionamiento general
8. Comunicación ESP-NOW
9. Modo Broadcast
10. Filtro por MAC Address
11. Interfaz Web
12. Concepto de Cards
13. Conexión electrónica
14. Programación de las placas
15. Puesta en marcha
16. Ejemplo de configuración
17. Pruebas realizadas
18. Posibles mejoras futuras
19. Autores

---

# Objetivos

- Implementar comunicación inalámbrica de baja latencia mediante ESP-NOW.
- Permitir configuración sin recompilar código.
- Incorporar almacenamiento persistente mediante EEPROM.
- Permitir control digital y PWM.
- Aplicar conceptos de sistemas embebidos, redes inalámbricas e interfaces web.
- Permitir que usuarios sin conocimientos de programación puedan configurar el sistema desde una interfaz web.
- Reducir la necesidad de modificar firmware para adaptar una instalación a distintos usos.
- Facilitar la utilización de tecnologías embebidas en proyectos artísticos e interactivos.

---

# Arquitectura del sistema

ESP32 (Emisor)
↓
ESP-NOW Broadcast
↓
ESP8266 (Receptor)
↓
Dashboard + Configuración Web + Salidas

---

# Tecnologías utilizadas

## Hardware

- ESP32
- ESP8266 NodeMCU
- LEDs
- Pulsadores
- Potenciómetro
- Resistencias

## Software

- Arduino IDE
- ESP-NOW
- EEPROM
- HTML
- CSS
- JavaScript

---

# Materiales utilizados durante la iniciación del prototipo

Los siguientes componentes fueron utilizados únicamente para la construcción y validación del prototipo presentado en este repositorio.

No representan una limitación del sistema, ya que posteriormente pueden utilizarse otros sensores, actuadores o dispositivos compatibles con los GPIO disponibles.

- 1 ESP32
- 1 ESP8266
- 3 Pulsadores
- 1 Potenciómetro
- LEDs de prueba
- Resistencias de 220Ω

---

# Instalación del entorno

Para compilar el proyecto se utilizó Arduino IDE.

## Instalación ESP32

1. Abrir Arduino IDE.
2. Ir a Herramientas → Placa → Gestor de placas.
3. Buscar:

esp32 by Espressif Systems

4. Instalar la última versión disponible.

## Instalación ESP8266

1. Abrir Arduino IDE.
2. seguir pasos de esta web: https://naylampmechatronics.com/blog/56_usando-esp8266-con-el-ide-de-arduino.html

---

# Funcionamiento general

La ESP32 lee entradas físicas como pulsadores y potenciómetros.

Cuando detecta un cambio:

- genera un mensaje ESP-NOW
- envía el mensaje
- la ESP8266 lo recibe
- se ejecuta la salida asociada

Las asociaciones entre entradas y salidas se configuran desde la web.

---

# Comunicación ESP-NOW

Cada mensaje enviado contiene:

- Tipo de entrada
- GPIO de origen
- Valor digital
- Valor analógico

Esto permite que el receptor identifique exactamente qué entrada generó el evento.

---

# Modo Broadcast

El emisor transmite utilizando:

FF:FF:FF:FF:FF:FF

No necesita conocer la MAC del receptor.

Cualquier receptor que esté escuchando en el mismo canal puede recibir el mensaje.

---

# Filtro por MAC Address

El receptor puede:

- aceptar cualquier emisor
- aceptar únicamente una MAC específica

Esto permite trabajar tanto en modo abierto como en modo restringido.

---

# Interfaz Web

La ESP8266 crea una red WiFi propia:

ESP-REMOTE-XXXX ( siendo XXXX los ultimos digitos de la MAC-ADRESS)

Contraseña:

12345678

Acceso:

192.168.4.1

La interfaz permite:

- Crear cards
- Editar cards
- Eliminar cards
- Probar salidas
- Configurar filtro MAC
- Configurar entradas y salidas

---

# Concepto de Cards

Cada card representa una relación entre una entrada y una salida.

Ejemplo:

GPIO32 (ESP32) → GPIO5 (ESP8266)

Las cards pueden trabajar en:

- modo pulsador
- modo llave
- salida digital
- salida PWM
- lógica invertida
- retención temporizada

---

# Conexión electrónica

⚠️ Importante

Las conexiones mostradas a continuación corresponden exclusivamente al montaje utilizado para las pruebas iniciales del sistema.

El objetivo de este proyecto no es controlar específicamente LEDs o potenciómetros, sino proporcionar una plataforma configurable de comunicación entre entradas y salidas.

Los GPIO utilizados durante las pruebas pueden reemplazarse posteriormente por sensores, actuadores, relés, iluminación, motores u otros dispositivos compatibles.

⚠️ Las conexiones mostradas a continuación corresponden únicamente a la configuración utilizada durante las pruebas y validación del sistema.

Los GPIO pueden modificarse desde la interfaz web según las necesidades de cada implementación.

## ESP32 utilizada durante las pruebas

GPIO32 ---- Pulsador ---- GND

GPIO33 ---- Pulsador ---- GND

GPIO25 ---- Pulsador ---- GND

Potenciómetro:

3V3 ---- extremo

GND ---- extremo

GPIO34 ---- pin medio

## ESP8266 utilizada durante las pruebas

GPIO5 ---- 220Ω ---- LED ---- GND

GPIO4 ---- 220Ω ---- LED ---- GND

GPIO14 ---- 220Ω ---- LED ---- GND

---

# Programación de las placas

## Receptor

Archivo:

ESPNOW_PULSADORES_RECEPTOR_V3

Placa:

NodeMCU 1.0 (ESP-12E Module)

## Emisor

Archivo:

ESPNOW_PULSADORES_EMISOR_V3

Placa:

ESP32 Dev Module

---

# Puesta en marcha

1. Cargar el firmware en ambas placas.
2. Encender ambas placas.
3. Conectarse al WiFi generado por la ESP8266.
4. Abrir 192.168.4.1.
5. Crear las cards deseadas.
6. Guardar configuración.
7. Realizar pruebas.

---

# Ejemplo de configuración

Card digital:

GPIO32 → GPIO5

Modo pulsador

Retención: 1000 ms

Card PWM:

GPIO34 → GPIO14

---

# Aplicaciones posibles

El sistema fue pensado especialmente para proyectos donde la flexibilidad de configuración es más importante que la programación.

Algunos ejemplos:

- Instalaciones artísticas interactivas.
- Performances audiovisuales.
- Instrumentos musicales experimentales.
- Automatizaciones simples.
- Prototipos de interacción física.
- Experiencias educativas relacionadas con electrónica creativa.
- Control inalámbrico de iluminación.
- Activación remota de dispositivos electrónicos.
- Domotica

---

# Pruebas realizadas

- Comunicación ESP-NOW Broadcast.
- Control digital mediante pulsadores.
- Control PWM mediante potenciómetro.
- Dashboard web.
- Configuración persistente en EEPROM.
- Filtro MAC.
- Asociación dinámica de GPIOs.


---

# Autor

Proyecto académico desarrollado por Gonzalo Lucero en el marco del grupo de estudio de IoT de Artes Electrónicas en UNTREF 2026
