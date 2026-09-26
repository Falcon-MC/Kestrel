#include "platform/Shell.h"

#import <Cocoa/Cocoa.h>

namespace kestrel::platform {

void openUrl(const std::string& url)
{
    NSURL* target = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
    if (target) {
        [[NSWorkspace sharedWorkspace] openURL:target];
    }
}

bool copyText(const std::string& text)
{
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    return [pasteboard setString:[NSString stringWithUTF8String:text.c_str()] forType:NSPasteboardTypeString];
}

}
