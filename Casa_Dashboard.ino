#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <string.h>
#include <time.h>
#include <secrets.h>

// ============================================================
// CASA DASHBOARD - ESP32-WROOM-32 + HC-35S + LCD 1602 I2C
// Home Assistant / Sonoff
//
// Refatoracao: responsabilidades separadas em secoes e funcoes,
// cadastro de dispositivos orientado a dados e comunicacao HTTP
// centralizada. O projeto continua em um unico .ino para facilitar
// a instalacao na Arduino IDE.
//
// Botoes ativos em LOW. Confirme que a placa HC-35S fornece o nivel
// eletrico esperado; INPUT foi mantido para preservar a montagem.
// ============================================================

// ------------------------- Configuracao ----------------------
const char* WEATHER_ENTITY = "weather.forecast_casa";

const unsigned long IDLE_TO_INFO_MS = 5000UL;
const unsigned long HA_UPDATE_INTERVAL = 5000UL;
const unsigned long WEATHER_UPDATE_INTERVAL = 10UL * 60UL * 1000UL;
const unsigned long WEATHER_RETRY_INTERVAL = 30000UL;
const unsigned long WIFI_RECONNECT_INTERVAL = 10000UL;
const unsigned long WIFI_CONNECT_TIMEOUT = 20000UL;
const uint16_t HTTP_TIMEOUT = 2500;
const unsigned long BUTTON_DEBOUNCE = 60UL;
const unsigned long TICKER_INTERVAL = 250UL;

const byte LCD_ADDRESS = 0x27;
const byte LCD_COLUMNS = 16;
const byte LCD_ROWS = 2;
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);

// ------------------------- Botoes HC-35S ---------------------
// Somente tres botoes estao em uso: S1 desce, S2 sobe e S3 aciona/abre.
const byte S1_PIN = 26;
const byte S2_PIN = 27;
const byte S3_PIN = 32;
const byte BUTTON_COUNT = 3;
const byte buttonPins[BUTTON_COUNT] = {S1_PIN, S2_PIN, S3_PIN};
bool lastButtonState[BUTTON_COUNT] = {HIGH, HIGH, HIGH};
unsigned long lastButtonTime[BUTTON_COUNT] = {0, 0, 0};

// --------------------- Cadastro de dispositivos --------------
enum class Screen : byte { HOME, DEVICES, INFO };
enum class DeviceDomain : byte { SWITCH, LIGHT, NONE };

struct DeviceConfig {
  const char* name;
  const char* entityId;
  DeviceDomain domain;
  bool configured;
};

// Para adicionar um dispositivo, cadastre-o aqui. Reservas nao
// enviam comandos e nao simulam um estado ON/OFF local.
const DeviceConfig devices[] = {
  {"Sala",    "switch.interruptor_sonoff_1001578d9b_2", DeviceDomain::SWITCH, true},
  {"Cozinha", "switch.interruptor_sonoff_1001578d9b_1", DeviceDomain::SWITCH, true},
  {"Quarto",  "light.pendente_sonoff_10016034a0",       DeviceDomain::LIGHT,  true},
  {"01 Reserva", nullptr, DeviceDomain::NONE, false},
  {"02 Reserva", nullptr, DeviceDomain::NONE, false},
  {"03 Reserva", nullptr, DeviceDomain::NONE, false},
  {"04 Reserva", nullptr, DeviceDomain::NONE, false},
  {"05 Reserva", nullptr, DeviceDomain::NONE, false}
};
const byte DEVICE_COUNT = sizeof(devices) / sizeof(devices[0]);
bool deviceState[DEVICE_COUNT] = {};
bool deviceStateAvailable[DEVICE_COUNT] = {};

// ------------------------- Estado da aplicacao ---------------
Screen currentScreen = Screen::HOME;
int selectedDevice = 0;
unsigned long lastHAUpdate = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastUserActivity = 0;
unsigned long lastTickerMove = 0;
int tickerOffset = 0;
String tickerText;

String weatherCondition = "indisponivel";
String weatherTemperature = "--";
bool weatherAvailable = false;

// ============================================================
// Interface: LCD
// ============================================================
void printLCD16(byte line, const String& text) {
  String value = text;
  if (value.length() > LCD_COLUMNS) value = value.substring(0, LCD_COLUMNS);
  lcd.setCursor(0, line);
  lcd.print(value);
  for (int i = value.length(); i < LCD_COLUMNS; ++i) lcd.print(' ');
}

void showMessage(const char* line1, const char* line2) {
  lcd.clear();
  printLCD16(0, line1);
  printLCD16(1, line2);
}

bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

void showHomeScreen() {
  lcd.clear();
  printLCD16(0, "MINHA CASA");
  printLCD16(1, isWiFiConnected() ? "S1/S2 Menu S3 OK" : "WiFi: OFF");
}

void showDeviceScreen() {
  lcd.clear();
  printLCD16(0, String(">") + devices[selectedDevice].name);

  if (!devices[selectedDevice].configured) {
    printLCD16(1, "Nao configurado");
  } else if (!deviceStateAvailable[selectedDevice]) {
    printLCD16(1, isWiFiConnected() ? "Estado indispon." : "WiFi: OFF");
  } else {
    printLCD16(1, deviceState[selectedDevice] ? "Estado: ON" : "Estado: OFF");
  }
}

// ============================================================
// Servico: Wi-Fi
// ============================================================
void connectWiFi() {
  showMessage("Conectando WiFi", WIFI_SSID);
  Serial.println("\n[WIFI] Iniciando conexao...");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long startTime = millis();
  while (!isWiFiConnected() && millis() - startTime < WIFI_CONNECT_TIMEOUT) {
    delay(100); // Apenas na inicializacao.
  }

  if (isWiFiConnected()) {
    Serial.println("[WIFI] Conectado");
    Serial.print("[WIFI] IP: "); Serial.println(WiFi.localIP());
    Serial.print("[WIFI] Gateway: "); Serial.println(WiFi.gatewayIP());
  } else {
    Serial.println("[WIFI] Falha na conexao inicial; novas tentativas serao feitas.");
  }
}

void maintainWiFi() {
  static unsigned long lastAttempt = 0;
  if (isWiFiConnected() || millis() - lastAttempt < WIFI_RECONNECT_INTERVAL) return;
  lastAttempt = millis();
  Serial.println("[WIFI] Tentando reconectar...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// ============================================================
// Servico: relogio NTP
// ============================================================
void initializeClock() {
  configTzTime("BRT3", "pool.ntp.org", "time.nist.gov", "time.google.com");
  Serial.println("[NTP] Sincronizando data/hora...");
  struct tm timeInfo;
  if (getLocalTime(&timeInfo, 10000)) Serial.println("[NTP] Data/hora sincronizadas");
  else Serial.println("[NTP] Sincronizacao pendente");
}

String getCurrentTime() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 100)) return "--:--";
  char buffer[6];
  strftime(buffer, sizeof(buffer), "%H:%M", &timeInfo);
  return String(buffer);
}

String getCurrentDate() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 100)) return "--/--/--";
  char buffer[9];
  strftime(buffer, sizeof(buffer), "%d/%m/%y", &timeInfo);
  return String(buffer);
}

// ============================================================
// Integracao: cliente HTTP centralizado para Home Assistant
// ============================================================
bool performHARequest(const char* method, const String& endpoint,
                     const String& requestBody, String* responseBody,
                     int* responseCode = nullptr) {
  if (!isWiFiConnected()) {
    Serial.println("[HA] Wi-Fi desconectado");
    return false;
  }

  String url = String(HA_URL) + endpoint;
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("[HA] Falha ao iniciar HTTP");
    return false;
  }

  http.setTimeout(HTTP_TIMEOUT);
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
  http.addHeader("Content-Type", "application/json");

  int code = -1;
  if (strcmp(method, "GET") == 0) code = http.GET();
  else if (strcmp(method, "POST") == 0) code = http.POST(requestBody);
  else {
    Serial.println("[HA] Metodo HTTP nao suportado");
    http.end();
    return false;
  }

  if (responseCode != nullptr) *responseCode = code;
  Serial.printf("[HA] %s %s -> %d\n", method, endpoint.c_str(), code);
  const bool success = code >= 200 && code < 300;
  if (success && responseBody != nullptr) *responseBody = http.getString();
  if (!success) {
    Serial.print("[HA] Falha HTTP: ");
    Serial.println(http.errorToString(code));
  }
  http.end();
  return success;
}

bool getEntityJson(const char* entityId, JsonDocument& document) {
  String payload;
  const String endpoint = String("/api/states/") + entityId;
  if (!performHARequest("GET", endpoint, "", &payload)) return false;

  DeserializationError error = deserializeJson(document, payload);
  if (error) {
    Serial.print("[HA] JSON invalido: ");
    Serial.println(error.c_str());
    return false;
  }
  return true;
}

bool getEntityState(const char* entityId, bool& state) {
  StaticJsonDocument<2048> document;
  if (!getEntityJson(entityId, document)) return false;

  const char* stateText = document["state"] | "";
  if (strcmp(stateText, "on") == 0) {
    state = true;
    return true;
  }
  if (strcmp(stateText, "off") == 0) {
    state = false;
    return true;
  }

  Serial.printf("[HA] Estado de %s nao e on/off: %s\n", entityId, stateText);
  return false;
}

bool callDeviceService(const DeviceConfig& device) {
  if (!device.configured || device.entityId == nullptr) return false;

  const char* domain = nullptr;
  if (device.domain == DeviceDomain::SWITCH) domain = "switch";
  else if (device.domain == DeviceDomain::LIGHT) domain = "light";
  else return false;

  const String endpoint = String("/api/services/") + domain + "/toggle";
  // entity_id vem de configuracao local fixa, nao de entrada do usuario.
  const String body = String("{\"entity_id\":\"") + device.entityId + "\"}";
  return performHARequest("POST", endpoint, body, nullptr);
}

// ============================================================
// Servico: clima via Home Assistant
// ============================================================
String translateWeatherCondition(const String& condition) {
  if (condition == "clear-night") return "Ceu limpo";
  if (condition == "cloudy") return "Nublado";
  if (condition == "exceptional") return "Excepcional";
  if (condition == "fog") return "Nevoeiro";
  if (condition == "hail") return "Granizo";
  if (condition == "lightning") return "Trovoadas";
  if (condition == "lightning-rainy") return "Trovoada/chuva";
  if (condition == "partlycloudy") return "Parc nublado";
  if (condition == "pouring") return "Chuva forte";
  if (condition == "rainy") return "Chuva";
  if (condition == "snowy") return "Neve";
  if (condition == "snowy-rainy") return "Neve/chuva";
  if (condition == "sunny") return "Ensolarado";
  if (condition == "windy") return "Ventando";
  if (condition == "windy-variant") return "Vento";
  return condition;
}

void updateWeather() {
  StaticJsonDocument<3072> document;
  if (!getEntityJson(WEATHER_ENTITY, document)) {
    weatherAvailable = false;
    return;
  }

  const char* condition = document["state"] | "";
  if (condition[0] == '\0' || strcmp(condition, "unavailable") == 0 ||
      strcmp(condition, "unknown") == 0) {
    weatherAvailable = false;
    return;
  }

  JsonVariant temperature = document["attributes"]["temperature"];
  if (temperature.is<float>() || temperature.is<int>() || temperature.is<double>()) {
    weatherTemperature = String(temperature.as<float>(), 1);
  } else {
    weatherTemperature = "--";
  }

  weatherCondition = translateWeatherCondition(String(condition));
  weatherAvailable = true;
  Serial.printf("[CLIMA] %s, %s C\n", weatherCondition.c_str(), weatherTemperature.c_str());
}

void maintainWeather() {
  const unsigned long interval = weatherAvailable ? WEATHER_UPDATE_INTERVAL : WEATHER_RETRY_INTERVAL;
  if (millis() - lastWeatherUpdate < interval) return;
  lastWeatherUpdate = millis();
  updateWeather();
}

// ============================================================
// Controle de dispositivos (sem estado otimista)
// ============================================================
void syncSelectedDevice() {
  const DeviceConfig& device = devices[selectedDevice];
  if (!device.configured || device.entityId == nullptr) {
    deviceStateAvailable[selectedDevice] = false;
    return;
  }

  bool actualState = false;
  if (getEntityState(device.entityId, actualState)) {
    deviceState[selectedDevice] = actualState;
    deviceStateAvailable[selectedDevice] = true;
  } else {
    deviceStateAvailable[selectedDevice] = false;
  }
  if (currentScreen == Screen::DEVICES) showDeviceScreen();
}

void toggleSelectedDevice() {
  const DeviceConfig& device = devices[selectedDevice];
  if (!device.configured) {
    showMessage("DISPOSITIVO", "Nao configurado");
    return;
  }

  if (!callDeviceService(device)) {
    deviceStateAvailable[selectedDevice] = false;
    showMessage("Falha no comando", "Verifique conexao");
    return;
  }

  // Nao inverte o estado local: consulta o estado confirmado pelo HA.
  deviceStateAvailable[selectedDevice] = false;
  syncSelectedDevice();
  if (!deviceStateAvailable[selectedDevice]) showMessage("Comando enviado", "Estado pendente");
}

// ============================================================
// Navegacao e modo informativo
// ============================================================
void showHome() {
  currentScreen = Screen::HOME;
  tickerText = "";
  tickerOffset = 0;
  showHomeScreen();
}

void enterDevices() {
  currentScreen = Screen::DEVICES;
  showDeviceScreen();
}

void selectNextDevice() {
  selectedDevice = (selectedDevice + 1) % DEVICE_COUNT;
  showDeviceScreen();
}

void selectPreviousDevice() {
  selectedDevice = (selectedDevice + DEVICE_COUNT - 1) % DEVICE_COUNT;
  showDeviceScreen();
}

void buildTickerMessage() {
  const String climate = weatherAvailable
    ? weatherCondition + " " + weatherTemperature + "C"
    : "Clima indisponivel";
  tickerText = "Hora " + getCurrentTime() + " | " + getCurrentDate() + " | " + climate + "    ";
  tickerOffset = 0;
}

void drawTickerWindow() {
  if (tickerText.length() == 0) buildTickerMessage();
  String windowText;
  for (int i = 0; i < LCD_COLUMNS; ++i) {
    int index = tickerOffset + i;
    if (index >= static_cast<int>(tickerText.length())) index %= tickerText.length();
    windowText += tickerText[index];
  }
  printLCD16(0, "S1/S2 Menu S3 OK");
  printLCD16(1, windowText);
}

void enterInfoScreen() {
  currentScreen = Screen::INFO;
  buildTickerMessage();
  lastTickerMove = millis();
  drawTickerWindow();
}

void maintainTicker() {
  if (currentScreen != Screen::INFO || millis() - lastTickerMove < TICKER_INTERVAL) return;
  lastTickerMove = millis();
  if (tickerText.length() == 0) buildTickerMessage();
  tickerOffset++;
  if (tickerOffset >= static_cast<int>(tickerText.length())) buildTickerMessage();
  drawTickerWindow();
}

void registerUserActivity() {
  lastUserActivity = millis();
  if (currentScreen == Screen::INFO) showHome();
}

void handleButton(byte buttonIndex) {
  // No modo informativo, qualquer um dos tres botoes sai para a tela inicial.
  // O pressionamento nao e reutilizado como comando para evitar acionamentos
  // acidentais ao interromper o ticker.
  if (currentScreen == Screen::INFO) {
    lastUserActivity = millis();
    showHome();
    return;
  }

  registerUserActivity();
  switch (buttonIndex) {
    case 0: // S1: descer / abrir o menu no primeiro item
      if (currentScreen == Screen::HOME) {
        selectedDevice = 0;
        enterDevices();
      } else if (currentScreen == Screen::DEVICES) {
        selectNextDevice();
      }
      break;

    case 1: // S2: subir / abrir o menu no ultimo item
      if (currentScreen == Screen::HOME) {
        selectedDevice = DEVICE_COUNT - 1;
        enterDevices();
      } else if (currentScreen == Screen::DEVICES) {
        selectPreviousDevice();
      }
      break;

    case 2: // S3: abrir o menu ou acionar o item selecionado
      if (currentScreen == Screen::HOME) {
        enterDevices();
      } else if (currentScreen == Screen::DEVICES) {
        toggleSelectedDevice();
      }
      break;
  }
}

void readPushButtons() {
  for (byte i = 0; i < BUTTON_COUNT; ++i) {
    const bool currentState = digitalRead(buttonPins[i]);
    if (currentState == LOW && lastButtonState[i] == HIGH &&
        millis() - lastButtonTime[i] >= BUTTON_DEBOUNCE) {
      lastButtonTime[i] = millis();
      handleButton(i);
    }
    lastButtonState[i] = currentState;
  }
}

void maintainHomeAssistantState() {
  if (millis() - lastHAUpdate < HA_UPDATE_INTERVAL) return;
  lastHAUpdate = millis();
  if (currentScreen == Screen::DEVICES) syncSelectedDevice();
}

void maintainIdleScreen() {
  if (currentScreen != Screen::INFO && millis() - lastUserActivity >= IDLE_TO_INFO_MS) {
    enterInfoScreen();
  }
}

// ============================================================
// Inicializacao e loop
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n================================");
  Serial.println("       CASA DASHBOARD");
  Serial.println("================================");

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  showMessage("PAINEL DA CASA", "Iniciando...");

  for (byte i = 0; i < BUTTON_COUNT; ++i) {
    pinMode(buttonPins[i], INPUT);
    lastButtonState[i] = digitalRead(buttonPins[i]);
    lastButtonTime[i] = millis();
  }

  connectWiFi();
  if (isWiFiConnected()) {
    initializeClock();
    updateWeather();
  }
  lastWeatherUpdate = millis();

  currentScreen = Screen::DEVICES;
  selectedDevice = 0;
  showDeviceScreen();
  syncSelectedDevice();
  lastHAUpdate = millis();
  lastUserActivity = millis();

  Serial.println("[SISTEMA] Painel pronto");
  Serial.println("[S1] Baixa | [S2] Sobe | [S3] Aciona/Menu");
  Serial.println("[INFO] Apos 5 segundos sem interacao: hora/data/clima");
  Serial.println("[INFO] Qualquer botao sai para a tela inicial; pressione novamente para agir.");
}

void loop() {
  maintainWiFi();
  readPushButtons();
  maintainHomeAssistantState();
  maintainWeather();
  maintainIdleScreen();
  maintainTicker();
  delay(5); // Pequena pausa para aliviar o loop; nao substitui operacoes assincronas.
}
