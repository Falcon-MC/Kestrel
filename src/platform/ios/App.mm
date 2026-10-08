#include "client/Client.h"
#include "client/LaunchOptions.h"
#include "../mobile/Resources.h"

#import <UIKit/UIKit.h>
#import <QuartzCore/CADisplayLink.h>
#import <AVFAudio/AVAudioSession.h>

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>

@interface KestrelViewController : UIViewController
@end
@implementation KestrelViewController
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }
@end

@interface KestrelAppDelegate : UIResponder <UIApplicationDelegate> {
    std::unique_ptr<kestrel::Client> client;
}
@property(nonatomic, strong) UIWindow* window;
@property(nonatomic, strong) CADisplayLink* displayLink;
@end

@implementation KestrelAppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options
{
    NSString* resources = NSBundle.mainBundle.resourcePath;
    NSString* certificates = [resources stringByAppendingPathComponent:@"cert.pem"];
    auto vanilla = kestrel::platform::mobileResources() / "resource_packs/vanilla";
    setenv("KESTREL_VANILLA_PACK", vanilla.c_str(), 1);
    setenv("KESTREL_FONTS", [resources stringByAppendingPathComponent:@"fonts"].fileSystemRepresentation, 1);
    setenv("SSL_CERT_FILE", certificates.fileSystemRepresentation, 1);
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [KestrelViewController new];
    self.window.rootViewController.view.backgroundColor = UIColor.blackColor;
    [self.window makeKeyAndVisible];
    application.idleTimerDisabled = YES;
    dispatch_async(dispatch_get_main_queue(), ^{
        [self prepareResources];
    });
    return YES;
}

- (void)prepareResources
{
    if (kestrel::platform::mobileResourcesReady()) {
        try {
            kestrel::platform::refreshTouchControls(NSBundle.mainBundle.resourcePath.fileSystemRepresentation);
            [self startClient];
        } catch (const std::exception& error) {
            [self showFailure:[NSString stringWithUTF8String:error.what()]];
        }
        return;
    }
    UILabel* status = [[UILabel alloc] initWithFrame:self.window.rootViewController.view.bounds];
    status.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    status.text = @"Downloading Minecraft resources from Mojang…";
    status.textColor = UIColor.whiteColor;
    status.textAlignment = NSTextAlignmentCenter;
    status.numberOfLines = 0;
    [self.window.rootViewController.view addSubview:status];
    CGRect bounds = self.window.rootViewController.view.bounds;
    UIProgressView* bar = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
    bar.frame = CGRectMake(bounds.size.width * 0.2, bounds.size.height / 2 + 30, bounds.size.width * 0.6, 4);
    bar.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleTopMargin | UIViewAutoresizingFlexibleBottomMargin;
    bar.progressTintColor = UIColor.whiteColor;
    bar.trackTintColor = [UIColor colorWithWhite:1 alpha:0.25];
    [self.window.rootViewController.view addSubview:bar];
    NSURL* url = [NSURL URLWithString:@(kestrel::platform::MobileResourceUrl)];
    NSURLSessionDownloadTask* task = [NSURLSession.sharedSession downloadTaskWithURL:url completionHandler:^(NSURL* location, NSURLResponse* response, NSError* failure) {
        NSString* message = failure.localizedDescription;
        if (!failure && [(NSHTTPURLResponse*)response statusCode] != 200) message = @"Mojang's resource download failed. Restart Kestrel to try again.";
        if (!message) {
            dispatch_async(dispatch_get_main_queue(), ^{
                status.text = @"Installing Minecraft resources…";
                bar.progress = 1;
            });
            try {
                std::ifstream file(location.fileSystemRepresentation, std::ios::binary);
                if (!file) throw std::runtime_error("Cannot read downloaded Minecraft resources");
                std::string archive { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
                kestrel::platform::installMobileResources(std::move(archive), NSBundle.mainBundle.resourcePath.fileSystemRepresentation);
            } catch (const std::exception& error) {
                message = [NSString stringWithUTF8String:error.what()];
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [status removeFromSuperview];
            [bar removeFromSuperview];
            if (message) [self showFailure:message];
            else [self startClient];
        });
    }];
    __weak NSURLSessionDownloadTask* watched = task;
    [NSTimer scheduledTimerWithTimeInterval:0.1 repeats:YES block:^(NSTimer* timer) {
        NSURLSessionDownloadTask* current = watched;
        if (!current || !bar.superview || current.state != NSURLSessionTaskStateRunning) {
            [timer invalidate];
            return;
        }
        [bar setProgress:float(current.progress.fractionCompleted) animated:YES];
    }];
    [task resume];
}

- (void)showFailure:(NSString*)message
{
    [self.displayLink invalidate];
    self.displayLink = nil;
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Kestrel" message:message preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self.window.rootViewController presentViewController:alert animated:YES completion:nil];
}

- (void)startClient
{
    try {
        std::vector<std::string> arguments;
        NSArray<NSString*>* nativeArguments = NSProcessInfo.processInfo.arguments;
        for (NSUInteger i = 1; i < nativeArguments.count; ++i) arguments.emplace_back(nativeArguments[i].UTF8String);
        kestrel::LaunchOptions options = kestrel::LaunchOptions::parse(arguments);
        options.hidden = false;
        options.headless = false;
        client = std::make_unique<kestrel::Client>(std::move(options));
        self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(drawFrame:)];
        self.displayLink.preferredFramesPerSecond = 60;
        self.displayLink.paused = UIApplication.sharedApplication.applicationState != UIApplicationStateActive;
        [self.displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    } catch (const std::exception& error) {
        [self showFailure:[NSString stringWithUTF8String:error.what()]];
    }
}

- (void)drawFrame:(CADisplayLink*)link
{
    @autoreleasepool {
        try {
            if (client && !client->frame(false)) {
                [self.displayLink invalidate];
                self.displayLink = nil;
                client.reset();
            }
        } catch (const std::exception& error) {
            [self showFailure:[NSString stringWithUTF8String:error.what()]];
        }
    }
}

- (void)applicationWillResignActive:(UIApplication*)application
{
    self.displayLink.paused = YES;
    [AVAudioSession.sharedInstance setActive:NO error:nil];
}

- (void)applicationDidBecomeActive:(UIApplication*)application
{
    [AVAudioSession.sharedInstance setCategory:AVAudioSessionCategoryAmbient error:nil];
    [AVAudioSession.sharedInstance setActive:YES error:nil];
    self.displayLink.paused = NO;
}

- (void)applicationWillTerminate:(UIApplication*)application
{
    [self.displayLink invalidate];
    self.displayLink = nil;
    client.reset();
}
@end

int main(int argc, char** argv)
{
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(KestrelAppDelegate.class));
    }
}
