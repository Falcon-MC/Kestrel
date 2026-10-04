#include "platform/Shell.h"
#import <UIKit/UIKit.h>

namespace kestrel::platform {

void openUrl(const std::string& url)
{
    NSURL* target = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
    if (!target.scheme.length) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        [UIApplication.sharedApplication openURL:target options:@{} completionHandler:nil];
    });
}

bool copyText(const std::string& text)
{
    UIPasteboard.generalPasteboard.string = [NSString stringWithUTF8String:text.c_str()];
    return UIPasteboard.generalPasteboard.string != nil;
}

std::string pasteText()
{
    NSString* text = UIPasteboard.generalPasteboard.string;
    return text ? std::string(text.UTF8String) : std::string();
}

std::string pickPngFile() { return {}; }
std::string pickResourcePackFile() { return {}; }

}
