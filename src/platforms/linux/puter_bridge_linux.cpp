#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <unistd.h>

static GtkWidget* main_window = nullptr;
static WebKitWebView* webview_view = nullptr;
static std::string request_json_data;
static bool operation_completed = false;

// Helper to escape standard JSON for safe injection into a Javascript call
static std::string EscapeJavaScriptString(const std::string& input) {
    std::string result = "";
    for (char c : input) {
        if (c == '\\') result += "\\\\";
        else if (c == '"') result += "\\\"";
        else if (c == '\'') result += "\\'";
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else result += c;
    }
    return result;
}

// Write final output and exit
static void SaveResponseAndExit(const std::string& status, const std::string& responseText) {
    std::cout << "[Puter Bridge] SaveResponseAndExit: status=" << status << " response length=" << responseText.length() << std::endl;

    // Write puter_response.json
    std::ofstream responseFile("puter_response.json");
    if (responseFile.is_open()) {
        std::string escapedResponse = "";
        for (char c : responseText) {
            if (c == '\\') escapedResponse += "\\\\";
            else if (c == '"') escapedResponse += "\\\"";
            else if (c == '\n') escapedResponse += "\\n";
            else if (c == '\r') escapedResponse += "\\r";
            else if (c == '\t') escapedResponse += "\\t";
            else escapedResponse += c;
        }

        responseFile << "{\n";
        responseFile << "  \"status\": \"" << status << "\",\n";
        responseFile << "  \"response\": \"" << escapedResponse << "\"\n";
        responseFile << "}\n";
        responseFile.close();
    }
    operation_completed = true;
    gtk_main_quit();
}

// Callback when JavaScript posts a message via window.webkit.messageHandlers.puter.postMessage
static void on_script_message_received(WebKitUserContentManager* manager,
                                       WebKitJavascriptResult* message,
                                       gpointer user_data) {
#if WEBKIT_CHECK_VERSION(2, 22, 0)
    JSCValue* value = webkit_javascript_result_get_js_value(message);
    char* json_str = jsc_value_to_string(value);
#else
    WebKitJavascriptResult* res = message;
    JSGlobalContextRef context = webkit_javascript_result_get_global_context(res);
    JSValueRef value = webkit_javascript_result_get_value(res);
    JSStringRef js_str_ref = JSValueToStringCopy(context, value, NULL);
    size_t max_size = JSStringGetMaximumUTF8CStringSize(js_str_ref);
    char* json_str = (char*)g_malloc(max_size);
    JSStringGetUTF8CString(js_str_ref, json_str, max_size);
    JSStringRelease(js_str_ref);
#endif

    if (json_str != nullptr) {
        std::string msg(json_str);
        g_free(json_str);

        // Check for ready action to send payload (immediate invisible execution)
        if (msg.find("\"action\":\"ready_invisible\"") != std::string::npos) {
            if (webview_view != nullptr) {
                std::string js_code = "window.chrome.webview._deliverMessage('" + EscapeJavaScriptString(request_json_data) + "')";
                webkit_web_view_run_javascript(webview_view, js_code.c_str(), NULL, NULL, NULL);
            }
        }
        // Check for login window trigger (user needs to sign in)
        else if (msg.find("\"action\":\"show_login_window\"") != std::string::npos) {
            if (main_window != nullptr) {
                gtk_widget_show_all(main_window);
                gtk_window_present(GTK_WINDOW(main_window));
            }
        }
        // Check for successful login
        else if (msg.find("\"action\":\"login_success\"") != std::string::npos) {
            if (main_window != nullptr) {
                gtk_widget_hide(main_window);
            }
            if (webview_view != nullptr) {
                std::string js_code = "window.chrome.webview._deliverMessage('" + EscapeJavaScriptString(request_json_data) + "')";
                webkit_web_view_run_javascript(webview_view, js_code.c_str(), NULL, NULL, NULL);
            }
        }
        // Check for success response
        else if (msg.find("\"action\":\"response\"") != std::string::npos) {
            size_t txtTag = msg.find("\"text\":\"");
            if (txtTag != std::string::npos) {
                size_t start = txtTag + 8;
                size_t end = std::string::npos;
                size_t backslashes = 0;
                for (size_t i = start; i < msg.length(); ++i) {
                    if (msg[i] == '\\') {
                        backslashes++;
                    } else if (msg[i] == '"') {
                        if (backslashes % 2 == 0) {
                            end = i;
                            break;
                        }
                        backslashes = 0;
                    } else {
                        backslashes = 0;
                    }
                }
                if (end != std::string::npos && end > start) {
                    std::string escapedText = msg.substr(start, end - start);
                    
                    // Simple unescape helper
                    std::string unescaped = "";
                    for (size_t i = 0; i < escapedText.length(); ++i) {
                        if (escapedText[i] == '\\' && i + 1 < escapedText.length()) {
                            char next = escapedText[i + 1];
                            if (next == '\\') { unescaped += '\\'; i++; }
                            else if (next == '"') { unescaped += '"'; i++; }
                            else if (next == 'n') { unescaped += '\n'; i++; }
                            else if (next == 'r') { unescaped += '\r'; i++; }
                            else if (next == 't') { unescaped += '\t'; i++; }
                            else { unescaped += escapedText[i]; }
                        } else {
                            unescaped += escapedText[i];
                        }
                    }
                    SaveResponseAndExit("success", unescaped);
                } else {
                    SaveResponseAndExit("success", "");
                }
            } else {
                SaveResponseAndExit("success", "");
            }
        }
        // Check for error response
        else if (msg.find("\"action\":\"error\"") != std::string::npos) {
            size_t txtTag = msg.find("\"text\":\"");
            std::string errText = "Unknown Puter AI error";
            if (txtTag != std::string::npos) {
                size_t start = txtTag + 8;
                size_t end = std::string::npos;
                size_t backslashes = 0;
                for (size_t i = start; i < msg.length(); ++i) {
                    if (msg[i] == '\\') {
                        backslashes++;
                    } else if (msg[i] == '"') {
                        if (backslashes % 2 == 0) {
                            end = i;
                            break;
                        }
                        backslashes = 0;
                    } else {
                        backslashes = 0;
                    }
                }
                if (end != std::string::npos && end > start) {
                    errText = msg.substr(start, end - start);
                }
            }
            SaveResponseAndExit("error", errText);
        }
    }
}

// Timeout handler in case the network hangs
static gboolean on_timeout_fallback(gpointer data) {
    if (!operation_completed) {
        std::cerr << "[Puter Bridge] Timeout reached after 180 seconds." << std::endl;
        SaveResponseAndExit("error", "Puter bridge request timed out after 180 seconds.");
    }
    return FALSE;
}

// Custom URI Scheme Request callback to serve files under app://local/ instead of file://
static void uri_scheme_request_cb(WebKitURISchemeRequest* request, gpointer user_data) {
    std::string exeDir = *(std::string*)user_data;
    const char* path = webkit_uri_scheme_request_get_path(request);
    
    // Serve file from assets folder
    std::string filePath = exeDir + path;
    
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        GError* error = g_error_new(WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_FAILED, "File not found: %s", filePath.c_str());
        webkit_uri_scheme_request_finish_error(request, error);
        g_error_free(error);
        return;
    }
    
    // Read file content
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    // Determine mime type
    const char* mime_type = "text/html";
    if (filePath.find(".html") != std::string::npos) mime_type = "text/html";
    else if (filePath.find(".js") != std::string::npos) mime_type = "application/javascript";
    else if (filePath.find(".png") != std::string::npos) mime_type = "image/png";
    else if (filePath.find(".css") != std::string::npos) mime_type = "text/css";
    
    GInputStream* stream = g_memory_input_stream_new_from_data(content.c_str(), content.length(), NULL);
    webkit_uri_scheme_request_finish(request, stream, content.length(), mime_type);
    g_object_unref(stream);
}

// Destroy window handler for popup
static void on_popup_close(WebKitWebView* webview, gpointer user_data) {
    GtkWidget* window = GTK_WIDGET(user_data);
    gtk_widget_destroy(window);
}

// Show window handler when webview is fully ready to display
static void on_popup_ready_to_show(WebKitWebView* webview, gpointer user_data) {
    GtkWidget* window = GTK_WIDGET(user_data);
    gtk_widget_show_all(window);
}

// Intercept window.open calls to allow Puter.js's auth popup window to display correctly
static GtkWidget* create_webview_popup_cb(WebKitWebView* web_view,
                                          WebKitNavigationAction* navigation_action,
                                          gpointer user_data) {
    GtkWidget* popup_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(popup_window), "Puter Authentication");
    gtk_window_set_default_size(GTK_WINDOW(popup_window), 500, 600);
    gtk_window_set_position(GTK_WINDOW(popup_window), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_window_set_transient_for(GTK_WINDOW(popup_window), GTK_WINDOW(main_window));
    
    // Create new webview RELATED to the main webview!
    // This is crucial: it inherits the context, scheme handlers, processes and session (cookies).
    GtkWidget* popup_webview = webkit_web_view_new_with_related_view(webview_view);
    gtk_container_add(GTK_CONTAINER(popup_window), popup_webview);
    
    // Set settings to allow scripts
    WebKitSettings* settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(popup_webview));
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, TRUE);
    
    // Destroy GTK window when webview emits 'close' signal (e.g. self.close() on success)
    g_signal_connect(popup_webview, "close", G_CALLBACK(on_popup_close), popup_window);
    
    // Delay showing the window until the WebKit page is ready to be drawn
    g_signal_connect(popup_webview, "ready-to-show", G_CALLBACK(on_popup_ready_to_show), popup_window);
    
    // CRUCIAL: Manually load the requested URI in the popup WebView!
    if (navigation_action != nullptr) {
        WebKitURIRequest* req = webkit_navigation_action_get_request(navigation_action);
        if (req != nullptr) {
            const char* uri = webkit_uri_request_get_uri(req);
            if (uri != nullptr && strlen(uri) > 0) {
                std::cout << "[Puter Bridge] Popup loading URI: " << uri << std::endl;
                webkit_web_view_load_uri(WEBKIT_WEB_VIEW(popup_webview), uri);
            }
        }
    }
    
    return popup_webview;
}

int main(int argc, char* argv[]) {
    // 1. Read puter_request.json
    std::ifstream file("puter_request.json");
    if (!file.is_open()) {
        std::cerr << "[Puter Bridge] Failed to open puter_request.json input file" << std::endl;
        return 1;
    }

    std::string requestStr((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    file.close();
    request_json_data = requestStr;

    // 2. Initialize GTK
    gtk_init(&argc, &argv);

    // 3. Find execution directory for local assets loading
    std::string exeDir = ".";
    char path[1024];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        std::string exePath(path);
        size_t pos = exePath.find_last_of("\\/");
        if (pos != std::string::npos) {
            exeDir = exePath.substr(0, pos);
        }
    }

    // 4. Create GTK window invisibly
    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "Puter AI Authentication");
    gtk_window_set_default_size(GTK_WINDOW(main_window), 600, 700);
    g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // 5. Create WebKit WebView
    webview_view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    gtk_container_add(GTK_CONTAINER(main_window), GTK_WIDGET(webview_view));

    // Enable scripts and window.open popups
    WebKitSettings* settings = webkit_web_view_get_settings(webview_view);
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, TRUE);
    webkit_settings_set_enable_write_console_messages_to_stdout(settings, TRUE);

    // Register script message handler named "puter"
    WebKitUserContentManager* manager = webkit_web_view_get_user_content_manager(webview_view);
    g_signal_connect(manager, "script-message-received::puter",
                     G_CALLBACK(on_script_message_received), NULL);
    webkit_user_content_manager_register_script_message_handler(manager, "puter");

    // Intercept window.open calls to create popup windows correctly
    g_signal_connect(webview_view, "create", G_CALLBACK(create_webview_popup_cb), NULL);

    // Configure WebKit WebContext to register a Custom URI Scheme "app://"
    // This maps local assets directory so that the browser loads them via app:// protocol,
    // thereby bypassing Puter.js's "Unsupported Protocol: file://" security restrictions completely.
    WebKitWebContext* context = webkit_web_view_get_context(webview_view);
    
    // Register scheme handler for "app" scheme
    webkit_web_context_register_uri_scheme(context, "app", uri_scheme_request_cb, &exeDir, NULL);

    // Load puter_bridge.html via app://local/assets/puter_bridge.html
    std::string htmlPath = "app://local/assets/puter_bridge.html";
    std::cout << "[Puter Bridge] Loading WebView from custom URI: " << htmlPath << std::endl;
    webkit_web_view_load_uri(webview_view, htmlPath.c_str());

    // 6. Set 180s fallback timeout
    g_timeout_add_seconds(180, on_timeout_fallback, NULL);

    // 7. Start GTK loop
    gtk_main();

    return 0;
}
