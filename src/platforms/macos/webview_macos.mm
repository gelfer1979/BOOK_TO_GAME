#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

// AppDelegate class wrapper to track window events
@interface WebViewAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
    NSWindow* window;
    WKWebView* webView;
}
@end

@implementation WebViewAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    // 1. Setup rect window bounds
    NSRect frame = NSMakeRect(0, 0, 1280, 720);
    
    // 2. Initialize NSWindow
    window = [[NSWindow alloc] initWithContentRect:frame
                                         styleMask:(NSWindowStyleMaskTitled |
                                                    NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskMiniaturizable |
                                                    NSWindowStyleMaskResizable)
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    
    [window setTitle:@"BOOK_TO_GAME Native Client (macOS)"];
    [window center];
    [window setDelegate:self];
    
    // 3. Configure WKWebView configurations (Enable local access & JS cookies)
    WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
    [configuration.preferences setValue:@YES forKey:@"developerExtrasEnabled"]; // Allows Inspect Element
    
    // WKWebsiteDataStore allows Puter session retention across launches
    configuration.websiteDataStore = [WKWebsiteDataStore defaultDataStore];
    
    // Allow reading from local file paths securely
    [configuration.preferences setValue:@YES forKey:@"allowFileAccessFromFileURLs"];
    
    // 4. Initialize WKWebView
    webView = [[WKWebView alloc] initWithFrame:frame configuration:configuration];
    [window setContentView:webView];
    
    // Load local served WebAssembly HTML game client in macOS Bundle or relative path
    NSString* exeDir = [[NSBundle mainBundle] resourcePath];
    if (!exeDir) {
        exeDir = [[NSBundle mainBundle] bundlePath];
    }
    NSString* htmlPath = [exeDir stringByAppendingPathComponent:@"BOOK_TO_GAME.html"];
    
    // Check if it exists in bundle, otherwise fall back to current directory
    if (![[NSFileManager defaultManager] fileExistsAtPath:htmlPath]) {
        NSString* currentDir = [[NSFileManager defaultManager] currentDirectoryPath];
        htmlPath = [currentDir stringByAppendingPathComponent:@"BOOK_TO_GAME.html"];
    }
    
    NSURL* fileURL = [NSURL fileURLWithPath:htmlPath];
    NSURL* readAccessURL = [fileURL URLByDeletingLastPathComponent];
    [webView loadFileURL:fileURL allowingReadAccessToURL:readAccessURL];
    
    // 6. Present window
    [window makeKeyAndOrderFront:nil];
}

- (void)windowWillClose:(NSNotification *)notification {
    [NSApp terminate:nil];
}

@end

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        WebViewAppDelegate* delegate = [[WebViewAppDelegate alloc] init];
        [app setDelegate:delegate];
        [app run];
    }
    return 0;
}
