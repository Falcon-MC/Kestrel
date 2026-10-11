#include "client/Client.h"
#include "client/LaunchOptions.h"
#include "client/PrepareAssets.h"
#include "../mobile/Resources.h"

#import <UIKit/UIKit.h>
#import <QuartzCore/CADisplayLink.h>
#import <AVFAudio/AVAudioSession.h>

#include <cstdlib>
#include <exception>
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
    std::unique_ptr<kestrel::AssetPreparation> preparation;
}
@property(nonatomic, strong) UIWindow* window;
@property(nonatomic, strong) CADisplayLink* displayLink;
@end

@implementation KestrelAppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options
{
    NSString* resources = NSBundle.mainBundle.resourcePath;
    NSString* certificates = [resources stringByAppendingPathComponent:@"cert.pem"];
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
    try {
        preparation = std::make_unique<kestrel::AssetPreparation>();
        self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(drawFrame:)];
        self.displayLink.preferredFramesPerSecond = 60;
        self.displayLink.paused = UIApplication.sharedApplication.applicationState != UIApplicationStateActive;
        [self.displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    } catch (const std::exception& error) {
        [self showFailure:[NSString stringWithUTF8String:error.what()]];
    }
}

- (void)showFailure:(NSString*)message
{
    [self.displayLink invalidate];
    self.displayLink = nil;
    preparation.reset();
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Kestrel" message:message preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Try again" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { [self prepareResources]; }]];
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
            if (preparation) {
                auto state = preparation->frame();
                if (state == kestrel::AssetPreparation::State::Working) return;
                preparation.reset();
                [self.displayLink invalidate];
                self.displayLink = nil;
                if (state == kestrel::AssetPreparation::State::Cancelled) {
                    [self showFailure:@"Setup was cancelled. Retry when you're ready to download the resources."];
                    return;
                }
                kestrel::platform::refreshTouchControls(NSBundle.mainBundle.resourcePath.fileSystemRepresentation);
                [self startClient];
                return;
            }
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
    preparation.reset();
    client.reset();
}
@end

int main(int argc, char** argv)
{
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(KestrelAppDelegate.class));
    }
}
