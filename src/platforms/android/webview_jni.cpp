#include <jni.h>
#include <android/log.h>
#include <string>

#define LOG_TAG "NativeWebViewJni"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

extern "C" {
    // Call Java/Kotlin code from JNI to construct a fullscreen WebApp activity
    JNIEXPORT void JNICALL
    Java_com_example_booktogame_MainActivity_initNativeWebView(JNIEnv* env, jobject thiz, jobject webViewInstance) {
        LOGI("Successfully initialized C++ JNI bridge.");
        
        jclass webViewClass = env->GetObjectClass(webViewInstance);
        
        // Locate WebView methods for settings
        jmethodID getSettingsMethod = env->GetMethodID(webViewClass, "getSettings", "()Landroid/webkit/WebSettings;");
        jobject webSettingsInstance = env->CallObjectMethod(webViewInstance, getSettingsMethod);
        jclass webSettingsClass = env->GetObjectClass(webSettingsInstance);
        
        // Enable Javascript: webSettingsInstance.setJavaScriptEnabled(true)
        jmethodID setJavaScriptEnabledMethod = env->GetMethodID(webSettingsClass, "setJavaScriptEnabled", "(Z)V");
        env->CallVoidMethod(webSettingsInstance, setJavaScriptEnabledMethod, JNI_TRUE);
        
        // Enable Local DOM Storage: webSettingsInstance.setDomStorageEnabled(true)
        jmethodID setDomStorageEnabledMethod = env->GetMethodID(webSettingsClass, "setDomStorageEnabled", "(Z)V");
        env->CallVoidMethod(webSettingsInstance, setDomStorageEnabledMethod, JNI_TRUE);
        
        // Load WebAssembly HTML build URI
        jmethodID loadUrlMethod = env->GetMethodID(webViewClass, "loadUrl", "(Ljava/lang/String;)V");
        jstring urlStr = env->NewStringUTF("file:///android_asset/build_web/BOOK_TO_GAME.html");
        env->CallVoidMethod(webViewInstance, loadUrlMethod, urlStr);
        
        LOGI("JNI WebView configured and directed to local assets.");
    }
}
