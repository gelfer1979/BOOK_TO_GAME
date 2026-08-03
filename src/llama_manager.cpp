#include "llama_manager.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
static PROCESS_INFORMATION g_llamaServerProcessInfo = {0};
static std::string g_llamaServerCurrentModel = "";
#endif

void StopLlamaServer() {
#ifdef _WIN32
  if (g_llamaServerProcessInfo.hProcess != NULL) {
    std::cout << "[Llama Server] Stopping llama_server process..." << std::endl;
    TerminateProcess(g_llamaServerProcessInfo.hProcess, 0);
    CloseHandle(g_llamaServerProcessInfo.hProcess);
    CloseHandle(g_llamaServerProcessInfo.hThread);
    g_llamaServerProcessInfo.hProcess = NULL;
    g_llamaServerProcessInfo.hThread = NULL;
    g_llamaServerCurrentModel = "";
  }
#endif
}

void LogLlama(const std::string& msg) {
    std::ofstream logFile("llama_log.txt", std::ios::app);
    if (logFile.is_open()) {
        logFile << msg << "\n";
        logFile.close();
    }
    std::cout << msg << std::endl;
}

void StartLlamaServer(const std::string &modelJsonFile) {
#ifdef _WIN32
  // Log entry point
  LogLlama("[Llama Server] StartLlamaServer called with config: " + modelJsonFile);

  // 1. Extract model json name
  std::string filename = modelJsonFile;
  size_t lastSlash = filename.find_last_of("\\/");
  if (lastSlash != std::string::npos) {
    filename = filename.substr(lastSlash + 1);
  }

  bool isLocal = (filename == "ai_local.json" || filename == "ai_local_low.json");
  if (!isLocal) {
    LogLlama("[Llama Server] Model '" + filename + "' is not a local model. Stopping server.");
    StopLlamaServer();
    return;
  }

  // If it's already running, keep it running (no restart)
  if (g_llamaServerProcessInfo.hProcess != NULL) {
    LogLlama("[Llama Server] Llama server is already running. Current model: " + filename);
    return;
  }

  // 2. Read settings.json to get "LOCAL" options
  std::string settingsPath = "settings.json";
  std::ifstream sFile(settingsPath);
  if (!sFile.is_open()) {
    settingsPath = "../settings.json";
    sFile.open(settingsPath);
  }

  int ctxSize = 4096;
  int threads = 4;
  int ngl = 0;
  float temp = 0.7f;
  bool contextShift = false;
  bool flashAttn = false;
  bool noMmap = false;
  bool thinking = false;
  bool showWindow = false;

  if (sFile.is_open()) {
    try {
      nlohmann::json j;
      sFile >> j;
      if (j.contains("LOCAL") && j["LOCAL"].is_object()) {
        auto localSettings = j["LOCAL"];
        if (localSettings.contains("context_length") && localSettings["context_length"].is_number()) {
          ctxSize = localSettings["context_length"].get<int>();
        }
        if (localSettings.contains("threads") && localSettings["threads"].is_number()) {
          threads = localSettings["threads"].get<int>();
        }
        if (localSettings.contains("gpu_offload")) {
          if (localSettings["gpu_offload"].is_number()) {
            ngl = localSettings["gpu_offload"].get<int>();
          } else if (localSettings["gpu_offload"].is_string()) {
            std::string offloadStr = localSettings["gpu_offload"].get<std::string>();
            if (offloadStr == "none") {
              ngl = 0;
            } else if (offloadStr == "max") {
              ngl = 99;
            } else {
              try {
                ngl = std::stoi(offloadStr);
              } catch (...) {
                ngl = 0;
              }
            }
          }
        }
        if (localSettings.contains("temperature") && localSettings["temperature"].is_number()) {
          temp = localSettings["temperature"].get<float>();
        }
        if (localSettings.contains("context-shift") && localSettings["context-shift"].is_boolean()) {
          contextShift = localSettings["context-shift"].get<bool>();
        }
        if (localSettings.contains("flash-attn") && localSettings["flash-attn"].is_boolean()) {
          flashAttn = localSettings["flash-attn"].get<bool>();
        }
        if (localSettings.contains("no-mmap") && localSettings["no-mmap"].is_boolean()) {
          noMmap = localSettings["no-mmap"].get<bool>();
        } else if (localSettings.contains("no-map") && localSettings["no-map"].is_boolean()) {
          noMmap = localSettings["no-map"].get<bool>();
        }
        if (localSettings.contains("thinking") && localSettings["thinking"].is_boolean()) {
          thinking = localSettings["thinking"].get<bool>();
        }
        if (localSettings.contains("show_window") && localSettings["show_window"].is_boolean()) {
          showWindow = localSettings["show_window"].get<bool>();
        }
      }
    } catch (const std::exception& e) {
      LogLlama("[Llama Server] Error parsing settings.json: " + std::string(e.what()));
    }
    sFile.close();
  } else {
    LogLlama("[Llama Server] Warning: Could not open settings.json to read parameters.");
  }

  // Resolve paths
  std::string serverName = "llama-server.exe";
  std::string serverPath = "assets/" + serverName;
  std::string assetsDir = "assets";
  if (!std::filesystem::exists(serverPath)) {
    serverPath = "../assets/" + serverName;
    assetsDir = "../assets";
  }
  if (!std::filesystem::exists(serverPath)) {
    serverPath = serverName;
    assetsDir = ".";
  }

  if (!std::filesystem::exists(serverPath)) {
    LogLlama("[Llama Server] ERROR: llama-server.exe not found at resolved path: " + serverPath);
    return;
  }

  std::string absoluteServerPath = std::filesystem::absolute(serverPath).string();
  std::string absoluteAssetsDir = std::filesystem::absolute(assetsDir).string();

  LogLlama("[Llama Server] Launching: " + absoluteServerPath);
  LogLlama("[Llama Server] Working directory for DLL resolution: " + absoluteAssetsDir);

  STARTUPINFOA si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = showWindow ? SW_SHOW : SW_HIDE;

  ZeroMemory(&g_llamaServerProcessInfo, sizeof(g_llamaServerProcessInfo));

  // Construct command line with long parameters
  // Note: we use "." for models-dir since working directory is set to absoluteAssetsDir (assets/)
  std::string cmd = "\"" + absoluteServerPath + "\" --models-dir \".\" --models-max 2 --port 8080";
  cmd += " --ctx-size " + std::to_string(ctxSize);
  cmd += " --threads " + std::to_string(threads);
  cmd += " --n-gpu-layers " + std::to_string(ngl);
  cmd += " --temperature " + std::to_string(temp);
  if (contextShift) {
    cmd += " --context-shift";
  }
  if (flashAttn) {
    cmd += " --flash-attn on";
  } else {
    cmd += " --flash-attn off";
  }
  if (noMmap) {
    cmd += " --no-mmap";
  }
  if (!thinking) {
    cmd += " --reasoning off --reasoning-budget 0";
  }

  LogLlama("[Llama Server] showWindow value parsed: " + std::string(showWindow ? "true" : "false"));
  LogLlama("[Llama Server] Full Command Line: " + cmd);

  std::vector<char> cmdCopy(cmd.begin(), cmd.end());
  cmdCopy.push_back('\0');

  DWORD creationFlags = showWindow ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW;

  if (CreateProcessA(
      NULL,
      cmdCopy.data(),
      NULL,
      NULL,
      FALSE,
      creationFlags,
      NULL,
      absoluteAssetsDir.c_str(), // Working dir set to assets/ where dlls live
      &si,
      &g_llamaServerProcessInfo
  )) {
    LogLlama("[Llama Server] Llama server process spawned successfully.");
    g_llamaServerCurrentModel = filename;

    // Check if it stays running or dies immediately
    Sleep(500);
    DWORD exitCode = 0;
    if (GetExitCodeProcess(g_llamaServerProcessInfo.hProcess, &exitCode)) {
      if (exitCode != STILL_ACTIVE) {
        LogLlama("[Llama Server] ERROR: Llama server exited immediately with exit code: " + std::to_string(exitCode));
        CloseHandle(g_llamaServerProcessInfo.hProcess);
        CloseHandle(g_llamaServerProcessInfo.hThread);
        g_llamaServerProcessInfo.hProcess = NULL;
        g_llamaServerProcessInfo.hThread = NULL;
      } else {
        LogLlama("[Llama Server] Llama server process is running and active (STILL_ACTIVE).");
      }
    }
  } else {
    DWORD err = GetLastError();
    LogLlama("[Llama Server] ERROR: Failed to launch llama-server.exe process. CreateProcessA error code: " + std::to_string(err));
  }
#endif
}
