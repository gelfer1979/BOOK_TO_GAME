#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#include <fstream>
#include <iostream>
#include <string>

// Global state
static NSWindow* mainWindow = nil;
static WKWebView* mainWebView = nil;
static NSString* requestJsonData = nil;
static BOOL operationCompleted = NO;

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
    [NSApp terminate:nil];
}

@interface BridgeScriptMessageHandler : NSObject <WKScriptMessageHandler>
@end

@implementation BridgeScriptMessageHandler

- (void)userContentController:(WKUserContentController *)userContentController
      didReceiveScriptMessage:(WKScriptMessage *)message {
    if (![message.name isEqualToString:@"puter"]) return;
    
    NSString* msg = message.body;
    if (![msg isKindOfClass:[NSString class]]) return;
    
    LogToFile([NSString stringWithFormat:@"[Script Message] Received event: %@", msg]);
    
    // Check for ready action to send payload (immediate invisible execution)
    if ([msg containsString:@"\"action\":\"ready_invisible\""]) {
        if (mainWebView) {
            LogToFile(@"[Script Message] Bridge ready. Delivering request payload...");
            NSString* jsCode = [NSString stringWithFormat:@"window.chrome.webview._deliverMessage('%@')", EscapeJavaScriptString(requestJsonData)];
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
                [mainWindow center];
                [mainWindow makeKeyAndOrderFront:nil];
                [NSApp activateIgnoringOtherApps:YES];
            });
        }
    }
    // Check for successful login
    else if ([msg containsString:@"\"action\":\"login_success\""]) {
        LogToFile(@"[Script Message] Login successful. Hiding window and delivering payload...");
        if (mainWindow) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [mainWindow setFrameOrigin:NSMakePoint(-10000, -10000)];
            });
        }
        if (mainWebView) {
            NSString* jsCode = [NSString stringWithFormat:@"window.chrome.webview._deliverMessage('%@')", EscapeJavaScriptString(requestJsonData)];
            [mainWebView evaluateJavaScript:jsCode completionHandler:nil];
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
    
    // Create WKWebView inside the window
    WKWebView* popupWebView = [[WKWebView alloc] initWithFrame:frame configuration:configuration];
    popupWebView.UIDelegate = self;
    [popupWindow setContentView:popupWebView];
    
    [popupWindow makeKeyAndOrderFront:nil];
    
    // Load the navigation URL
    if (navigationAction.request.URL) {
        LogToFile([NSString stringWithFormat:@"[UI Delegate] Popup loading URL: %@", [navigationAction.request.URL absoluteString]]);
        [popupWebView loadRequest:navigationAction.request];
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

@end

@interface BridgeAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
    BridgeScriptMessageHandler* messageHandler;
    BridgeUIDelegate* uiDelegate;
    BridgeNavigationDelegate* navigationDelegate;
}
@end

@implementation BridgeAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    LogToFile(@"[App Delegate] applicationDidFinishLaunching started");
    
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
    
    // 1. Create Headless NSWindow positioned way off-screen
    NSRect frame = NSMakeRect(-10000, -10000, 600, 700);
    mainWindow = [[NSWindow alloc] initWithContentRect:frame
                                             styleMask:(NSWindowStyleMaskTitled |
                                                        NSWindowStyleMaskClosable |
                                                        NSWindowStyleMaskResizable)
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
    [mainWindow setTitle:@"Puter AI Authentication"];
    [mainWindow setDelegate:self];
    
    // 2. Setup configuration & message handler
    WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
    messageHandler = [[BridgeScriptMessageHandler alloc] init];
    [configuration.userContentController addScriptMessageHandler:messageHandler name:@"puter"];
    [configuration.preferences setValue:@YES forKey:@"developerExtrasEnabled"];
    configuration.websiteDataStore = [WKWebsiteDataStore defaultDataStore];
    [configuration.preferences setValue:@YES forKey:@"allowFileAccessFromFileURLs"];
    
    // 3. Initialize WebView
    mainWebView = [[WKWebView alloc] initWithFrame:frame configuration:configuration];
    uiDelegate = [[BridgeUIDelegate alloc] init];
    navigationDelegate = [[BridgeNavigationDelegate alloc] init];
    
    mainWebView.UIDelegate = uiDelegate;
    mainWebView.navigationDelegate = navigationDelegate;
    [mainWindow setContentView:mainWebView];
    
    // 4. Load puter_bridge.html
    NSString* exeDir = [[NSBundle mainBundle] resourcePath];
    if (!exeDir) {
        exeDir = [[NSBundle mainBundle] bundlePath];
    }
    NSString* htmlPath = [exeDir stringByAppendingPathComponent:@"assets/puter_bridge.html"];
    if (![[NSFileManager defaultManager] fileExistsAtPath:htmlPath]) {
        NSString* currentDir = [[NSFileManager defaultManager] currentDirectoryPath];
        htmlPath = [currentDir stringByAppendingPathComponent:@"assets/puter_bridge.html"];
    }
    
    // Verify existence of puter_bridge.html
    if (![[NSFileManager defaultManager] fileExistsAtPath:htmlPath]) {
        NSString* currentDir = [[NSFileManager defaultManager] currentDirectoryPath];
        htmlPath = [currentDir stringByAppendingPathComponent:@"assets/puter_bridge.html"];
    }
    
    LogToFile([NSString stringWithFormat:@"[App Delegate] Resolved HTML file path: %@", htmlPath]);
    
    if (![[NSFileManager defaultManager] fileExistsAtPath:htmlPath]) {
        LogToFile(@"[App Delegate] ERROR: HTML file does not exist at path!");
        SaveResponseAndExit(@"error", @"Puter bridge assets not found");
        return;
    }
    
    NSURL* fileURL = [NSURL fileURLWithPath:htmlPath];
    NSURL* readAccessURL = [[fileURL URLByDeletingLastPathComponent] URLByDeletingLastPathComponent]; // Access to parent dir containing assets/
    
    LogToFile([NSString stringWithFormat:@"[App Delegate] Loading fileURL: %@ (access to: %@)", [fileURL absoluteString], [readAccessURL absoluteString]]);
    [mainWebView loadFileURL:fileURL allowingReadAccessToURL:readAccessURL];
    
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
    LogToFile(@"[App Delegate] Main window closed by user");
    if (!operationCompleted) {
        SaveResponseAndExit(@"error", @"Puter bridge window closed by user.");
    }
}

@end

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        // Clear old log
        [[NSFileManager defaultManager] removeItemAtPath:@"puter_bridge_log.txt" error:nil];
        LogToFile(@"=== Puter Bridge execution started ===");
        
        NSApplication* app = [NSApplication sharedApplication];
        LogToFile(@"[Main] NSApplication initialized");
        
        // Crucial: Set activation policy to Regular so that Cocoa windows can be presented and focused
        // when launched as a child process via exec.
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        LogToFile(@"[Main] Activation policy set to Regular");
        
        BridgeAppDelegate* delegate = [[BridgeAppDelegate alloc] init];
        [app setDelegate:delegate];
        LogToFile(@"[Main] Delegate set, running application...");
        [app run];
    }
    return 0;
}
