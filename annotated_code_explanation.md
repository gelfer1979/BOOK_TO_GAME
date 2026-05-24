# Developer's Annotated Code Guide: BOOK_TO_GAME

This guide provides a comprehensive, line-by-block, and component-by-component explanation of the entire C++ codebase (`main.cpp` and `modelapi.h`). It is written in intermediate English to help any developer quickly understand the logic, architecture, and purpose of every command.

---

## 1. High-Level Architectural Blueprint
The project is built on a **Model-View-Controller (MVC)** pattern:

```mermaid
graph TD
    subgraph Model (modelapi.h)
        M[GameState] --> S[save.json]
        A[AskAiExternal] -->|cURL Requests| LLM[Local/Remote LLM]
    end
    subgraph View & Controller (main.cpp)
        C[SDL Main Loop] -->|Events & Input| L[Layout Engine]
        L -->|Render| V[SDL Renderer]
        C -->|SubmitQuery| T[Asynchronous Threads]
    end
    T -->|API Response| M
    M -->|SyncState| V
```

---

## 2. Comprehensive Analysis of `modelapi.h`

`modelapi.h` handles **Data Structures, AI Client Communication, and Text Parsers**.

### A. Data Representations & Structures
* **`ChatMessageData` (L19-L22)**:
  - Represents a simple message in the dialog bubbles.
  - `sender` ("User" or "AI") determines the bubble color and side.
  - `text` holds the raw narrative text.
* **`GameState` (L500-L557)**:
  - This is the master database of the adventure. It contains all fields serialized to `save.json`:
    - `currentChapter`: The 1-indexed chapter number.
    - `messages`: Historical dialog log.
    - `activeChoices`: Choices presented to the player as UI buttons.
    - `chapterSummaries`: Array of completed chapter summaries formatted as `"X-Y::[summary_text]"`.

### B. AskAi & AskAiExternal (L25-L345)
This class encapsulates the connection to LLM endpoints (LM Studio, OpenAI, Gemini).
* **`AskAiExternal::AskAiExternal(const std::string& configFilePath)` (L40-L97)**:
  - Loads API options from a JSON config (e.g. `ai_gemini.json`).
  - Checks if the file is in the current directory or the parent directory (`../`).
  - Securely overrides the API key using environment variables (`std::getenv`) if configured.
* **`AskAiExternal::ask(const std::string& question)` (L103-L277)**:
  - Performs synchronous HTTP POST requests using **libcurl**.
  - **Payload Construction**:
    - If `format` is `"gemini"`, builds a JSON with `"systemInstruction"` and `"contents"`.
    - Otherwise, builds a standard OpenAI JSON with `"messages"` (including `"role": "system"` and `"role": "user"`).
  - **Retry Policy**: Loops and retries the connection up to `maxRetries_` times with exponential backoff (`delayMs *= 2`) on transient errors (like HTTP 429 Rate Limits or 5xx server issues).
  - **Response Parsing**: Parses the response payload via `nlohmann::json` and returns the clean text response.

---

## 3. Comprehensive Analysis of `main.cpp`

`main.cpp` drives the **Game State Machine, SDL2 Windowing, Custom Text Wrapping, and UI Rendering Pipeline**.

### A. Game State Machine (`AppState` Enum, L30-L37)
The application operates in several distinct modes:
* `APP_STATE_ASK_CONTINUE`: Shows a start prompt asking if the player wants to resume their previous game.
* `APP_STATE_ENTER_TXT_PATH`: Displays a clean drag-and-drop or file dialog selector.
* `APP_STATE_SETUP`: An interactive chat setup where the player defines custom parameters.
* `APP_STATE_AI_GENERATING`: Shows a loading bar while the AI compiles the book's blueprint and hydration blocks.
* `APP_STATE_GAMEPLAY`: The primary text adventure gameplay screen.
* `APP_STATE_SELECT_AI`: Allows live switching between available local/remote AI models.

### B. Asynchronous Query Pipeline (`SubmitQuery`, L1930-L2260)
AI calls take time and would freeze the main UI loop if run synchronously. We run them in detached threads (`std::thread`):

1. **Chapter Transition Thread (L1997-L2130)**:
   - Triggered when the player clicks a "transition button".
   - Collects a slice of the **first `maxTurnsForce - 2` user turns** of the old chapter.
   - Requests a short chapter summary from `promptAiSummarizer`.
   - Checks the global `queryId` against `state.currentQueryId` to verify if the query has been cancelled.
   - If 10 uncompressed summaries have accumulated, launches a background **Summary Compression** request (`promptAiSummaryCompressor`).
   - If successful, commits the next chapter's starting narrative and clears the screen.
2. **Gameplay Turn Thread (L2242-L2260)**:
   - Captures the active user prompt and history.
   - Sends it to the AI in a background thread.
   - Writes the response to `state.pendingResponse` and sets `state.responseReady = true`.
3. **Response Consumer (`ConsumeApiResponse`, L2254-L2415)**:
   - Runs in the main UI loop. When `state.responseReady` becomes `true`:
     - Resets flags and parses any transition (`[next_chapter]`) or death (`[player_dead]`) tags.
     - Strips bullet formatting using `ExtractAndStripOptions`.
     - Appends the corrected response to `state.modelState.messages`.
     - Auto-saves state to `save.json`.

---

## 4. Line-by-Line Key Algorithm Spotlights

### A. Double Colon Summary Parser (`UpdateSystemPrompt` inside `modelapi.h`)
This splits compressed summaries (labeled like `1-10::Text`) from standard index summaries (labeled like `11::Text`):
```cpp
size_t sep = raw.find("::");
if (sep != std::string::npos) {
    label = raw.substr(0, sep);  // Extracts "1-10" or "11"
    text = raw.substr(sep + 2);  // Extracts the actual summary text
} else {
    label = std::to_string(i + 1); // Fallback to index if no separator is found
    text = raw;
}
```

### B. Language-Agnostic Option Parser (`ExtractAndStripOptions` inside `modelapi.h`)
Splits narrative block from option list via the custom block separator `|`:
```cpp
size_t splitPos = response.find('|');
if (splitPos != std::string::npos) {
    std::string optionsPart = response.substr(splitPos + 1);
    // Reads each line below '|' and strips bullets dynamically:
    // e.g. converts "-  1. Open the door" -> "Open the door"
}
```

### C. Safe Query Cancelation & Choice Reversion (L2808-L2840 inside `main.cpp`)
Executed when the player clicks "Main Menu" while the AI is thinking:
```cpp
state.currentQueryId++; // Instantly invalidates all active thread processes
if (state.aiThinking) {
    state.aiThinking = false;
    state.responseReady = false;
    
    // Pops the unanswered user prompt to restore dialog history state
    if (!state.modelState.messages.empty() && state.modelState.messages.back().sender == "User") {
        state.modelState.messages.pop_back();
    }
    
    // Restores clickable options to their previous interactive state
    if (!state.savedChoices.empty()) {
        state.modelState.activeChoices = state.savedChoices;
        state.savedChoices.clear();
    }
    SyncModelToUi();
    SaveGame(); // Writes the clean, cancelled state back to disk
}
```
