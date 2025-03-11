#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "driver/rtc_io.h"

// Definiciones de Estados
#define ESTADO_SINCRONIZACION  0
#define ESTADO_LECTURA         1
#define ESTADO_REPOSO          2  // Implementa Deep Sleep

// Definiciones para BLE
#define DEVICE_NAME        "BarraInteligentev6"
#define SERVICE_UUID       "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID_DIST "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHARACTERISTIC_UUID_BAT  "d9d9fbb3-03a5-4bda-9e44-916ffb9470b1"

// Definiciones para el sensor ultrasónico
#define TRIGGER_PIN        D3
#define ECHO_PIN           D5
#define MAX_DISTANCE       100  // Distancia máxima permitida en cm

// Tiempos en milisegundos
#define TIEMPO_SINCRONIZACION  1 * 60 * 1000  // Simulación de 1 minuto en vez de 30
#define TIEMPO_SIN_LECTURAS    2 * 60 * 1000  // 5 minutos sin lecturas para ir a reposo
#define SAMPLE_INTERVAL        50  // Intervalo de muestreo del sensor (50ms)

// Definiciones para el LED
#define LED_ESTADO          A2
#define FADE_STEP           5  // Velocidad de cambio de brillo en sincronización
#define LED_LECTURA_INTENSIDAD  50  // 20% de 255 (PWM en ESP32)


// Definiciones para la batería
#define BATT_PIN           A9  
#define BATT_MAX_VOLT      4.2  
#define BATT_MIN_VOLT      3.0  

// Definiciones para Deep Sleep
#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)
#define WAKEUP_GPIO GPIO_NUM_1  // Botón en GPIO 1 para despertar

RTC_DATA_ATTR int bootCount = 0;




// Variables globales
BLECharacteristic *pCharacteristicDist;
BLECharacteristic *pCharacteristicBat;
BLEServer *pServer;
BLEAdvertising *pAdvertising;
bool deviceConnected = false;
uint8_t estadoActual = ESTADO_SINCRONIZACION;

unsigned long tiempoInicioEstado = 0;
unsigned long tiempoUltimaLectura = 0;
unsigned long previousMillis = 0;
unsigned long previousMillisBat = 0;

uint8_t bateria = 0;

// Variables para controlar el LED en estado de sincronización
int ledBrillo = 0;
bool aumentando = true;

// Clase para manejar eventos de conexión BLE
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("Dispositivo BLE conectado.");
  }

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    Serial.println("Dispositivo BLE desconectado. Reanudando publicidad...");
    pAdvertising->start();
  }
};

// Función para medir la distancia con el sensor ultrasónico
uint8_t medirDistancia() {
    digitalWrite(TRIGGER_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIGGER_PIN, LOW);
    
    long t = pulseIn(ECHO_PIN, HIGH);
    long d = t / 59; // Conversión a cm

    return (d > MAX_DISTANCE) ? MAX_DISTANCE : (uint8_t)d;
}

// Función para hacer parpadear el LED 3 veces antes de dormir
void blinkLED() {
    pinMode(LED_ESTADO, OUTPUT);
    for (int i = 0; i < 3; i++) {
        analogWrite(LED_ESTADO, 255);
        delay(500);
        analogWrite(LED_ESTADO, 0);
        delay(500);
    }
}

// Función para imprimir la razón de despertar
void print_wakeup_reason() {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0: Serial.println("Wakeup by button (EXT0)"); break;
        case ESP_SLEEP_WAKEUP_EXT1: Serial.println("Wakeup by multiple GPIOs (EXT1)"); break;
        case ESP_SLEEP_WAKEUP_TIMER: Serial.println("Wakeup by timer"); break;
        default: Serial.printf("Wakeup reason: %d\n", wakeup_reason); break;
    }
}

// Función para activar el modo Deep Sleep
void entrarEnDeepSleep() {
    Serial.println("Entrando en modo Deep Sleep...");
    
    // Parpadea antes de dormir
    blinkLED();

    // Configurar el GPIO para despertar en HIGH
    esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 1);
    rtc_gpio_pullup_dis(WAKEUP_GPIO);
    rtc_gpio_pulldown_en(WAKEUP_GPIO);

    // Ir a Deep Sleep
    esp_deep_sleep_start();
}

// Función para leer el porcentaje de batería
uint8_t leerBateria() {
    int lecturaADC = analogRead(BATT_PIN);
    float voltaje = (lecturaADC * 3.3) / 4095.0;  // Convertir ADC a voltaje

    // Ajustar por divisor de voltaje (R1 = R2 -> multiplicar por 2)
    voltaje *= 2.0;

    // Ajustar caída de voltaje del módulo de carga si es necesario (prueba con multímetro)
    voltaje += 0.2;

    // Interpolación basada en curva real de descarga
    uint8_t porcentaje;
    if (voltaje >= 4.2) porcentaje = 100;
    else if (voltaje >= 4.0) porcentaje = map(voltaje * 100, 400, 420, 80, 100);
    else if (voltaje >= 3.7) porcentaje = map(voltaje * 100, 370, 400, 50, 80);
    else if (voltaje >= 3.4) porcentaje = map(voltaje * 100, 340, 370, 20, 50);
    else if (voltaje >= 3.0) porcentaje = map(voltaje * 100, 300, 340, 0, 20);
    else porcentaje = 0;

    Serial.print("Batería: ");
    Serial.print(voltaje);
    Serial.print("V -> ");
    Serial.print(porcentaje);
    Serial.println("%");

    return constrain(porcentaje, 0, 100);
}

void setup() {
    Serial.begin(115200);
    Serial.println("Iniciando Máquina de Estados");
    
    // Imprimir razón de despertar
    print_wakeup_reason();

    // Configuración de pines
    pinMode(TRIGGER_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(LED_ESTADO, OUTPUT);
    pinMode(BATT_PIN, INPUT);
    
    digitalWrite(TRIGGER_PIN, LOW);

    // Inicializar BLE
    BLEDevice::init(DEVICE_NAME);
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);

    pCharacteristicDist = pService->createCharacteristic(
        CHARACTERISTIC_UUID_DIST,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
    );
    pCharacteristicBat = pService->createCharacteristic(
        CHARACTERISTIC_UUID_BAT,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
    );

    pCharacteristicDist->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
    pCharacteristicBat->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);

    uint8_t initialValue[1] = {0};
    pCharacteristicDist->setValue(initialValue, 1);
    pCharacteristicBat->setValue(initialValue, 1);

    pService->start();
    pAdvertising = pServer->getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->start();

    // Inicializar tiempos
    tiempoInicioEstado = millis();
    tiempoUltimaLectura = millis();
}

void loop() {
    unsigned long tiempoActual = millis();
    switch (estadoActual) {
        case ESTADO_SINCRONIZACION:
            Serial.println("Estado: Sincronización de dispositivo...");
            if (tiempoActual - previousMillis >= 30) {
                previousMillis = tiempoActual;
                analogWrite(LED_ESTADO, ledBrillo);
                ledBrillo += (aumentando ? FADE_STEP : -FADE_STEP);
                if (ledBrillo >= 100 || ledBrillo <= 0) {
                    aumentando = !aumentando;
                }
            }

            if (deviceConnected) {
                Serial.println("Dispositivo BLE sincronizado. Cambiando a modo lectura.");
                estadoActual = ESTADO_LECTURA;
                bateria = leerBateria();
            } else if (tiempoActual - tiempoInicioEstado >= TIEMPO_SINCRONIZACION) {
                Serial.println("Tiempo de espera agotado. Pasando a estado de lectura.");
                estadoActual = ESTADO_LECTURA;
            }
            break;

        case ESTADO_LECTURA:
            analogWrite(LED_ESTADO, LED_LECTURA_INTENSIDAD);


            if (tiempoActual - previousMillisBat >= SAMPLE_INTERVAL*8){
                bateria = leerBateria();
                previousMillisBat = tiempoActual;
                if (deviceConnected){
                      pCharacteristicBat->setValue(&bateria, 1);
                      pCharacteristicBat->notify();
                }
            }

            if (tiempoActual - previousMillis >= SAMPLE_INTERVAL) {
                
                previousMillis = tiempoActual;
                uint8_t distancia = medirDistancia();
                Serial.print("Distancia: ");
                Serial.print(distancia);
                Serial.println(" cm");

                if (distancia < MAX_DISTANCE / 3) {
                    tiempoUltimaLectura = tiempoActual;
                }

                if (deviceConnected) {
                    pCharacteristicDist->setValue(&distancia, 1);
                    pCharacteristicDist->notify();

                }
            }

            if (tiempoActual - tiempoUltimaLectura >= TIEMPO_SIN_LECTURAS) {
                Serial.println("Tiempo sin lecturas agotado. Pasando a estado de reposo.");
                estadoActual = ESTADO_REPOSO;
            }
            break;

        case ESTADO_REPOSO:
            Serial.println("Estado: Reposo. Activando Deep Sleep.");
            entrarEnDeepSleep();
            break;
    }
}
