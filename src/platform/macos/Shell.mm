#include "platform/Shell.h"

#import <Cocoa/Cocoa.h>

namespace kestrel::platform {

void openUrl(const std::string& url)
{
    NSString* value = [NSString stringWithUTF8String:url.c_str()];
    if (!value) {
        return;
    }

    NSURL* target = [NSURL URLWithString:value];
    if (!target || !target.scheme.length) {
        target = [NSURL fileURLWithPath:value];
    }
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

std::string pasteText()
{
    NSString* text = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    return text ? std::string([text UTF8String]) : std::string();
}

std::string pickPngFile()
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.allowedFileTypes = @[ @"png" ];
    panel.allowsMultipleSelection = NO;
    if ([panel runModal] != NSModalResponseOK || panel.URLs.count == 0) {
        return {};
    }
    return std::string([panel.URLs.firstObject.path UTF8String]);
}

std::string pickResourcePackFile()
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.allowedFileTypes = @[ @"mcpack", @"zip" ];
    panel.allowsMultipleSelection = NO;
    if ([panel runModal] != NSModalResponseOK || panel.URLs.count == 0) return {};
    return std::string([panel.URLs.firstObject.path UTF8String]);
}

}
