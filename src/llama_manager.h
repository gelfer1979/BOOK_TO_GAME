#ifndef LLAMA_MANAGER_H
#define LLAMA_MANAGER_H

#include <string>

// Starts the llama-server.exe if the model is LOCAL or LOCAL_LOW, otherwise stops it.
void StartLlamaServer(const std::string& modelJsonFile);

// Stops the llama-server.exe if it was running.
void StopLlamaServer();

#endif // LLAMA_MANAGER_H
