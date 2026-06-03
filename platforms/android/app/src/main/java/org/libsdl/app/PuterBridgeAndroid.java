package org.libsdl.app;

import android.annotation.SuppressLint;
import android.annotation.TargetApi;
import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.content.DialogInterface;
import android.net.Uri;
import android.os.Build;
import android.os.Message;
import android.util.Log;
import android.view.ViewGroup;
import android.webkit.CookieManager;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import java.io.IOException;
import java.io.InputStream;
import org.json.JSONArray;
import org.json.JSONObject;

public class PuterBridgeAndroid {
    private static final String TAG = "PuterBridgeAndroid";
    private static WebView webView = null;
    private static Dialog mainDialog = null;

    @SuppressLint("SetJavaScriptEnabled")
    public static String callPuter(final String historyJson, final String systemPrompt, final String modelName) {
        Log.i(TAG, "callPuter starting for model: " + modelName);
        final Activity activity = (Activity) SDLActivity.getContext();
        if (activity == null) {
            Log.e(TAG, "Activity context is null, cannot proceed.");
            return "Error: Android Activity context is null.";
        }

        final String[] responseHolder = new String[1];
        final boolean[] completed = new boolean[1];

        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    if (webView != null) {
                        webView.destroy();
                    }

                    webView = new WebView(activity);
                    WebSettings settings = webView.getSettings();
                    settings.setJavaScriptEnabled(true);
                    settings.setDomStorageEnabled(true);
                    settings.setSupportMultipleWindows(true);
                    settings.setJavaScriptCanOpenWindowsAutomatically(true);

                    // Spoof User Agent to bypass Google OAuth WebView block
                    String defaultUA = settings.getUserAgentString();
                    if (defaultUA != null) {
                        String spoofedUA = defaultUA.replace("Version/4.0 ", "").replace("; wv", "");
                        settings.setUserAgentString(spoofedUA);
                        Log.d(TAG, "Spoofed Main WebView UA: " + spoofedUA);
                    }

                    // Allow cookies
                    CookieManager.getInstance().setAcceptCookie(true);
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                        CookieManager.getInstance().setAcceptThirdPartyCookies(webView, true);
                    }

                    // Javascript interface for bridge
                    webView.addJavascriptInterface(new Object() {
                        @JavascriptInterface
                        public void postMessage(String jsonStr) {
                            Log.d(TAG, "Received message from JS: " + jsonStr);
                            try {
                                JSONObject msg = new JSONObject(jsonStr);
                                String action = msg.optString("action");
                                if ("ready_invisible".equals(action)) {
                                    // HTML bridge ready, load prompt
                                    activity.runOnUiThread(new Runnable() {
                                        @Override
                                        public void run() {
                                            deliverRequest(historyJson, systemPrompt, modelName);
                                        }
                                    });
                                } else if ("show_login_window".equals(action)) {
                                    // Show the main webView inside a Dialog so the user can interact with the login UI
                                    activity.runOnUiThread(new Runnable() {
                                        @Override
                                        public void run() {
                                            if (webView != null) {
                                                if (mainDialog != null && mainDialog.isShowing()) {
                                                    return;
                                                }
                                                if (webView.getParent() != null) {
                                                    ((ViewGroup) webView.getParent()).removeView(webView);
                                                }
                                                mainDialog = new Dialog(activity, android.R.style.Theme_Black_NoTitleBar_Fullscreen);
                                                mainDialog.setContentView(webView);
                                                mainDialog.setCancelable(true);
                                                mainDialog.setOnCancelListener(new DialogInterface.OnCancelListener() {
                                                    @Override
                                                    public void onCancel(DialogInterface dialogInterface) {
                                                        Log.w(TAG, "Main dialog cancelled by user.");
                                                        completeRequest("error", "User cancelled Puter authentication.", responseHolder, completed);
                                                    }
                                                });
                                                mainDialog.show();
                                            }
                                        }
                                    });
                                } else if ("login_success".equals(action)) {
                                    // Hide the login dialog, and immediately deliver the AI prompt request to WebView
                                    activity.runOnUiThread(new Runnable() {
                                        @Override
                                        public void run() {
                                            if (mainDialog != null) {
                                                mainDialog.dismiss();
                                                mainDialog = null;
                                            }
                                            deliverRequest(historyJson, systemPrompt, modelName);
                                        }
                                    });
                                } else if ("response".equals(action)) {
                                    String text = msg.optString("text");
                                    completeRequest("success", text, responseHolder, completed);
                                } else if ("error".equals(action)) {
                                    String text = msg.optString("text");
                                    completeRequest("error", text, responseHolder, completed);
                                }
                            } catch (Exception e) {
                                Log.e(TAG, "Error handling message from JS", e);
                            }
                        }
                    }, "PuterAndroidBridge");

                    webView.setWebViewClient(new WebViewClient() {
                        @SuppressWarnings("deprecation")
                        @Override
                        public WebResourceResponse shouldInterceptRequest(WebView view, String url) {
                            return interceptRequest(view.getContext(), Uri.parse(url));
                        }

                        @TargetApi(Build.VERSION_CODES.LOLLIPOP)
                        @Override
                        public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
                            return interceptRequest(view.getContext(), request.getUrl());
                        }

                        private WebResourceResponse interceptRequest(Context context, Uri uri) {
                            String host = uri.getHost();
                            String path = uri.getPath();
                            if ("localhost".equals(host) && uri.getPath() != null && path.startsWith("/assets/")) {
                                try {
                                    String filename = path.substring("/assets/".length());
                                    InputStream is = context.getAssets().open(filename);
                                    String mimeType = "text/html";
                                    if (filename.endsWith(".js")) mimeType = "application/javascript";
                                    else if (filename.endsWith(".css")) mimeType = "text/css";
                                    else if (filename.endsWith(".png")) mimeType = "image/png";

                                    return new WebResourceResponse(mimeType, "UTF-8", is);
                                } catch (IOException e) {
                                    Log.e(TAG, "Failed to load local asset: " + path, e);
                                }
                            }
                            return null;
                        }

                        @Override
                        public void onReceivedError(WebView view, int errorCode, String description, String failingUrl) {
                            Log.e(TAG, "WebView error: " + description + " for URL: " + failingUrl);
                        }
                    });

                    webView.setWebChromeClient(new WebChromeClient() {
                        @Override
                        public boolean onCreateWindow(WebView view, boolean isDialog, boolean isUserGesture, Message resultMsg) {
                            Log.i(TAG, "onCreateWindow: Intercepting popup request");
                            WebView popupWebView = new WebView(activity);
                            WebSettings popupSettings = popupWebView.getSettings();
                            popupSettings.setJavaScriptEnabled(true);
                            popupSettings.setDomStorageEnabled(true);
                            popupSettings.setSupportMultipleWindows(true);
                            popupSettings.setJavaScriptCanOpenWindowsAutomatically(true);

                            // Spoof User Agent in popup WebView too to bypass Google OAuth WebView block
                            String defaultPopupUA = popupSettings.getUserAgentString();
                            if (defaultPopupUA != null) {
                                String spoofedPopupUA = defaultPopupUA.replace("Version/4.0 ", "").replace("; wv", "");
                                popupSettings.setUserAgentString(spoofedPopupUA);
                                Log.d(TAG, "Spoofed Popup WebView UA: " + spoofedPopupUA);
                            }

                            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                                CookieManager.getInstance().setAcceptThirdPartyCookies(popupWebView, true);
                            }

                            // Use an immersive Dialog to overlay the popup
                            final Dialog dialog = new Dialog(activity, android.R.style.Theme_Black_NoTitleBar_Fullscreen);
                            dialog.setContentView(popupWebView);
                            dialog.show();

                            popupWebView.setWebChromeClient(new WebChromeClient() {
                                @Override
                                public void onCloseWindow(WebView window) {
                                    Log.i(TAG, "onCloseWindow: Dismissing popup dialog");
                                    dialog.dismiss();
                                }
                            });

                            popupWebView.setWebViewClient(new WebViewClient() {
                                @Override
                                public boolean shouldOverrideUrlLoading(WebView view, String url) {
                                    return false; // Load inside popupWebView
                                }
                            });

                            WebView.WebViewTransport transport = (WebView.WebViewTransport) resultMsg.obj;
                            transport.setWebView(popupWebView);
                            resultMsg.sendToTarget();
                            return true;
                        }
                    });

                    // Load local bridge page
                    webView.loadUrl("http://localhost:8000/assets/puter_bridge.html");

                } catch (Exception e) {
                    Log.e(TAG, "Failed to initialize WebView", e);
                    completeRequest("error", "Failed to initialize WebView: " + e.getMessage(), responseHolder, completed);
                }
            }
        });

        // Block JNI thread until complete
        synchronized (completed) {
            while (!completed[0]) {
                try {
                    completed.wait();
                } catch (InterruptedException e) {
                    Log.e(TAG, "JNI Thread interrupted", e);
                }
            }
        }

        return responseHolder[0];
    }

    private static void deliverRequest(String historyJson, String systemPrompt, String modelName) {
        try {
            JSONObject payload = new JSONObject();
            payload.put("systemPrompt", systemPrompt);
            payload.put("modelName", modelName);
            payload.put("history", new JSONArray(historyJson));

            String escapedPayload = payload.toString().replace("\\", "\\\\").replace("'", "\\'");
            String js = "window.chrome.webview._deliverMessage('" + escapedPayload + "')";
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
                webView.evaluateJavascript(js, null);
            } else {
                webView.loadUrl("javascript:" + js);
            }
        } catch (Exception e) {
            Log.e(TAG, "Error delivering payload to JS", e);
        }
    }

    private static void completeRequest(String status, String text, String[] responseHolder, boolean[] completed) {
        synchronized (completed) {
            if ("success".equals(status)) {
                responseHolder[0] = text;
            } else {
                responseHolder[0] = "Error: " + text;
            }
            completed[0] = true;
            completed.notifyAll();
        }

        final Activity activity = (Activity) SDLActivity.getContext();
        if (activity != null) {
            activity.runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    if (mainDialog != null) {
                        mainDialog.dismiss();
                        mainDialog = null;
                    }
                    if (webView != null) {
                        if (webView.getParent() != null) {
                            ((ViewGroup) webView.getParent()).removeView(webView);
                        }
                        webView.destroy();
                        webView = null;
                    }
                }
            });
        }
    }
}
