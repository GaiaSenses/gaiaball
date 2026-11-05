/****************************************************************
 * Example6_DMP_Quat9_Orientation.ino
 * ICM 20948 + BLE + CO2 UART + INMP441(I2S) + Pot(ADC1) + LEDs
 * Versão estabilizada mantendo compatibilidade com o site
 *06/05/2026 Dados simplificado feito pelo Felipe



 ****************************************************************/
#define QUAT_ANIMATION

#include "ICM_20948.h"
//#define USE_SPI

#define SERIAL_PORT Serial

#define SPI_PORT SPI
#define CS_PIN 2

#define WIRE_PORT Wire
#define AD0_VAL 0

#ifdef USE_SPI
ICM_20948_SPI myICM;
#else
ICM_20948_I2C myICM;
#endif

// ================= BLE =================
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>

BLEServer* pServer = NULL;
BLECharacteristic* pServerDummy = NULL;
BLECharacteristic* pSensorCharacteristic = NULL;
BLECharacteristic* pLedCharacteristic = NULL;
BLECharacteristic* pCO2Characteristic = NULL;

bool deviceConnected = false;
bool oldDeviceConnected = false;

String value;
String co2ppmValue;

const int ledPin = 2;

// UUIDs
#define SERVICE_UUID                 "19b10000-e8f2-537e-4f6c-d104768a1214"
#define SENSOR_CHARACTERISTIC_UUID   "19b10001-e8f2-537e-4f6c-d104768a1214"
#define LED_CHARACTERISTIC_UUID      "19b10002-e8f2-537e-4f6c-d104768a1214"
#define CO2_CHARACTERISTIC_UUID      "19b10003-e8f2-537e-4f6c-d104768a1214"

class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("✅ BLE: Cliente conectado!");
  }
  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("❌ BLE: Cliente desconectado");
  }
};

class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pLedCharacteristic) override {
    String v = pLedCharacteristic->getValue();
    if (v.length() > 0) {
      int receivedValue = (int)v[0];
      Serial.print("BLE LED cmd: ");
      Serial.println(receivedValue);
      digitalWrite(ledPin, receivedValue == 1 ? HIGH : LOW);
    }
  }
};

// ================= CO2 UART =================
#include <HardwareSerial.h>
HardwareSerial co2Serial(1);

const int RX_PIN = 16;  // sensor TX -> ESP32 RX
const int TX_PIN = 17;  // sensor RX <- ESP32 TX

// ================= I2S MIC + ADC POT =================
#include <driver/i2s.h>
#include <driver/adc.h>

// INMP441 (I2S)
#define I2S_WS   25
#define I2S_SCK  26
#define I2S_SD   33

// Potenciômetro: GPIO34 = ADC1_CHANNEL_6
#define POT_PIN 34

// LEDs
#define LED_SOM_PIN 2   // Vermelho (som) - mantido no GPIO2
#define LED_CO2_PIN 4   // Verde (CO2 real > 2000)

// I2S
#define I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 16000
#define BUFFER_LEN  256
#define I2S_TIMEOUT_MS 10

// Lógica do site (CO2 "virtual" quando pico de áudio)
#define CO2_PICO_VALUE        2100
#define CO2_NORMAL_VALUE       600

// ===== CONTROLE DO DISPARO =====
#define LED_SOM_TEMPO_MS       100
#define CO2_TRIGGER_DELAY_MS  1000
#define CO2_TRIGGER_HOLD_MS  17000

#define DEBOUNCE_MS            300

// ===== AJUSTE DO POTENCIÔMETRO =====
#define THRESHOLD_MIN         10000
#define THRESHOLD_MAX        500000

// Tarefas por tempo
#define AUDIO_PERIOD_MS     20   // 50 Hz
#define POT_PERIOD_MS      100   // 10 Hz
#define CO2_PERIOD_MS      500   // 2 Hz

// ===== PLOT =====
#define PLOT_ENABLED 1
#define PLOT_PERIOD_MS 50

float micLevel = 0.0f;
float thresholdAtual = THRESHOLD_MIN;
uint32_t ultimoTrigger = 0;
uint32_t ledSomAte = 0;
bool ledSomEstado = false;

uint16_t co2_para_site = CO2_NORMAL_VALUE;
uint16_t co2_real = 0;

// ===== CONTROLE DO EVENTO VIRTUAL =====
bool co2PulsoAgendado = false;
bool co2PulsoAtivo = false;
uint32_t co2PulsoInicio = 0;
uint32_t co2PulsoFim = 0;

// ✅ Microfone: baseline + delta
float micBaseline = 0.0f;
float micDeltaRaw = 0.0f;

// ✅ IIR Filter
float micDeltaFilt = 0.0f;
#define MIC_IIR_ALPHA 0.99f

bool eventoVirtualEmAndamento() {
  return co2PulsoAgendado || co2PulsoAtivo;
}

// ================= ADC1 configurado (sem analogRead) =================
void configurarADC() {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11); // GPIO34
}

int lerPotenciometro() {
  int valorADC = adc1_get_raw(ADC1_CHANNEL_6); // 0..4095
  return map(valorADC, 0, 4095, THRESHOLD_MIN, THRESHOLD_MAX);
}

// ================= I2S =================
void setupI2S() {
  i2s_config_t i2s_config = {};
  i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  i2s_config.sample_rate = SAMPLE_RATE;
  i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2s_config.communication_format = I2S_COMM_FORMAT_I2S;
  i2s_config.intr_alloc_flags = 0;
  i2s_config.dma_buf_count = 4;
  i2s_config.dma_buf_len = BUFFER_LEN;
  i2s_config.use_apll = false;

  i2s_pin_config_t pin_config = {};
  pin_config.bck_io_num = I2S_SCK;
  pin_config.ws_io_num = I2S_WS;
  pin_config.data_out_num = -1;
  pin_config.data_in_num = I2S_SD;

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, nullptr);
  if (err != ESP_OK) {
    Serial.printf("❌ Erro i2s_driver_install: %d\n", err);
    return;
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("❌ Erro i2s_set_pin: %d\n", err);
    return;
  }

  i2s_zero_dma_buffer(I2S_PORT);
  Serial.println("✅ I2S (2x INMP441 stereo) OK");
}

float lerNivelAudio() {
  // Stereo: dois INMP441 no mesmo barramento I2S.
  // Um microfone com L/R no GND e outro com L/R no 3V3.
  static int32_t samples[BUFFER_LEN * 2];
  size_t bytesRead = 0;

  esp_err_t err = i2s_read(
    I2S_PORT,
    (void*)samples,
    sizeof(samples),
    &bytesRead,
    pdMS_TO_TICKS(I2S_TIMEOUT_MS)
  );

  if (err != ESP_OK || bytesRead == 0) return 0.0f;

  int count = bytesRead / 4;
  if (count <= 1) return 0.0f;

  double somaLeft = 0;
  double somaRight = 0;
  int nLeft = 0;
  int nRight = 0;

  // Em stereo, as amostras chegam intercaladas:
  // esquerda, direita, esquerda, direita...
  for (int i = 0; i < count - 1; i += 2) {
    int32_t sLeft = samples[i] >> 8;
    int32_t sRight = samples[i + 1] >> 8;

    if (sLeft < 0) sLeft = -sLeft;
    if (sRight < 0) sRight = -sRight;

    somaLeft += (double)sLeft;
    somaRight += (double)sRight;

    nLeft++;
    nRight++;
  }

  float mediaLeft = (nLeft > 0) ? (float)(somaLeft / nLeft) : 0.0f;
  float mediaRight = (nRight > 0) ? (float)(somaRight / nRight) : 0.0f;

  // Mantém a lógica original usando apenas um nível de áudio.
  // Para captar dos dois lados da placa, usa o maior nível detectado.
  return (mediaLeft > mediaRight) ? mediaLeft : mediaRight;
}

void calibrarMicrofone() {
  Serial.println("🎤 Calibrando ruído do microfone (2s)... fique em silêncio");
  uint32_t t0 = millis();
  double soma = 0;
  int n = 0;

  while (millis() - t0 < 2000) {
    float v = lerNivelAudio();
    if (v > 0) {
      soma += v;
      n++;
    }
    delay(5);
  }

  micBaseline = (n > 0) ? (float)(soma / n) : 0.0f;
  micDeltaFilt = 0.0f;
  Serial.printf("✅ Baseline mic = %.0f\n", micBaseline);
}

void atualizarPulsoCO2(uint32_t agora) {
  // entra em 2100 após o atraso
  if (co2PulsoAgendado && agora >= co2PulsoInicio) {
    co2PulsoAgendado = false;
    co2PulsoAtivo = true;
    co2PulsoFim = agora + CO2_TRIGGER_HOLD_MS;

    co2_para_site = CO2_PICO_VALUE;

    // LED fica aceso enquanto o CO2 virtual estiver ativo
    digitalWrite(LED_SOM_PIN, HIGH);
    ledSomEstado = true;

    Serial.printf("🟢 CO2 virtual ATIVO: %u por %ums\n", CO2_PICO_VALUE, CO2_TRIGGER_HOLD_MS);
  }

  if (co2PulsoAtivo) {
    if (agora < co2PulsoFim) {
      co2_para_site = CO2_PICO_VALUE;
      digitalWrite(LED_SOM_PIN, HIGH);
      ledSomEstado = true;
    } else {
      co2PulsoAtivo = false;
      co2_para_site = (co2_real > 0) ? co2_real : CO2_NORMAL_VALUE;

      digitalWrite(LED_SOM_PIN, LOW);
      ledSomEstado = false;

      Serial.println("🔵 CO2 virtual FINALIZADO, retornando ao valor real");
    }
  }
}

void detectarPicoAudio(uint32_t agora) {
  if (micDeltaFilt > thresholdAtual &&
      (agora - ultimoTrigger) > DEBOUNCE_MS &&
      !eventoVirtualEmAndamento()) {

    ultimoTrigger = agora;

    digitalWrite(LED_SOM_PIN, HIGH);
    ledSomEstado = true;
    ledSomAte = agora + LED_SOM_TEMPO_MS;

    co2PulsoAgendado = true;
    co2PulsoAtivo = false;
    co2PulsoInicio = agora + CO2_TRIGGER_DELAY_MS;
    co2PulsoFim = 0;

    Serial.printf("🔊 PICO! raw=%.0f filt=%.0f th=%.0f | LED=%ums | CO2 virtual em %ums por %ums\n",
                  micDeltaRaw, micDeltaFilt, thresholdAtual,
                  LED_SOM_TEMPO_MS, CO2_TRIGGER_DELAY_MS, CO2_TRIGGER_HOLD_MS);
  }

  if (ledSomEstado && agora >= ledSomAte && !co2PulsoAtivo) {
    digitalWrite(LED_SOM_PIN, LOW);
    ledSomEstado = false;
  }
}

void lerCO2Real_periodico(uint32_t agora) {
  static uint32_t ultimaLeitura = 0;
  if (agora - ultimaLeitura < CO2_PERIOD_MS) return;
  ultimaLeitura = agora;

  if (co2Serial.available() < 16) return;

  uint8_t buffer[16];
  co2Serial.readBytes(buffer, 16);

  if (buffer[0] == 0x42 && buffer[1] == 0x4D) {
    uint8_t soma = 0;
    for (int i = 0; i < 15; i++) soma += buffer[i];

    if (soma == buffer[15]) {
      co2_real = (buffer[6] << 8) | buffer[7];

      digitalWrite(LED_CO2_PIN, (co2_real > 2000) ? HIGH : LOW);

      if (!eventoVirtualEmAndamento()) {
        co2_para_site = co2_real;
      }
    }
  }
}

#if PLOT_ENABLED
void plotSerial(uint32_t agora, int potRaw) {
  static uint32_t tPlot = 0;
  if (agora - tPlot < PLOT_PERIOD_MS) return;
  tPlot = agora;

  /*
  Serial.print("pot:");
  Serial.print(potRaw);
  Serial.print(" th:");
  Serial.print((int)thresholdAtual);
  Serial.print(" mic:");
  Serial.print((int)micLevel);
  Serial.print(" raw:");
  Serial.print((int)micDeltaRaw);
  Serial.print(" filt:");
  Serial.print((int)micDeltaFilt);
  Serial.print(" co2r:");
  Serial.print((int)co2_real);
  Serial.print(" co2s:");
  Serial.println((int)co2_para_site);
  */
}
#endif

// ================= SETUP =================
void setup() {
  SERIAL_PORT.begin(115200);
  delay(500);

  Serial.println("\n=== INICIANDO GAIA (Disparo único som + atraso CO2) ===");

  pinMode(LED_SOM_PIN, OUTPUT);
  digitalWrite(LED_SOM_PIN, LOW);

  pinMode(LED_CO2_PIN, OUTPUT);
  digitalWrite(LED_CO2_PIN, LOW);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  configurarADC();
  setupI2S();
  calibrarMicrofone();

  Serial.println("Inicializando ICM-20948...");

#ifdef USE_SPI
  SPI_PORT.begin();
#else
  WIRE_PORT.begin();
  WIRE_PORT.setClock(400000);
#endif

  bool initialized = false;
  while (!initialized) {
#ifdef USE_SPI
    myICM.begin(CS_PIN, SPI_PORT);
#else
    myICM.begin(WIRE_PORT, AD0_VAL);
#endif

    if (myICM.status == ICM_20948_Stat_Ok) {
      initialized = true;
      Serial.println("✅ ICM-20948 conectado!");
    } else {
      Serial.println("Tentando novamente...");
      delay(500);
    }
  }

  bool success = true;
  success &= (myICM.initializeDMP() == ICM_20948_Stat_Ok);
  success &= (myICM.enableDMPSensor(INV_ICM20948_SENSOR_ORIENTATION) == ICM_20948_Stat_Ok);
  success &= (myICM.enableDMPSensor(INV_ICM20948_SENSOR_ACCELEROMETER) == ICM_20948_Stat_Ok);
  success &= (myICM.setDMPODRrate(DMP_ODR_Reg_Quat9, 1) == ICM_20948_Stat_Ok);
  success &= (myICM.setDMPODRrate(DMP_ODR_Reg_Accel, 1) == ICM_20948_Stat_Ok);        // Set to 1Hz
  success &= (myICM.enableFIFO() == ICM_20948_Stat_Ok);
  success &= (myICM.enableDMP() == ICM_20948_Stat_Ok);
  success &= (myICM.resetDMP() == ICM_20948_Stat_Ok);
  success &= (myICM.resetFIFO() == ICM_20948_Stat_Ok);

  if (!success) {
    Serial.println("❌ Enable DMP falhou! Confira #define ICM_20948_USE_DMP no ICM_20948_C.h");
  } else {
    Serial.println("✅ DMP enabled!");
  }

  BLEDevice::init("Globo_1");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pSensorCharacteristic = pService->createCharacteristic(
    SENSOR_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ   |
    BLECharacteristic::PROPERTY_WRITE  |
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_INDICATE
  );

  pLedCharacteristic = pService->createCharacteristic(
    LED_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );

  pCO2Characteristic = pService->createCharacteristic(
    CO2_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ   |
    BLECharacteristic::PROPERTY_WRITE  |
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_INDICATE
  );

  pLedCharacteristic->setCallbacks(new MyCharacteristicCallbacks());

  pSensorCharacteristic->addDescriptor(new BLE2902());
  pLedCharacteristic->addDescriptor(new BLE2902());
  pCO2Characteristic->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);
  BLEDevice::startAdvertising();

  Serial.println("✅ BLE pronto. Aguardando conexão...");

  co2Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial.println("✅ CO2 UART pronto");
}

// ================= LOOP =================
void loop() {
  uint32_t agora = millis();

  static uint32_t tAudio = 0, tPot = 0;
  static int potRaw = 0;

  if (agora - tPot >= POT_PERIOD_MS) {
    tPot = agora;
    potRaw = adc1_get_raw(ADC1_CHANNEL_6);
    thresholdAtual = (float)lerPotenciometro();

    // proteção extra
    if (thresholdAtual < THRESHOLD_MIN) thresholdAtual = THRESHOLD_MIN;
    if (thresholdAtual > THRESHOLD_MAX) thresholdAtual = THRESHOLD_MAX;
  }

  if (agora - tAudio >= AUDIO_PERIOD_MS) {
    tAudio = agora;

    micLevel = lerNivelAudio();

    micDeltaRaw = micLevel - micBaseline;
    if (micDeltaRaw < 0) micDeltaRaw = 0;

    micDeltaFilt = (MIC_IIR_ALPHA * micDeltaFilt) + ((1.0f - MIC_IIR_ALPHA) * micDeltaRaw);

    detectarPicoAudio(agora);
  }

  lerCO2Real_periodico(agora);
  atualizarPulsoCO2(agora);

#if PLOT_ENABLED
  plotSerial(agora, potRaw);
#endif

  // IMU
  icm_20948_DMP_data_t data;
  myICM.readDMPdataFromFIFO(&data);

  bool haveQuat = false;
  StaticJsonDocument<256> doc;
  StaticJsonDocument<64>  co2Doc;
  StaticJsonDocument<256>  accDoc;
  
  if ((myICM.status == ICM_20948_Stat_Ok) || (myICM.status == ICM_20948_Stat_FIFOMoreDataAvail)) {
    if ((data.header & DMP_header_bitmap_Quat9) > 0) {
      haveQuat = true;

      double q1 = ((double)data.Quat9.Data.Q1) / 1073741824.0;
      double q2 = ((double)data.Quat9.Data.Q2) / 1073741824.0;
      double q3 = ((double)data.Quat9.Data.Q3) / 1073741824.0;
      double q0 = sqrt(1.0 - ((q1*q1) + (q2*q2) + (q3*q3)));

     

      JsonObject quat = doc["quat"].to<JsonObject>();
      quat["quat_w"] = q0;
      quat["quat_x"] = q1;
      quat["quat_y"] = q2;
      quat["quat_z"] = q3;

      double qw = q0;
      double qx = q2;
      double qy = q1;
      double qz = -q3;

      double t0 = +2.0 * (qw * qx + qy * qz);
      double t1 = +1.0 - 2.0 * (qx * qx + qy * qy);
      double roll = atan2(t0, t1) * 180.0 / PI;

      double t2 = +2.0 * (qw * qy - qx * qz);
      t2 = t2 > 1.0 ? 1.0 : t2;
      t2 = t2 < -1.0 ? -1.0 : t2;
      double pitch = asin(t2) * 180.0 / PI;

      double t3 = +2.0 * (qw * qz + qx * qy);
      double t4 = +1.0 - 2.0 * (qy * qy + qz * qz);
      double yaw = atan2(t3, t4) * 180.0 / PI;

      JsonObject euler = doc["euler"].to<JsonObject>();
      euler["roll"] = roll;
      euler["pitch"] = pitch;
      euler["yaw"] = yaw;

      
      JsonObject co2 = co2Doc["co2"].to<JsonObject>();
      co2["ppm"] = co2_para_site;
      
    }

    if ((data.header & DMP_header_bitmap_Accel) > 0) // Check for Accel
    {
      float acc_x = (float)data.Raw_Accel.Data.X; // Extract the raw accelerometer data
      float acc_y = (float)data.Raw_Accel.Data.Y;
      float acc_z = (float)data.Raw_Accel.Data.Z;

      SERIAL_PORT.print(F("Accel: X:"));
      SERIAL_PORT.print(acc_x);
      SERIAL_PORT.print(F(" Y:"));
      SERIAL_PORT.print(acc_y);
      SERIAL_PORT.print(F(" Z:"));
      SERIAL_PORT.println(acc_z);

      JsonObject acc = doc["acc"].to<JsonObject>();
      acc["x"] = acc_x;
      acc["y"] = acc_y;
      acc["z"] = acc_z;

    }
    if (deviceConnected) {
        String output;
        serializeJson(doc, output);
        value = output;
        pSensorCharacteristic->setValue(value.c_str());
        pSensorCharacteristic->notify();

        String co2ppmOutput;
        serializeJson(co2Doc, co2ppmOutput);
        co2ppmValue = co2ppmOutput;
        pCO2Characteristic->setValue(co2ppmValue.c_str());
        pCO2Characteristic->notify();

        delay(12);
      }
  }

  if (!deviceConnected && oldDeviceConnected) {
    Serial.println("BLE: desconectou. Reanunciando...");
    delay(200);
    pServer->startAdvertising();
    oldDeviceConnected = deviceConnected;
  }

  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
    Serial.println("BLE: conectado (ok)");
  }

  if (myICM.status != ICM_20948_Stat_FIFOMoreDataAvail) {
    if (!haveQuat) delay(2);
    delay(10);
  }
}
