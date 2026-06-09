#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#include <fstream>
#include <iostream>
#include <string>

// Private WebKit APIs to register custom schemes as secure and CORS-enabled.
// This allows cross-origin window.opener access and postMessage to work correctly.
@interface WKProcessPool (PrivateSchemeRegistration)
+ (void)_registerURLSchemeAsSecure:(NSString *)scheme;
+ (void)_registerURLSchemeAsCORSEnabled:(NSString *)scheme;
+ (void)_registerURLSchemeAsBypassingContentSecurityPolicy:(NSString *)scheme;
@end

@interface OffscreenWindow : NSWindow
@end

@implementation OffscreenWindow
- (NSRect)constrainFrameRect:(NSRect)frameRect toScreen:(NSScreen *)screen {
    return frameRect;
}
@end

// Global state
static NSWindow* __strong mainWindow = nil;
static WKWebView* __strong mainWebView = nil;
static NSString* __strong requestJsonData = nil;
static BOOL operationCompleted = NO;
static NSMutableArray* __strong activePopupWindows = nil;
static id __strong appDelegate = nil;
static id __strong activityToken = nil;

// Logging helper
static void LogToFile(NSString* message) {
    std::cout << [message UTF8String] << std::endl;
    NSString* logLine = [NSString stringWithFormat:@"%@\n", message];
    NSFileHandle* fileHandle = [NSFileHandle fileHandleForWritingAtPath:@"puter_bridge_log.txt"];
    if (fileHandle) {
        [fileHandle seekToEndOfFile];
        [fileHandle writeData:[logLine dataUsingEncoding:NSUTF8StringEncoding]];
        [fileHandle closeFile];
    } else {
        [logLine writeToFile:@"puter_bridge_log.txt" atomically:YES encoding:NSUTF8StringEncoding error:nil];
    }
}

// Helper to escape standard JSON for safe injection into a Javascript call
static NSString* EscapeJavaScriptString(NSString* input) {
    NSMutableString* s = [input mutableCopy];
    [s replaceOccurrencesOfString:@"\\" withString:@"\\\\" options:0 range:NSMakeRange(0, [s length])];
    [s replaceOccurrencesOfString:@"\"" withString:@"\\\"" options:0 range:NSMakeRange(0, [s length])];
    [s replaceOccurrencesOfString:@"'" withString:@"\\'" options:0 range:NSMakeRange(0, [s length])];
    [s replaceOccurrencesOfString:@"\n" withString:@"\\n" options:0 range:NSMakeRange(0, [s length])];
    [s replaceOccurrencesOfString:@"\r" withString:@"\\r" options:0 range:NSMakeRange(0, [s length])];
    [s replaceOccurrencesOfString:@"\t" withString:@"\\t" options:0 range:NSMakeRange(0, [s length])];
    return s;
}

// Write final output and exit
static void SaveResponseAndExit(NSString* status, NSString* responseText) {
    LogToFile([NSString stringWithFormat:@"[Puter Bridge] SaveResponseAndExit: status=%@ response length=%lu", status, (unsigned long)[responseText length]]);

    // Escaping for JSON format
    NSMutableString* escapedResponse = [responseText mutableCopy];
    [escapedResponse replaceOccurrencesOfString:@"\\" withString:@"\\\\" options:0 range:NSMakeRange(0, [escapedResponse length])];
    [escapedResponse replaceOccurrencesOfString:@"\"" withString:@"\\\"" options:0 range:NSMakeRange(0, [escapedResponse length])];
    [escapedResponse replaceOccurrencesOfString:@"\n" withString:@"\\n" options:0 range:NSMakeRange(0, [escapedResponse length])];
    [escapedResponse replaceOccurrencesOfString:@"\r" withString:@"\\r" options:0 range:NSMakeRange(0, [escapedResponse length])];
    [escapedResponse replaceOccurrencesOfString:@"\t" withString:@"\\t" options:0 range:NSMakeRange(0, [escapedResponse length])];

    NSString* jsonString = [NSString stringWithFormat:@"{\n  \"status\": \"%@\",\n  \"response\": \"%@\"\n}\n", status, escapedResponse];
    
    NSError* error = nil;
    [jsonString writeToFile:@"puter_response.json" atomically:YES encoding:NSUTF8StringEncoding error:&error];
    if (error) {
        LogToFile([NSString stringWithFormat:@"[Puter Bridge] ERROR: Failed to write puter_response.json: %@", [error localizedDescription]]);
    }
    
    operationCompleted = YES;
    
    if (activityToken) {
        [[NSProcessInfo processInfo] endActivity:activityToken];
        activityToken = nil;
    }
    
    // Sleep for 1 second on the main thread to allow background WebKit processes (e.g. database/network)
    // to write cookies and localStorage to disk.
    [NSThread sleepForTimeInterval:1.0];
    
    // Perform a direct OS-level exit with code 0 to avoid Cocoa window/teardown crashes.
    exit(0);
}

static NSMutableDictionary* ReadLocalStorageFile(void) {
    NSString* storePath = @"puter_store.json";
    if ([[NSFileManager defaultManager] fileExistsAtPath:storePath]) {
        NSData* data = [NSData dataWithContentsOfFile:storePath];
        if (data) {
            NSError* jsonError = nil;
            NSDictionary* dict = [NSJSONSerialization JSONObjectWithData:data options:0 error:&jsonError];
            if (dict) {
                LogToFile([NSString stringWithFormat:@"[ReadLocalStorage] Loaded %lu keys from %@", (unsigned long)[dict count], storePath]);
                return [dict mutableCopy];
            } else {
                LogToFile([NSString stringWithFormat:@"[ReadLocalStorage] ERROR: Failed to parse JSON: %@", [jsonError localizedDescription]]);
            }
        } else {
            LogToFile([NSString stringWithFormat:@"[ReadLocalStorage] ERROR: Failed to read data from %@", storePath]);
        }
    } else {
        LogToFile([NSString stringWithFormat:@"[ReadLocalStorage] Info: File %@ does not exist", storePath]);
    }
    return [NSMutableDictionary dictionary];
}

static void WriteLocalStorageFile(NSDictionary* dict) {
    NSString* storePath = @"puter_store.json";
    NSData* data = [NSJSONSerialization dataWithJSONObject:dict options:NSJSONWritingPrettyPrinted error:nil];
    if (data) {
        [data writeToFile:storePath atomically:YES];
    }
}

static void UpdateLocalStorageKey(NSString* key, NSString* value) {
    NSMutableDictionary* dict = ReadLocalStorageFile();
    dict[key] = value;
    WriteLocalStorageFile(dict);
}

static void RemoveLocalStorageKey(NSString* key) {
    NSMutableDictionary* dict = ReadLocalStorageFile();
    [dict removeObjectForKey:key];
    WriteLocalStorageFile(dict);
}

static void ClearLocalStorage(void) {
    WriteLocalStorageFile([NSDictionary dictionary]);
}

@interface BridgeScriptMessageHandler : NSObject <WKScriptMessageHandler>
@end

@implementation BridgeScriptMessageHandler

- (void)userContentController:(WKUserContentController *)userContentController
      didReceiveScriptMessage:(WKScriptMessage *)message {
    if (![message.name isEqualToString:@"puter"]) return;
    
    NSString* msg = message.body;
    if (![msg isKindOfClass:[NSString class]]) return;
    
    // Handle local storage synchronization from JavaScript
    if ([msg containsString:@"\"action\":\"store_setItem\""]) {
        NSData* data = [msg dataUsingEncoding:NSUTF8StringEncoding];
        NSDictionary* json = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        if (json) {
            NSString* key = json[@"key"];
            NSString* value = json[@"value"];
            if (key && value) {
                UpdateLocalStorageKey(key, value);
            }
        }
        return;
    }
    else if ([msg containsString:@"\"action\":\"store_removeItem\""]) {
        NSData* data = [msg dataUsingEncoding:NSUTF8StringEncoding];
        NSDictionary* json = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        if (json) {
            NSString* key = json[@"key"];
            if (key) {
                RemoveLocalStorageKey(key);
            }
        }
        return;
    }
    else if ([msg containsString:@"\"action\":\"store_clear\""]) {
        ClearLocalStorage();
        return;
    }
    else if ([msg containsString:@"\"action\":\"console_log\""]) {
        NSData* data = [msg dataUsingEncoding:NSUTF8StringEncoding];
        NSDictionary* json = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        if (json) {
            NSString* level = json[@"level"];
            NSString* text = json[@"text"];
            LogToFile([NSString stringWithFormat:@"[Console:%@] %@", level, text]);
        }
        return;
    }
    
    LogToFile([NSString stringWithFormat:@"[Script Message] Received event: %@", msg]);
    
    // Check for ready action to send payload (immediate invisible execution)
    if ([msg containsString:@"\"action\":\"ready_invisible\""]) {
        if (mainWebView) {
            LogToFile(@"[Script Message] Bridge ready. Delivering request payload...");
            NSData* data = [requestJsonData dataUsingEncoding:NSUTF8StringEncoding];
            NSString* base64String = [data base64EncodedStringWithOptions:0];
            NSString* jsCode = [NSString stringWithFormat:@"window.chrome.webview._deliverMessageBase64('%@')", base64String];
            [mainWebView evaluateJavaScript:jsCode completionHandler:^(id result, NSError *error) {
                if (error) {
                    LogToFile([NSString stringWithFormat:@"[Script Message] ERROR delivering message: %@", [error localizedDescription]]);
                }
            }];
        }
    }
    // Check for login window trigger (user needs to sign in)
    else if ([msg containsString:@"\"action\":\"show_login_window\""]) {
        if (mainWindow) {
            LogToFile(@"[Script Message] Authentication required. Displaying login window...");
            dispatch_async(dispatch_get_main_queue(), ^{
                [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
                [mainWindow center];
                [mainWindow makeKeyAndOrderFront:nil];
                [NSApp activateIgnoringOtherApps:YES];
            });
        }
    }
    // Check for successful login
    else if ([msg containsString:@"\"action\":\"login_success\""]) {
        LogToFile(@"[Script Message] Login successful. Delivering payload...");
        if (mainWindow) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [mainWindow setFrameOrigin:NSMakePoint(-10000, -10000)];
                [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
            });
        }
        if (mainWebView) {
            NSData* data = [requestJsonData dataUsingEncoding:NSUTF8StringEncoding];
            NSString* base64String = [data base64EncodedStringWithOptions:0];
            NSString* jsCode = [NSString stringWithFormat:@"window.chrome.webview._deliverMessageBase64('%@')", base64String];
            [mainWebView evaluateJavaScript:jsCode completionHandler:^(id result, NSError *error) {
                if (error) {
                    LogToFile([NSString stringWithFormat:@"[Script Message] ERROR delivering message: %@", [error localizedDescription]]);
                }
            }];
        }
    }
    // Check for success response
    else if ([msg containsString:@"\"action\":\"response\""]) {
        NSRange txtTagRange = [msg rangeOfString:@"\"text\":\""];
        if (txtTagRange.location != NSNotFound) {
            NSUInteger start = txtTagRange.location + 8;
            NSUInteger end = NSNotFound;
            NSUInteger backslashes = 0;
            for (NSUInteger i = start; i < [msg length]; ++i) {
                unichar c = [msg characterAtIndex:i];
                if (c == '\\') {
                    backslashes++;
                } else if (c == '"') {
                    if (backslashes % 2 == 0) {
                        end = i;
                        break;
                    }
                    backslashes = 0;
                } else {
                    backslashes = 0;
                }
            }
            if (end != NSNotFound && end > start) {
                NSString* escapedText = [msg substringWithRange:NSMakeRange(start, end - start)];
                
                // Simple unescape helper
                NSMutableString* unescaped = [NSMutableString string];
                for (NSUInteger i = 0; i < [escapedText length]; ++i) {
                    unichar c = [escapedText characterAtIndex:i];
                    if (c == '\\' && i + 1 < [escapedText length]) {
                        unichar next = [escapedText characterAtIndex:i + 1];
                        if (next == '\\') { [unescaped appendString:@"\\"]; i++; }
                        else if (next == '"') { [unescaped appendString:@"\""]; i++; }
                        else if (next == 'n') { [unescaped appendString:@"\n"]; i++; }
                        else if (next == 'r') { [unescaped appendString:@"\r"]; i++; }
                        else if (next == 't') { [unescaped appendString:@"\t"]; i++; }
                        else { [unescaped appendFormat:@"%C", c]; }
                    } else {
                        [unescaped appendFormat:@"%C", c];
                    }
                }
                SaveResponseAndExit(@"success", unescaped);
            } else {
                SaveResponseAndExit(@"success", @"");
            }
        } else {
            SaveResponseAndExit(@"success", @"");
        }
    }
    // Check for error response
    else if ([msg containsString:@"\"action\":\"error\""]) {
        NSRange txtTagRange = [msg rangeOfString:@"\"text\":\""];
        NSString* errText = @"Unknown Puter AI error";
        if (txtTagRange.location != NSNotFound) {
            NSUInteger start = txtTagRange.location + 8;
            NSUInteger end = NSNotFound;
            NSUInteger backslashes = 0;
            for (NSUInteger i = start; i < [msg length]; ++i) {
                unichar c = [msg characterAtIndex:i];
                if (c == '\\') {
                    backslashes++;
                } else if (c == '"') {
                    if (backslashes % 2 == 0) {
                        end = i;
                        break;
                    }
                    backslashes = 0;
                } else {
                    backslashes = 0;
                }
            }
            if (end != NSNotFound && end > start) {
                errText = [msg substringWithRange:NSMakeRange(start, end - start)];
            }
        }
        SaveResponseAndExit(@"error", errText);
    }
}

@end

@interface BridgeNavigationDelegate : NSObject <WKNavigationDelegate>
@end

@implementation BridgeNavigationDelegate

- (void)webView:(WKWebView *)webView didStartProvisionalNavigation:(WKNavigation *)navigation {
    LogToFile(@"[Navigation] didStartProvisionalNavigation");
}

- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation {
    LogToFile(@"[Navigation] didFinishNavigation: HTML loaded successfully!");
}

- (void)webView:(WKWebView *)webView didFailNavigation:(WKNavigation *)navigation withError:(NSError *)error {
    LogToFile([NSString stringWithFormat:@"[Navigation] ERROR didFailNavigation: %@", [error localizedDescription]]);
}

- (void)webView:(WKWebView *)webView didFailProvisionalNavigation:(WKNavigation *)navigation withError:(NSError *)error {
    LogToFile([NSString stringWithFormat:@"[Navigation] ERROR didFailProvisionalNavigation: %@", [error localizedDescription]]);
}

- (void)webViewWebContentProcessDidTerminate:(WKWebView *)webView {
    LogToFile(@"[Navigation] ERROR: Web content process terminated (crashed)!");
}

@end

@interface BridgeUIDelegate : NSObject <WKUIDelegate, NSWindowDelegate>
@end

@implementation BridgeUIDelegate

- (WKWebView *)webView:(WKWebView *)webView
createWebViewWithConfiguration:(WKWebViewConfiguration *)configuration
    forNavigationAction:(WKNavigationAction *)navigationAction
         windowFeatures:(WKWindowFeatures *)windowFeatures {
    
    LogToFile(@"[UI Delegate] Creating popup webview (window.open intercept)");
    
    // Create new popup window
    NSRect frame = NSMakeRect(0, 0, 500, 600);
    NSWindow* popupWindow = [[NSWindow alloc] initWithContentRect:frame
                                                        styleMask:(NSWindowStyleMaskTitled |
                                                                   NSWindowStyleMaskClosable |
                                                                   NSWindowStyleMaskResizable)
                                                          backing:NSBackingStoreBuffered
                                                            defer:NO];
    [popupWindow setTitle:@"Puter Authentication"];
    [popupWindow setDelegate:self];
    [popupWindow center];
    [popupWindow setReleasedWhenClosed:NO];
    
    // Create WKWebView inside the window
    WKWebView* popupWebView = [[WKWebView alloc] initWithFrame:frame configuration:configuration];
    popupWebView.UIDelegate = self;
    [popupWindow setContentView:popupWebView];
    
    [popupWindow makeKeyAndOrderFront:nil];
    
    // Retain the popup window in the active array to prevent ARC deallocation
    if (!activePopupWindows) {
        activePopupWindows = [[NSMutableArray alloc] init];
    }
    [activePopupWindows addObject:popupWindow];
    LogToFile([NSString stringWithFormat:@"[UI Delegate] Popup window created and retained. Total popups: %lu", (unsigned long)[activePopupWindows count]]);
    
    // Let the returned WKWebView load the URL automatically to preserve window connection and OAuth flow.
    if (navigationAction.request.URL) {
        LogToFile([NSString stringWithFormat:@"[UI Delegate] Popup requested URL: %@", [navigationAction.request.URL absoluteString]]);
    }
    
    return popupWebView;
}

- (void)webViewDidClose:(WKWebView *)webView {
    LogToFile(@"[UI Delegate] Closing popup webview");
    NSWindow* window = webView.window;
    if (window) {
        [window close];
    }
}

- (void)windowWillClose:(NSNotification *)notification {
    NSWindow* window = notification.object;
    LogToFile(@"[UI Delegate] Popup window WillClose callback triggered");
    if (activePopupWindows && [activePopupWindows containsObject:window]) {
        [activePopupWindows removeObject:window];
        LogToFile([NSString stringWithFormat:@"[UI Delegate] Removed closed window from active list. Remaining popups: %lu", (unsigned long)[activePopupWindows count]]);
    }
}

@end

// Custom WKURLSchemeHandler to load files using app://local/ instead of file://
// This bypasses Puter.js's "Unsupported Protocol: file://" security checks on macOS.
@interface BridgeURLSchemeHandler : NSObject <WKURLSchemeHandler>
@end

@implementation BridgeURLSchemeHandler

- (void)webView:(WKWebView *)webView startURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    NSURL *url = urlSchemeTask.request.URL;
    NSString *path = url.path;
    
    LogToFile([NSString stringWithFormat:@"[Scheme Handler] Request for path: %@", path]);
    
    // Resolve file path locally
    NSString* exeDir = [[NSBundle mainBundle] resourcePath];
    if (!exeDir) {
        exeDir = [[NSBundle mainBundle] bundlePath];
    }
    
    if ([path hasPrefix:@"/"]) {
        path = [path substringFromIndex:1];
    }
    
    // Remove "local/" prefix if requested as app://local/assets/...
    if ([path hasPrefix:@"local/"]) {
        path = [path substringFromIndex:6];
    }
    
    NSString* filePath = [exeDir stringByAppendingPathComponent:path];
    if (![[NSFileManager defaultManager] fileExistsAtPath:filePath]) {
        NSString* currentDir = [[NSFileManager defaultManager] currentDirectoryPath];
        filePath = [currentDir stringByAppendingPathComponent:path];
    }
    
    NSData *data = [NSData dataWithContentsOfFile:filePath];
    if (!data) {
        // Secondary fallback search
        NSString* currentDir = [[NSFileManager defaultManager] currentDirectoryPath];
        filePath = [currentDir stringByAppendingPathComponent:path];
        data = [NSData dataWithContentsOfFile:filePath];
    }
    
    if (data) {
        NSString *mimeType = @"text/html";
        if ([filePath hasSuffix:@".html"]) mimeType = @"text/html";
        else if ([filePath hasSuffix:@".js"]) mimeType = @"application/javascript";
        else if ([filePath hasSuffix:@".css"]) mimeType = @"text/css";
        else if ([filePath hasSuffix:@".png"]) mimeType = @"image/png";
        
        NSDictionary *headers = @{
            @"Access-Control-Allow-Origin": @"*",
            @"Access-Control-Allow-Headers": @"*",
            @"Access-Control-Allow-Methods": @"*",
            @"Cross-Origin-Opener-Policy": @"same-origin-allow-popups",
            @"Content-Type": [NSString stringWithFormat:@"%@; charset=utf-8", mimeType]
        };
        NSHTTPURLResponse *response = [[NSHTTPURLResponse alloc] initWithURL:url
                                                                  statusCode:200
                                                                 HTTPVersion:@"HTTP/1.1"
                                                                headerFields:headers];
        
        [urlSchemeTask didReceiveResponse:response];
        [urlSchemeTask didReceiveData:data];
        [urlSchemeTask didFinish];
        LogToFile([NSString stringWithFormat:@"[Scheme Handler] Successfully served file: %@", filePath]);
    } else {
        LogToFile([NSString stringWithFormat:@"[Scheme Handler] ERROR: File not found at path: %@", filePath]);
        NSError *error = [NSError errorWithDomain:NSURLErrorDomain
                                             code:NSURLErrorResourceUnavailable
                                         userInfo:nil];
        [urlSchemeTask didFailWithError:error];
    }
}

- (void)webView:(WKWebView *)webView stopURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    // No-op
}

@end

@interface BridgeAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
    BridgeScriptMessageHandler* messageHandler;
    BridgeUIDelegate* uiDelegate;
    BridgeNavigationDelegate* navigationDelegate;
    BridgeURLSchemeHandler* schemeHandler;
}
@end

@implementation BridgeAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    LogToFile(@"[App Delegate] applicationDidFinishLaunching started");
    
    // Disable App Nap for the process so WKWebView keeps running even when the window is offscreen
    if ([[NSProcessInfo processInfo] respondsToSelector:@selector(beginActivityWithOptions:reason:)]) {
        activityToken = [[NSProcessInfo processInfo] beginActivityWithOptions:NSActivityUserInitiated | NSActivityLatencyCritical reason:@"PuterBridge execution"];
        LogToFile(@"[App Delegate] App Nap disabled via beginActivityWithOptions");
    }
    
    // Load request data
    NSError* error = nil;
    NSString* content = [NSString stringWithContentsOfFile:@"puter_request.json" encoding:NSUTF8StringEncoding error:&error];
    if (error || !content) {
        LogToFile([NSString stringWithFormat:@"[App Delegate] ERROR reading puter_request.json: %@", [error localizedDescription]]);
        [NSApp terminate:nil];
        return;
    }
    requestJsonData = content;
    LogToFile(@"[App Delegate] Successfully read puter_request.json payload");
    
    // 1. Create Headless NSWindow positioned way off-screen using OffscreenWindow to prevent window server constraint correction
    NSRect frame = NSMakeRect(-10000, -10000, 600, 700);
    mainWindow = [[OffscreenWindow alloc] initWithContentRect:frame
                                                    styleMask:(NSWindowStyleMaskTitled |
                                                               NSWindowStyleMaskClosable |
                                                               NSWindowStyleMaskResizable)
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
    [mainWindow setTitle:@"Puter AI Authentication"];
    [mainWindow setDelegate:self];
    [mainWindow setReleasedWhenClosed:NO];
    
    // 2. Setup configuration & message handler & custom URL scheme handler
    WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
    messageHandler = [[BridgeScriptMessageHandler alloc] init];
    [configuration.userContentController addScriptMessageHandler:messageHandler name:@"puter"];
    [configuration.preferences setValue:@YES forKey:@"developerExtrasEnabled"];
    [configuration.preferences setValue:@YES forKey:@"allowFileAccessFromFileURLs"];
    configuration.preferences.javaScriptCanOpenWindowsAutomatically = YES;
    configuration.websiteDataStore = [WKWebsiteDataStore defaultDataStore];
    
    // Load persisted local storage
    NSDictionary* storedDb = ReadLocalStorageFile();
    NSMutableString* injectionJs = [NSMutableString string];
    [injectionJs appendString:@"(function() {\n"
                               "    const logToHost = (level, args) => {\n"
                               "        try {\n"
                               "            window.webkit.messageHandlers.puter.postMessage(JSON.stringify({\n"
                               "                action: 'console_log',\n"
                               "                level: level,\n"
                               "                text: args.map(x => {\n"
                               "                    if (x === null) return 'null';\n"
                               "                    if (x === undefined) return 'undefined';\n"
                               "                    if (typeof x === 'object') {\n"
                               "                        try { return JSON.stringify(x); } catch(e) { return String(x); }\n"
                               "                    }\n"
                               "                    return String(x);\n"
                               "                }).join(' ')\n"
                               "            }));\n"
                               "        } catch(e) {}\n"
                               "    };\n"
                               "    const _log = console.log;\n"
                               "    const _error = console.error;\n"
                               "    const _warn = console.warn;\n"
                               "    const _info = console.info;\n"
                               "    console.log = function(...args) { _log.apply(console, args); logToHost('log', args); };\n"
                               "    console.error = function(...args) { _error.apply(console, args); logToHost('error', args); };\n"
                               "    console.warn = function(...args) { _warn.apply(console, args); logToHost('warn', args); };\n"
                               "    console.info = function(...args) { _info.apply(console, args); logToHost('info', args); };\n"
                               "    const store = {};\n"];
    
    NSMutableDictionary* dbDict = [storedDb mutableCopy];
    NSString* authToken = nil;
    if (dbDict) {
        if (dbDict[@"auth_token_v2"]) {
            authToken = dbDict[@"auth_token_v2"];
        } else if (dbDict[@"puter.auth.token.v2"]) {
            authToken = dbDict[@"puter.auth.token.v2"];
        }
    }
    
    if (authToken && [authToken isKindOfClass:[NSString class]]) {
        dbDict[@"auth_token_v2"] = authToken;
        dbDict[@"puter.auth.token.v2"] = authToken;
        
        NSString* escapedToken = EscapeJavaScriptString(authToken);
        [injectionJs appendFormat:@"    window.auth_token = '%@';\n", escapedToken];
        LogToFile(@"[Injection] Set window.auth_token and duplicated storage tokens");
    }
    
    if (dbDict) {
        for (NSString* key in dbDict) {
            NSString* val = dbDict[key];
            if ([val isKindOfClass:[NSString class]]) {
                NSString* escapedVal = EscapeJavaScriptString(val);
                [injectionJs appendFormat:@"    store['%@'] = '%@';\n", key, escapedVal];
                LogToFile([NSString stringWithFormat:@"[Injection] Injected localStorage key: %@", key]);
            }
        }
    }
    
    [injectionJs appendString:@"    const mockLocalStorage = {\n"
                               "        getItem: function(key) {\n"
                               "            return store.hasOwnProperty(key) ? store[key] : null;\n"
                               "        },\n"
                               "        setItem: function(key, value) {\n"
                               "            const valString = String(value);\n"
                               "            store[key] = valString;\n"
                               "            window.webkit.messageHandlers.puter.postMessage(JSON.stringify({\n"
                               "                action: 'store_setItem',\n"
                               "                key: key,\n"
                               "                value: valString\n"
                               "            }));\n"
                               "        },\n"
                               "        removeItem: function(key) {\n"
                               "            delete store[key];\n"
                               "            window.webkit.messageHandlers.puter.postMessage(JSON.stringify({\n"
                               "                action: 'store_removeItem',\n"
                               "                key: key\n"
                               "            }));\n"
                               "        },\n"
                               "        clear: function() {\n"
                               "            for (const key in store) {\n"
                               "                if (store.hasOwnProperty(key)) {\n"
                               "                    delete store[key];\n"
                               "                }\n"
                               "            }\n"
                               "            window.webkit.messageHandlers.puter.postMessage(JSON.stringify({\n"
                               "                action: 'store_clear'\n"
                               "            }));\n"
                               "        },\n"
                               "        key: function(index) {\n"
                               "            const keys = Object.keys(store);\n"
                               "            return index < keys.length ? keys[index] : null;\n"
                               "        }\n"
                               "    };\n"
                               "    const storageProxy = new Proxy(mockLocalStorage, {\n"
                               "        get: function(target, prop) {\n"
                               "            if (prop in target) return target[prop];\n"
                               "            if (typeof prop === 'string') {\n"
                               "                return store.hasOwnProperty(prop) ? store[prop] : undefined;\n"
                               "            }\n"
                               "            return undefined;\n"
                               "        },\n"
                               "        set: function(target, prop, value) {\n"
                               "            if (typeof prop === 'string') {\n"
                               "                target.setItem(prop, value);\n"
                               "                return true;\n"
                               "            }\n"
                               "            return false;\n"
                               "        },\n"
                               "        deleteProperty: function(target, prop) {\n"
                               "            if (typeof prop === 'string') {\n"
                               "                target.removeItem(prop);\n"
                               "                return true;\n"
                               "            }\n"
                               "            return false;\n"
                               "        },\n"
                               "        has: function(target, prop) {\n"
                               "            if (prop in target) return true;\n"
                               "            if (typeof prop === 'string') {\n"
                               "                return store.hasOwnProperty(prop);\n"
                               "            }\n"
                               "            return false;\n"
                               "        },\n"
                               "        ownKeys: function(target) {\n"
                               "            return Object.keys(store);\n"
                               "        },\n"
                               "        getOwnPropertyDescriptor: function(target, prop) {\n"
                               "            if (typeof prop === 'string' && store.hasOwnProperty(prop)) {\n"
                               "                return {\n"
                               "                    value: store[prop],\n"
                               "                    writable: true,\n"
                               "                    enumerable: true,\n"
                               "                    configurable: true\n"
                               "                };\n"
                               "            }\n"
                               "            return undefined;\n"
                               "        }\n"
                               "    });\n"
                               "    Object.defineProperty(storageProxy, 'length', {\n"
                               "        get: function() { return Object.keys(store).length; },\n"
                               "        configurable: true,\n"
                               "        enumerable: false\n"
                               "    });\n"
                               "    try {\n"
                               "        Object.defineProperty(window, 'localStorage', {\n"
                               "            value: storageProxy,\n"
                               "            configurable: true,\n"
                               "            enumerable: true,\n"
                               "            writable: false\n"
                               "        });\n"
                               "    } catch (e) {\n"
                               "        console.error('Failed to mock localStorage:', e);\n"
                               "    }\n"
                               "    const sessionStore = {};\n"
                               "    const mockSessionStorage = {\n"
                               "        getItem: function(key) {\n"
                               "            return sessionStore.hasOwnProperty(key) ? sessionStore[key] : null;\n"
                               "        },\n"
                               "        setItem: function(key, value) {\n"
                               "            sessionStore[key] = String(value);\n"
                               "        },\n"
                               "        removeItem: function(key) {\n"
                               "            delete sessionStore[key];\n"
                               "        },\n"
                               "        clear: function() {\n"
                               "            for (const key in sessionStore) {\n"
                               "                delete sessionStore[key];\n"
                               "            }\n"
                               "        },\n"
                               "        key: function(index) {\n"
                               "            const keys = Object.keys(sessionStore);\n"
                               "            return index < keys.length ? keys[index] : null;\n"
                               "        }\n"
                               "    };\n"
                               "    const sessionStorageProxy = new Proxy(mockSessionStorage, {\n"
                               "        get: function(target, prop) {\n"
                               "            if (prop in target) return target[prop];\n"
                               "            if (typeof prop === 'string') {\n"
                               "                return sessionStore.hasOwnProperty(prop) ? sessionStore[prop] : undefined;\n"
                               "            }\n"
                               "            return undefined;\n"
                               "        },\n"
                               "        set: function(target, prop, value) {\n"
                               "            if (typeof prop === 'string') {\n"
                               "                target.setItem(prop, value);\n"
                               "                return true;\n"
                               "            }\n"
                               "            return false;\n"
                               "        },\n"
                               "        deleteProperty: function(target, prop) {\n"
                               "            if (typeof prop === 'string') {\n"
                               "                target.removeItem(prop);\n"
                               "                return true;\n"
                               "            }\n"
                               "            return false;\n"
                               "        },\n"
                               "        has: function(target, prop) {\n"
                               "            if (prop in target) return true;\n"
                               "            if (typeof prop === 'string') {\n"
                               "                return sessionStore.hasOwnProperty(prop);\n"
                               "            }\n"
                               "            return false;\n"
                               "        },\n"
                               "        ownKeys: function(target) {\n"
                               "            return Object.keys(sessionStore);\n"
                               "        },\n"
                               "        getOwnPropertyDescriptor: function(target, prop) {\n"
                               "            if (typeof prop === 'string' && sessionStore.hasOwnProperty(prop)) {\n"
                               "                return {\n"
                               "                    value: sessionStore[prop],\n"
                               "                    writable: true,\n"
                               "                    enumerable: true,\n"
                               "                    configurable: true\n"
                               "                };\n"
                               "            }\n"
                               "            return undefined;\n"
                               "        }\n"
                               "    });\n"
                               "    Object.defineProperty(sessionStorageProxy, 'length', {\n"
                               "        get: function() { return Object.keys(sessionStore).length; },\n"
                               "        configurable: true,\n"
                               "        enumerable: false\n"
                               "    });\n"
                               "    try {\n"
                               "        Object.defineProperty(window, 'sessionStorage', {\n"
                               "            value: sessionStorageProxy,\n"
                               "            configurable: true,\n"
                               "            enumerable: true,\n"
                               "            writable: false\n"
                               "        });\n"
                               "    } catch (e) {\n"
                               "        console.error('Failed to mock sessionStorage:', e);\n"
                               "    }\n"
                               "})();\n"];
    
    WKUserScript* userScript = [[WKUserScript alloc] initWithSource:injectionJs
                                                      injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                    forMainFrameOnly:NO];
    [configuration.userContentController addUserScript:userScript];
    
    schemeHandler = [[BridgeURLSchemeHandler alloc] init];
    [configuration setURLSchemeHandler:schemeHandler forURLScheme:@"app"];
    
    // 3. Initialize WebView
    mainWebView = [[WKWebView alloc] initWithFrame:frame configuration:configuration];
    uiDelegate = [[BridgeUIDelegate alloc] init];
    navigationDelegate = [[BridgeNavigationDelegate alloc] init];
    
    mainWebView.UIDelegate = uiDelegate;
    mainWebView.navigationDelegate = navigationDelegate;
    [mainWindow setContentView:mainWebView];
    
    // 4. Load puter_bridge.html content from file and load it using loadHTMLString:baseURL:
    NSString* exeDir = [[NSBundle mainBundle] resourcePath];
    if (!exeDir) {
        exeDir = [[NSBundle mainBundle] bundlePath];
    }
    NSString* filePath = [exeDir stringByAppendingPathComponent:@"assets/puter_bridge.html"];
    if (![[NSFileManager defaultManager] fileExistsAtPath:filePath]) {
        NSString* currentDir = [[NSFileManager defaultManager] currentDirectoryPath];
        filePath = [currentDir stringByAppendingPathComponent:@"assets/puter_bridge.html"];
    }
    
    NSError* fileLoadError = nil;
    NSString* htmlContent = [NSString stringWithContentsOfFile:filePath encoding:NSUTF8StringEncoding error:&fileLoadError];
    if (fileLoadError || !htmlContent) {
        LogToFile([NSString stringWithFormat:@"[App Delegate] ERROR reading puter_bridge.html, searching fallback: %@", [fileLoadError localizedDescription]]);
        NSString* currentDir = [[NSFileManager defaultManager] currentDirectoryPath];
        filePath = [currentDir stringByAppendingPathComponent:@"assets/puter_bridge.html"];
        htmlContent = [NSString stringWithContentsOfFile:filePath encoding:NSUTF8StringEncoding error:nil];
    }
    
    if (htmlContent) {
        NSURL *baseURL = [NSURL URLWithString:@"https://puter.com/"];
        LogToFile([NSString stringWithFormat:@"[App Delegate] Loading HTML string with baseURL: %@", [baseURL absoluteString]]);
        [mainWebView loadHTMLString:htmlContent baseURL:baseURL];
    } else {
        LogToFile(@"[App Delegate] CRITICAL ERROR: Could not find or read assets/puter_bridge.html!");
        [NSApp terminate:nil];
    }
    
    // Crucial: order the window front so it activates the WKWebView execution loop immediately
    [mainWindow orderFront:nil];
    LogToFile(@"[App Delegate] Window ordered front off-screen");
    
    // 5. Add 180s fallback timeout
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(180 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (!operationCompleted) {
            LogToFile(@"[App Delegate] Timeout reached after 180 seconds.");
            SaveResponseAndExit(@"error", @"Puter bridge request timed out after 180 seconds.");
        }
    });
}

- (void)windowWillClose:(NSNotification *)notification {
    LogToFile(@"[App Delegate] Main window WillClose callback triggered");
    if (!operationCompleted) {
        SaveResponseAndExit(@"error", @"Puter bridge window closed by user.");
    }
}

@end

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        // Register 'app' scheme as secure and CORS-enabled before any WebView initializes
        if ([WKProcessPool respondsToSelector:@selector(_registerURLSchemeAsSecure:)]) {
            [WKProcessPool _registerURLSchemeAsSecure:@"app"];
        }
        if ([WKProcessPool respondsToSelector:@selector(_registerURLSchemeAsCORSEnabled:)]) {
            [WKProcessPool _registerURLSchemeAsCORSEnabled:@"app"];
        }
        if ([WKProcessPool respondsToSelector:@selector(_registerURLSchemeAsBypassingContentSecurityPolicy:)]) {
            [WKProcessPool _registerURLSchemeAsBypassingContentSecurityPolicy:@"app"];
        }

        // Clear old log
        [[NSFileManager defaultManager] removeItemAtPath:@"puter_bridge_log.txt" error:nil];
        LogToFile(@"=== Puter Bridge execution started ===");
        
        NSApplication* app = [NSApplication sharedApplication];
        LogToFile(@"[Main] NSApplication initialized");
        
        // Crucial: Set activation policy to Accessory initially to hide Dock icon, and elevate only when auth is needed.
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
        LogToFile(@"[Main] Activation policy set to Accessory");
        
        appDelegate = [[BridgeAppDelegate alloc] init];
        [app setDelegate:appDelegate];
        LogToFile(@"[Main] Delegate set, running application...");
        [app run];
    }
    return 0;
}
