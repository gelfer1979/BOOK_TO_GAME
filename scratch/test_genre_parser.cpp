#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <fstream>
#include "../src/modelapi.h"

// Stub for CleanTextForFont if it needs it (modelapi.h defines it)
int main() {
    // 1. Create a mock GameState
    GameState modelState;
    
    // 2. Load settings.json manually to get the genrePreprocessingPipeline
    std::ifstream file("../settings.json");
    if (!file.is_open()) {
        file.open("settings.json");
    }
    if (file.is_open()) {
        try {
            nlohmann::json j;
            file >> j;
            if (j.contains("genrePreprocessingPipeline") && j["genrePreprocessingPipeline"].is_array()) {
                modelState.genrePreprocessingPipeline.clear();
                for (const auto& stepJson : j["genrePreprocessingPipeline"]) {
                    if (stepJson.is_object()) {
                        PipelineStep step;
                        if (stepJson.contains("action") && stepJson["action"].is_string()) {
                            step.action = stepJson["action"].get<std::string>();
                        }
                        if (stepJson.contains("target") && stepJson["target"].is_string()) {
                            step.target = stepJson["target"].get<std::string>();
                        }
                        if (stepJson.contains("replacement") && stepJson["replacement"].is_string()) {
                            step.replacement = stepJson["replacement"].get<std::string>();
                        }
                        modelState.genrePreprocessingPipeline.push_back(step);
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "JSON Load error: " << e.what() << std::endl;
        }
        file.close();
    } else {
        std::cerr << "Failed to open settings.json" << std::endl;
        return 1;
    }

    std::cout << "Loaded " << modelState.genrePreprocessingPipeline.size() << " pipeline steps." << std::endl;

    // 3. Define sample JSON response from AI
    std::string genresText = "```json\n{\n  \"genres\": [\n    \"Фэнтези\",\n    \"Фантастика\",\n    \"Киберпанк\",\n    \"Мистика\",\n    \"Ужасы\"\n  ]\n}\n```";
    std::cout << "--- BEFORE PREPROCESSING ---\n" << genresText << "\n----------------------------" << std::endl;

    // 4. Run PreprocessRawAiResponse
    std::string sep = "\x1F";
    PreprocessRawAiResponse(genresText, sep, modelState.genrePreprocessingPipeline);
    std::cout << "--- AFTER PREPROCESSING ---\n" << genresText << "\n----------------------------" << std::endl;

    // 5. Parse lines
    std::vector<std::string> parsedGenres;
    std::stringstream ss(genresText);
    std::string line;
    while (std::getline(ss, line)) {
        line = Trim(line);
        if (line.empty()) continue;
        
        bool onlyPunct = true;
        for (unsigned char c : line) {
            if (c >= 128 || ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
                onlyPunct = false;
                break;
            }
        }
        if (onlyPunct) {
            std::cout << "Skipped onlyPunct line: \"" << line << "\"" << std::endl;
            continue;
        }
        
        std::string lowerLine = ToLower(line);
        if (lowerLine.find("genres") != std::string::npos || 
            lowerLine.find("genre") != std::string::npos ||
            lowerLine.find("excerpt") != std::string::npos ||
            lowerLine.find("thematic") != std::string::npos) {
            std::cout << "Skipped keyword line: \"" << line << "\"" << std::endl;
            continue;
        }
        
        if (line.size() > 2 && line[0] == '-' && line[1] == ' ') line = line.substr(2);
        else if (line.size() > 3 && std::isdigit(line[0]) && line[1] == '.' && line[2] == ' ') line = line.substr(3);
        else if (line.size() > 2 && std::isdigit(line[0]) && line[1] == '.') line = line.substr(2);
        
        line = CleanTextForFont(Trim(line));
        if (!line.empty()) {
            int spaceCount = std::count(line.begin(), line.end(), ' ');
            if (line.length() <= 35 && spaceCount <= 3) {
                parsedGenres.push_back(line);
            } else {
                std::cout << "Skipped too long or too many spaces: \"" << line << "\"" << std::endl;
            }
        }
    }

    std::cout << "--- PARSED GENRES (" << parsedGenres.size() << ") ---" << std::endl;
    for (const auto& g : parsedGenres) {
        std::cout << "  - \"" << g << "\"" << std::endl;
    }

    return 0;
}
