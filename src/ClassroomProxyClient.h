#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class ClassroomProxyClient {
 public:
  struct Config {
    const char* wifiSsid;
    const char* wifiPassword;
    const char* proxyHost;
    uint16_t proxyPort;
    const char* geminiPath;
    const char* docsAppendPath;
    const char* driveReadTextPath;
    const char* driveReadLinesPath;
    const char* defaultGeminiModel;
    const char* clientName;
    uint32_t serialBaud;
    uint32_t wifiConnectTimeoutMs;
    uint32_t wifiRetryIntervalMs;
    uint32_t httpTimeoutMs;
    size_t maxPromptLength;
    bool enableSerialConsole;
    bool echoSerialInput;

    Config();
  };

  ClassroomProxyClient();
  explicit ClassroomProxyClient(const Config& config);

  void configure(const Config& config);

  void begin();
  void poll();

  void printHelp();
  void printPrompt();

  bool connectWiFi(bool forceReconnect = false);
  bool ensureWiFi();
  bool isWiFiConnected() const;

  const String& sessionId() const;
  const Config& config() const;

  bool geminiPrompt(const String& prompt, String& answer, String& errorText, const String& model = "");

  // Generic wrappers for proxy endpoints (Gemini/Docs/Drive via Python proxy + Apps Script).
  bool docAppendText(const String& text, String& errorText);
  bool driveReadText(const String& fileId, String& text, String& errorText);
  bool driveReadLines(const String& fileId, int fromLine, int toLine, String& text, String& errorText);

  bool proxyJsonRequest(
      const String& path,
      DynamicJsonDocument& requestDoc,
      DynamicJsonDocument& responseDoc,
      String& errorText);

 private:
  Config config_;
  String sessionId_;
  String serialInputBuffer_;
  bool wifiWasConnected_;
  bool lastSerialWasCR_;
  uint32_t lastWifiRetryMs_;

  bool hasPlaceholderWiFiSecrets() const;
  bool hasValidWiFiSecrets() const;
  String makeProxyUrl(const String& path) const;

  bool proxyPostJsonString(const String& path, const String& jsonBody, String& responseBody, int& httpCode, String& errorText);
  void monitorWiFi();
  void handleSerialInput();
  void processSerialCommand(const String& command);
};
