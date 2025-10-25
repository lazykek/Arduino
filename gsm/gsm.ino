#include <SoftwareSerial.h>
SoftwareSerial sim(7, 8);
SoftwareSerial esp(2, 3);

const long SERIAL_BAUD = 115200;
const long ESP_BAUD = 9600;
const long SIM_BAUD = 9600;

const char* SSID = "KazzHole";
const char* PASSWORD = "Halyk##198";

const char* PHONE = "+79969793617";

const char* HOST = "api.open-meteo.com";
const int PORT = 80;
const char* PATH = "/v1/forecast?latitude=55.7558&longitude=37.6173&current_weather=true";

String lastResponse = "Last response";

void setup() {
  Serial.begin(SERIAL_BAUD);
  
  initModule(esp, ESP_BAUD);
  initModule(sim, SIM_BAUD);
  
  connectToWiFi();
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd == "HTTP") {
      sendHTTPRequest();
    } else if (cmd == "SMS") {
      sendSMS(PHONE, lastResponse);
    } else if (cmd == "LR") {
      Serial.println(lastResponse);
    } else {
      sendCommand(esp, "ESP", cmd, 2000);
    }
  }
}

// Подключение к Wi-Fi
void connectToWiFi() {
  sendCommand(esp, "ESP", "AT+CWMODE=1", 2000);
  
  String connectCmd = "AT+CWJAP=\"";
  connectCmd += SSID;
  connectCmd += "\",\"";
  connectCmd += PASSWORD;
  connectCmd += "\"";
  
  sendCommand(esp, "ESP", connectCmd, 15000);
}

// Отправка HTTP-запроса
void sendHTTPRequest() {
  // Закрываем предыдущие соединения
  sendCommand(esp, "ESP", "AT+CIPCLOSE", 1000);
  
  // Установка TCP-соединения
  String tcpCmd = "AT+CIPSTART=\"TCP\",\"";
  tcpCmd += HOST;
  tcpCmd += "\",";
  tcpCmd += PORT;
  if (!sendCommand(esp, "ESP", tcpCmd, 5000)) {
    Serial.println("Connection failed");
    return;
  }
  
  // Подготовка HTTP-запроса
  String httpRequest = "GET ";
  httpRequest += PATH;
  httpRequest += " HTTP/1.1\r\nHost: ";
  httpRequest += HOST;
  httpRequest += "\r\nConnection: close\r\n\r\n";
  
  // Отправка команды на передачу данных
  String sendCmd = "AT+CIPSEND=";
  sendCmd += httpRequest.length();
  if (!sendCommand(esp, "ESP", sendCmd, 2000)) {
    return;
  }
  
  // Отправка самого HTTP-запроса
  delay(100);
  esp.listen();
  esp.print(httpRequest);
  
  // Чтение HTTP-ответа
  readHTTPResponse();
}

// Специальная функция для чтения HTTP-ответа
void readHTTPResponse() {
  Serial.println("Reading HTTP response...");
  unsigned long startTime = millis();
  const unsigned long timeout = 10000; // 10 секунд на весь ответ
  bool dataReceived = false;
  lastResponse = "";
  
  while (millis() - startTime < timeout) {
    while (esp.available()) {
      dataReceived = true;
      char c = esp.read();
      
      // Фильтруем управляющие символы (оставляем только печатаемые)
      if (c >= 32 || c == '\n' || c == '\r' || c == '\t') {
        // Serial.write(c);
        lastResponse += String(c);
      }
      
      // Если обнаружен конец ответа
      if (c == '\n') {
        startTime = millis(); // Сбрасываем таймер при получении данных
      }
    }
    
    // Если данные не приходят более 500мс - выходим
    if (dataReceived && !esp.available() && (millis() - startTime > 500)) {
      break;
    }
  }
  Serial.println(lastResponse);
  Serial.println("\nEnd of response");
}

// Инициализация модуля
void initModule(SoftwareSerial &module, long speed) {
  module.begin(speed);
  delay(1000);
  module.listen();
  while(module.available()) module.read();
  module.stopListening();
}

// Отправка команды с возвратом статуса
bool sendCommand(SoftwareSerial &module, const char* name, String cmd, unsigned long timeout) {
  if (!module.listen()) {
    Serial.println("Ошибка: модуль не смог начать прослушивание!");
    return false;
  };
  while(module.available()) module.read();
  module.println(cmd);
  String response = readResponse(module, name, timeout);
  module.stopListening();
  
  return response.indexOf("OK") != -1 || 
         response.indexOf("CONNECT") != -1 ||
         response.indexOf("SEND OK") != -1;
}

// Чтение ответа с таймаутом
String readResponse(SoftwareSerial &module, const char* name, unsigned long timeout) {
  unsigned long start = millis();
  String response = "";
  
  while (millis() - start < timeout) {
    while (module.available()) {
      char c = module.read();
      response += String(c);
      
      // Проверка конца ответа
      if (response.endsWith("OK\r\n") || 
          response.endsWith("ERROR\r\n") ||
          response.endsWith("SEND OK\r\n") ||
          response.endsWith("> ") ||
          response.endsWith("CLOSED\r\n") ||
          response.endsWith("CONNECT\r\n") ||
          response.endsWith("WIFI GOT IP\r\n")) {
        break;
      }
    }
    
    // Прерываем если прошло 100мс без данных
    if (response.length() > 0 && !module.available() && millis() - start > timeout/2) {
      break;
    }
  }
  
  if (response != "") {
    Serial.print(name);
    Serial.print(" response: ");
    
    // Фильтрация управляющих символов
    for (unsigned int i = 0; i < response.length(); i++) {
      char c = response.charAt(i);
      if (c >= 32 || c == '\n' || c == '\r' || c == '\t') {
        Serial.write(c);
      }
    }
    Serial.println();
  } else {
    Serial.print(name);
    Serial.println(": No response");
  }
  
  return response;
}

// Функция отправки SMS
void sendSMS(String number, String message) {
  Serial.println("=== Sending SMS ===");
  
  sim.listen();
  delay(100);
  
  // Установка текстового режима
  sim.println("AT+CMGF=1");
  if (!readResponse(sim, "SMS", 2000).indexOf("OK")) {
    Serial.println("Failed to set text mode");
    return;
  }

  // Проверка регистрации в сети
  sim.println("AT+CREG?");
  String regResponse = readResponse(sim, "SMS", 2000);
  if (regResponse.indexOf("+CREG: 0,1") == -1 && regResponse.indexOf("+CREG: 0,5") == -1) {
    Serial.println("Not registered in network");
    return;
  }

  // Установка номера
  sim.print("AT+CMGS=\"");
  sim.print(number);
  sim.println("\"");
  
  // Ожидаем приглашение "> "
  String response = readResponse(sim, "SMS", 3000);
  if (response.indexOf(">") == -1) {
    Serial.println("Did not get > prompt");
    return;
  }

  // Отправка сообщения и Ctrl+Z
  sim.print(message);
  delay(500);  // Важно: задержка перед отправкой Ctrl+Z
  sim.write(26); // Ctrl+Z
  sim.println();

  // Ожидаем финальный ответ
  response = readResponse(sim, "SMS", 10000);
  if (response.indexOf("SEND OK") != -1) {
    Serial.println("SMS sent successfully!");
  } else if (response.indexOf("ERROR") != -1) {
    Serial.println("SMS sending failed");
  } else {
    Serial.println("Unknown response");
  }
  
  sim.stopListening();
}

