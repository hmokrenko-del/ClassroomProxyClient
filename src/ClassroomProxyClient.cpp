#include "ClassroomProxyClient.h"

#include <stdlib.h>

#include <HTTPClient.h>
#include <WiFi.h>

namespace {

String extractGeminiText(JsonVariantConst root) {
  String text;
  JsonArrayConst candidates = root["candidates"].as<JsonArrayConst>();
  if (candidates.isNull() || candidates.size() == 0) {
    return text;
  }

  JsonArrayConst parts = candidates[0]["content"]["parts"].as<JsonArrayConst>();
  if (parts.isNull()) {
    return text;
  }

  for (JsonVariantConst part : parts) {
    if (part["text"].is<const char*>()) {
      if (!text.isEmpty()) {
        text += '\n';
      }
      text += part["text"].as<const char*>();
    }
  }

  return text;
}

String extractArrayLines(JsonVariantConst root, const char* key) {
  String text;
  JsonArrayConst arr = root[key].as<JsonArrayConst>();
  if (arr.isNull()) {
    return text;
  }

  for (JsonVariantConst item : arr) {
    if (item.is<const char*>()) {
      if (!text.isEmpty()) {
        text += '\n';
      }
      text += item.as<const char*>();
    }
  }
  return text;
}

String extractTableText(JsonVariantConst root, const char* key) {
  String text;
  JsonArrayConst rows = root[key].as<JsonArrayConst>();
  if (rows.isNull()) {
    return text;
  }

  for (JsonVariantConst row : rows) {
    JsonArrayConst cols = row.as<JsonArrayConst>();
    if (cols.isNull()) {
      continue;
    }

    if (!text.isEmpty()) {
      text += '\n';
    }

    bool first = true;
    for (JsonVariantConst col : cols) {
      if (!first) {
        text += '\t';
      }
      first = false;

      if (col.is<const char*>()) {
        text += col.as<const char*>();
      } else if (col.is<long>()) {
        text += String(col.as<long>());
      } else if (col.is<double>()) {
        text += String(col.as<double>(), 6);
      } else if (col.is<bool>()) {
        text += col.as<bool>() ? "true" : "false";
      }
    }
  }
  return text;
}

bool parseIntStrict(const String& s, int& out) {
  if (s.isEmpty()) {
    return false;
  }

  const char* c = s.c_str();
  char* end = nullptr;
  const long value = strtol(c, &end, 10);
  if (end == c || *end != '\0') {
    return false;
  }
  out = static_cast<int>(value);
  return true;
}

}  // namespace

ClassroomProxyClient::Config::Config()
    : wifiSsid(""),
      wifiPassword(""),
      proxyHost("host.wokwi.internal"),
      proxyPort(8080),
      geminiPath("/gemini"),
      docsAppendPath("/doc/append"),
      driveReadTextPath("/drive/readText"),
      driveReadLinesPath("/drive/readLines"),
      sheetsAppendRowPath("/sheets/appendRow"),
      sheetsReadRangePath("/sheets/readRange"),
      defaultGeminiModel("gemini-2.5-flash"),
      clientName("esp32"),
      serialBaud(115200),
      wifiConnectTimeoutMs(20000),
      wifiRetryIntervalMs(10000),
      httpTimeoutMs(45000),
      maxPromptLength(1000),
      enableSerialConsole(true),
      echoSerialInput(true) {}

ClassroomProxyClient::ClassroomProxyClient()
    : config_(),
      sessionId_(),
      serialInputBuffer_(),
      wifiWasConnected_(false),
      lastSerialWasCR_(false),
      lastWifiRetryMs_(0) {}

ClassroomProxyClient::ClassroomProxyClient(const Config& config)
    : ClassroomProxyClient() {
  configure(config);
}

void ClassroomProxyClient::configure(const Config& config) {
  config_ = config;
}

void ClassroomProxyClient::begin() {
  Serial.begin(config_.serialBaud);
  delay(300);

  sessionId_ = "boot-" + String(static_cast<uint32_t>(micros()), HEX);

  if (!config_.enableSerialConsole) {
    connectWiFi();
    return;
  }

  Serial.println();
  Serial.println(F("ClassroomProxyClient starting..."));
  Serial.printf(
      "[Proxy] http://%s:%u%s\n",
      config_.proxyHost,
      static_cast<unsigned>(config_.proxyPort),
      config_.geminiPath);
  Serial.printf("[Device] %s | [Session] %s\n", config_.clientName, sessionId_.c_str());

  printHelp();
  connectWiFi();
  printPrompt();
}

void ClassroomProxyClient::poll() {
  if (config_.enableSerialConsole) {
    handleSerialInput();
  }
  monitorWiFi();
  delay(10);
}

void ClassroomProxyClient::printHelp() {
  Serial.println();
  Serial.println(F("=== Classroom Proxy Client ==="));
  Serial.println(F("Type a prompt and press Enter."));
  Serial.println(F("Commands:"));
  Serial.println(F("  /help   - show this help"));
  Serial.println(F("  /wifi   - reconnect WiFi"));
  Serial.println(F("  /demo   - send a test prompt"));
  Serial.println(F("  /docappend <text>"));
  Serial.println(F("  /drivetext <fileId>"));
  Serial.println(F("  /drivelines <fileId> [from] [to]"));
  Serial.println();
}

void ClassroomProxyClient::printPrompt() {
  if (config_.enableSerialConsole) {
    Serial.print(F("> "));
  }
}

bool ClassroomProxyClient::isWiFiConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

const String& ClassroomProxyClient::sessionId() const {
  return sessionId_;
}

const ClassroomProxyClient::Config& ClassroomProxyClient::config() const {
  return config_;
}

bool ClassroomProxyClient::hasPlaceholderWiFiSecrets() const {
  const String ssid = config_.wifiSsid ? String(config_.wifiSsid) : String();
  const String pass = config_.wifiPassword ? String(config_.wifiPassword) : String();
  if (ssid.isEmpty()) {
    return true;
  }
  return ssid.startsWith("YOUR_") || pass.startsWith("YOUR_");
}

bool ClassroomProxyClient::hasValidWiFiSecrets() const {
  return !hasPlaceholderWiFiSecrets();
}

bool ClassroomProxyClient::connectWiFi(bool forceReconnect) {
  if (!hasValidWiFiSecrets()) {
    if (config_.enableSerialConsole) {
      Serial.println(F("[CONFIG] Fill WiFi values in your classroom config file."));
    }
    return false;
  }

  if (forceReconnect && WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(true, true);
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  if (config_.enableSerialConsole) {
    Serial.printf("[WiFi] Connecting to %s\n", config_.wifiSsid);
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(config_.wifiSsid, config_.wifiPassword);

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < config_.wifiConnectTimeoutMs) {
    delay(300);
    if (config_.enableSerialConsole) {
      Serial.print('.');
    }
  }
  if (config_.enableSerialConsole) {
    Serial.println();
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiWasConnected_ = true;
    if (config_.enableSerialConsole) {
      Serial.printf("[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
    }
    return true;
  }

  if (config_.enableSerialConsole) {
    Serial.println(F("[WiFi] Connection failed."));
  }
  return false;
}

bool ClassroomProxyClient::ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  return connectWiFi(false);
}

String ClassroomProxyClient::makeProxyUrl(const String& path) const {
  String normalizedPath = path;
  if (normalizedPath.isEmpty()) {
    normalizedPath = "/";
  } else if (!normalizedPath.startsWith("/")) {
    normalizedPath = "/" + normalizedPath;
  }

  return "http://" + String(config_.proxyHost) + ":" + String(config_.proxyPort) + normalizedPath;
}

bool ClassroomProxyClient::proxyPostJsonString(
    const String& path,
    const String& jsonBody,
    String& responseBody,
    int& httpCode,
    String& errorText) {
  responseBody = "";
  errorText = "";
  httpCode = 0;

  if (!ensureWiFi()) {
    errorText = "WiFi is not connected.";
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(config_.httpTimeoutMs);

  const String url = makeProxyUrl(path);
  if (!http.begin(client, url)) {
    errorText = "Failed to initialize HTTP proxy connection.";
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  httpCode = http.POST(jsonBody);
  responseBody = http.getString();
  http.end();

  if (httpCode <= 0) {
    errorText = "HTTP proxy error: " + http.errorToString(httpCode);
    return false;
  }

  return true;
}

bool ClassroomProxyClient::proxyJsonRequest(
    const String& path,
    DynamicJsonDocument& requestDoc,
    DynamicJsonDocument& responseDoc,
    String& errorText) {
  errorText = "";
  responseDoc.clear();

  String requestBody;
  serializeJson(requestDoc, requestBody);

  int httpCode = 0;
  String responseBody;
  if (!proxyPostJsonString(path, requestBody, responseBody, httpCode, errorText)) {
    return false;
  }

  DeserializationError jsonErr = deserializeJson(responseDoc, responseBody);
  if (jsonErr) {
    const size_t previewLen = responseBody.length() > 300 ? 300 : responseBody.length();
    errorText = "Failed to parse proxy response JSON: " + String(jsonErr.c_str());
    if (previewLen > 0) {
      errorText += " | Raw: " + responseBody.substring(0, previewLen);
    }
    return false;
  }

  if (responseDoc["error"].is<const char*>()) {
    errorText = responseDoc["error"].as<const char*>();
    return false;
  }

  if (httpCode < 200 || httpCode >= 300) {
    errorText = "Proxy HTTP " + String(httpCode) + " with error.";
    return false;
  }

  return true;
}

bool ClassroomProxyClient::geminiPrompt(const String& prompt, String& answer, String& errorText, const String& model) {
  answer = "";
  errorText = "";

  if (prompt.isEmpty()) {
    errorText = "Prompt is empty.";
    return false;
  }

  DynamicJsonDocument requestDoc(2048);
  requestDoc["prompt"] = prompt;
  requestDoc["model"] = model.isEmpty() ? String(config_.defaultGeminiModel) : model;
  requestDoc["device"] = config_.clientName;
  requestDoc["session"] = sessionId_;

  DynamicJsonDocument responseDoc(16384);
  if (!proxyJsonRequest(config_.geminiPath, requestDoc, responseDoc, errorText)) {
    return false;
  }

  if (responseDoc["answer"].is<const char*>()) {
    answer = responseDoc["answer"].as<const char*>();
  } else {
    answer = extractGeminiText(responseDoc.as<JsonVariantConst>());
  }

  if (answer.isEmpty()) {
    errorText = "Proxy returned no Gemini answer.";
    return false;
  }

  return true;
}

bool ClassroomProxyClient::docAppendText(
    const String& text,
    String& errorText,
    const String& docId,
    const String& prefix) {
  if (text.isEmpty()) {
    errorText = "Text is empty.";
    return false;
  }

  DynamicJsonDocument requestDoc(2048);
  requestDoc["text"] = text;
  requestDoc["device"] = config_.clientName;
  requestDoc["session"] = sessionId_;
  if (!docId.isEmpty()) {
    requestDoc["docId"] = docId;
  }
  if (!prefix.isEmpty()) {
    requestDoc["prefix"] = prefix;
  }

  DynamicJsonDocument responseDoc(2048);
  return proxyJsonRequest(config_.docsAppendPath, requestDoc, responseDoc, errorText);
}

bool ClassroomProxyClient::driveReadText(const String& fileId, String& text, String& errorText) {
  text = "";
  errorText = "";
  if (fileId.isEmpty()) {
    errorText = "fileId is empty.";
    return false;
  }

  DynamicJsonDocument requestDoc(1024);
  requestDoc["fileId"] = fileId;
  requestDoc["device"] = config_.clientName;
  requestDoc["session"] = sessionId_;

  DynamicJsonDocument responseDoc(8192);
  if (!proxyJsonRequest(config_.driveReadTextPath, requestDoc, responseDoc, errorText)) {
    return false;
  }

  if (responseDoc["text"].is<const char*>()) {
    text = responseDoc["text"].as<const char*>();
  }
  if (text.isEmpty()) {
    errorText = "Proxy returned no 'text'.";
    return false;
  }
  return true;
}

bool ClassroomProxyClient::driveReadLines(const String& fileId, int fromLine, int toLine, String& text, String& errorText) {
  text = "";
  errorText = "";
  if (fileId.isEmpty()) {
    errorText = "fileId is empty.";
    return false;
  }

  DynamicJsonDocument requestDoc(1024);
  requestDoc["fileId"] = fileId;
  requestDoc["device"] = config_.clientName;
  requestDoc["session"] = sessionId_;
  if (fromLine >= 0) {
    requestDoc["fromLine"] = fromLine;
  }
  if (toLine >= 0) {
    requestDoc["toLine"] = toLine;
  }

  DynamicJsonDocument responseDoc(16384);
  if (!proxyJsonRequest(config_.driveReadLinesPath, requestDoc, responseDoc, errorText)) {
    return false;
  }

  if (responseDoc["text"].is<const char*>()) {
    text = responseDoc["text"].as<const char*>();
  }
  if (text.isEmpty()) {
    text = extractArrayLines(responseDoc.as<JsonVariantConst>(), "lines");
  }
  if (text.isEmpty()) {
    errorText = "Proxy returned no 'text' or 'lines'.";
    return false;
  }
  return true;
}

bool ClassroomProxyClient::sheetsAppendRow(
    const String& spreadsheetId,
    const String& sheetName,
    const String* values,
    size_t valueCount,
    String& errorText) {
  errorText = "";
  if (spreadsheetId.isEmpty()) {
    errorText = "spreadsheetId is empty.";
    return false;
  }
  if (sheetName.isEmpty()) {
    errorText = "sheetName is empty.";
    return false;
  }
  if (values == nullptr || valueCount == 0) {
    errorText = "values are empty.";
    return false;
  }

  DynamicJsonDocument requestDoc(3072);
  requestDoc["spreadsheetId"] = spreadsheetId;
  requestDoc["sheetName"] = sheetName;
  requestDoc["device"] = config_.clientName;
  requestDoc["session"] = sessionId_;
  JsonArray arr = requestDoc.createNestedArray("values");
  for (size_t i = 0; i < valueCount; i++) {
    arr.add(values[i]);
  }

  DynamicJsonDocument responseDoc(2048);
  return proxyJsonRequest(config_.sheetsAppendRowPath, requestDoc, responseDoc, errorText);
}

bool ClassroomProxyClient::sheetsReadRange(
    const String& spreadsheetId,
    const String& sheetName,
    const String& rangeA1,
    String& text,
    String& errorText) {
  text = "";
  errorText = "";
  if (spreadsheetId.isEmpty()) {
    errorText = "spreadsheetId is empty.";
    return false;
  }
  if (sheetName.isEmpty()) {
    errorText = "sheetName is empty.";
    return false;
  }
  if (rangeA1.isEmpty()) {
    errorText = "range is empty.";
    return false;
  }

  DynamicJsonDocument requestDoc(2048);
  requestDoc["spreadsheetId"] = spreadsheetId;
  requestDoc["sheetName"] = sheetName;
  requestDoc["range"] = rangeA1;
  requestDoc["device"] = config_.clientName;
  requestDoc["session"] = sessionId_;

  DynamicJsonDocument responseDoc(24576);
  if (!proxyJsonRequest(config_.sheetsReadRangePath, requestDoc, responseDoc, errorText)) {
    return false;
  }

  if (responseDoc["text"].is<const char*>()) {
    text = responseDoc["text"].as<const char*>();
  }
  if (text.isEmpty()) {
    text = extractTableText(responseDoc.as<JsonVariantConst>(), "values");
  }
  if (text.isEmpty()) {
    errorText = "Proxy returned no 'text' or 'values'.";
    return false;
  }
  return true;
}

void ClassroomProxyClient::monitorWiFi() {
  const wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED) {
    if (!wifiWasConnected_) {
      wifiWasConnected_ = true;
      if (config_.enableSerialConsole) {
        Serial.printf("[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
        printPrompt();
      }
    }
    return;
  }

  if (wifiWasConnected_) {
    wifiWasConnected_ = false;
    if (config_.enableSerialConsole) {
      Serial.println(F("[WiFi] Disconnected."));
    }
  }

  const uint32_t now = millis();
  if (now - lastWifiRetryMs_ >= config_.wifiRetryIntervalMs) {
    lastWifiRetryMs_ = now;
    connectWiFi(false);
    if (config_.enableSerialConsole && WiFi.status() != WL_CONNECTED) {
      printPrompt();
    }
  }
}

void ClassroomProxyClient::handleSerialInput() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());

    if (ch == '\n' || ch == '\r') {
      if (ch == '\n' && lastSerialWasCR_) {
        lastSerialWasCR_ = false;
        continue;
      }
      lastSerialWasCR_ = (ch == '\r');

      Serial.println();
      serialInputBuffer_.trim();
      const String command = serialInputBuffer_;
      serialInputBuffer_ = "";
      processSerialCommand(command);
      continue;
    }
    lastSerialWasCR_ = false;

    if (ch == '\b' || ch == 127) {
      if (!serialInputBuffer_.isEmpty()) {
        serialInputBuffer_.remove(serialInputBuffer_.length() - 1);
        if (config_.echoSerialInput) {
          Serial.print(F("\b \b"));
        }
      }
      continue;
    }

    if (serialInputBuffer_.length() >= config_.maxPromptLength) {
      continue;
    }

    serialInputBuffer_ += ch;
    if (config_.echoSerialInput) {
      Serial.write(static_cast<uint8_t>(ch));
    }
  }
}

void ClassroomProxyClient::processSerialCommand(const String& command) {
  if (command.isEmpty()) {
    printPrompt();
    return;
  }

  if (command == "/help") {
    printHelp();
    printPrompt();
    return;
  }

  if (command == "/wifi") {
    connectWiFi(true);
    printPrompt();
    return;
  }

  if (command == "/docappend" || command.startsWith("/docappend ")) {
    String text = command.substring(String("/docappend").length());
    text.trim();
    if (text.isEmpty()) {
      Serial.println(F("[Doc] Usage: /docappend <text>"));
      printPrompt();
      return;
    }

    Serial.println(F("[Doc] Appending text to default Google Doc..."));
    String errorText;
    if (docAppendText(text, errorText)) {
      Serial.println(F("[Doc] OK"));
    } else {
      Serial.print(F("[Doc] ERROR: "));
      Serial.println(errorText);
    }
    Serial.println();
    printPrompt();
    return;
  }

  if (command == "/drivetext" || command.startsWith("/drivetext ")) {
    String fileId = command.substring(String("/drivetext").length());
    fileId.trim();
    if (fileId.isEmpty()) {
      Serial.println(F("[Drive] Usage: /drivetext <fileId>"));
      printPrompt();
      return;
    }

    Serial.printf("[Drive] Reading text from fileId=%s ...\n", fileId.c_str());
    String text;
    String errorText;
    if (driveReadText(fileId, text, errorText)) {
      Serial.println(F("[Drive] Text:"));
      Serial.println(text);
    } else {
      Serial.print(F("[Drive] ERROR: "));
      Serial.println(errorText);
    }
    Serial.println();
    printPrompt();
    return;
  }

  if (command == "/drivelines" || command.startsWith("/drivelines ")) {
    String args = command.substring(String("/drivelines").length());
    args.trim();
    if (args.isEmpty()) {
      Serial.println(F("[Drive] Usage: /drivelines <fileId> [from] [to]"));
      printPrompt();
      return;
    }

    int fromLine = -1;
    int toLine = -1;

    const int firstSpace = args.indexOf(' ');
    String fileId = args;
    if (firstSpace >= 0) {
      fileId = args.substring(0, firstSpace);
      String rest = args.substring(firstSpace + 1);
      rest.trim();

      if (!rest.isEmpty()) {
        const int secondSpace = rest.indexOf(' ');
        String fromToken = rest;
        String toToken;
        if (secondSpace >= 0) {
          fromToken = rest.substring(0, secondSpace);
          toToken = rest.substring(secondSpace + 1);
          toToken.trim();
        }
        fromToken.trim();
        if (!fromToken.isEmpty() && !parseIntStrict(fromToken, fromLine)) {
          Serial.println(F("[Drive] ERROR: 'from' must be an integer."));
          printPrompt();
          return;
        }
        if (!toToken.isEmpty() && !parseIntStrict(toToken, toLine)) {
          Serial.println(F("[Drive] ERROR: 'to' must be an integer."));
          printPrompt();
          return;
        }
      }
    }

    fileId.trim();
    if (fileId.isEmpty()) {
      Serial.println(F("[Drive] Usage: /drivelines <fileId> [from] [to]"));
      printPrompt();
      return;
    }

    Serial.printf(
        "[Drive] Reading lines from fileId=%s (from=%d, to=%d) ...\n",
        fileId.c_str(),
        fromLine,
        toLine);
    String text;
    String errorText;
    if (driveReadLines(fileId, fromLine, toLine, text, errorText)) {
      Serial.println(F("[Drive] Lines/Text:"));
      Serial.println(text);
    } else {
      Serial.print(F("[Drive] ERROR: "));
      Serial.println(errorText);
    }
    Serial.println();
    printPrompt();
    return;
  }

  String actualPrompt = command;
  if (command == "/demo") {
    actualPrompt = "Give a short greeting in Ukrainian and one sentence about ESP32.";
  }

  Serial.println();
  Serial.print(F("[You] "));
  Serial.println(actualPrompt);
  Serial.println(F("[Gemini] Sending request..."));

  String answer;
  String errorText;
  if (geminiPrompt(actualPrompt, answer, errorText)) {
    Serial.println(F("[Gemini] Response:"));
    Serial.println(answer);
  } else {
    Serial.print(F("[Gemini] ERROR: "));
    Serial.println(errorText);
  }
  Serial.println();
  printPrompt();
}
