#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <unistd.h>
#include <limits.h>
#include <string>

static void destroyWindowCb(GtkWidget* widget, gpointer data) {
    gtk_main_quit();
}

int main(int argc, char* argv[]) {
    // Initialize GTK Toolkit
    gtk_init(&argc, &argv);

    // Create top-level GtkWindow
    GtkWidget* main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(main_window), 1280, 720);
    gtk_window_set_title(GTK_WINDOW(main_window), "BOOK_TO_GAME Native Client (Linux)");

    // Initialize WebKit configuration settings
    WebKitSettings* settings = webkit_settings_new();
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_enable_webgl(settings, TRUE);
    webkit_settings_set_enable_write_console_messages_to_stdout(settings, TRUE);

    // Create the WebKit WebView and apply the configurations
    GtkWidget* web_view = webkit_web_view_new_with_settings(settings);
    gtk_container_add(GTK_CONTAINER(main_window), web_view);

    // Connect signals for close event
    g_signal_connect(main_window, "destroy", G_CALLBACK(destroyWindowCb), NULL);

    // Load locally served WebAssembly build (which loads Puter.js dynamically)
    char exePath[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    std::string htmlPath;
    if (len != -1) {
        exePath[len] = '\0';
        std::string exeDir = exePath;
        size_t pos = exeDir.find_last_of("/");
        if (pos != std::string::npos) {
            exeDir = exeDir.substr(0, pos);
        }
        htmlPath = "file://" + exeDir + "/BOOK_TO_GAME.html";
    } else {
        htmlPath = "file://./BOOK_TO_GAME.html";
    }
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(web_view), htmlPath.c_str());

    // Display widgets
    gtk_widget_show_all(main_window);

    // Enter GTK Event Loop
    gtk_main();

    return 0;
}
