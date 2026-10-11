#include "platform/Shell.h"
#include "../mobile/Resources.h"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <filesystem>

@interface KestrelFilePicker : NSObject <UIDocumentPickerDelegate>
@property(nonatomic, assign) kestrel::platform::FileKind kind;
@end

@implementation KestrelFilePicker
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
    NSURL* chosen = urls.firstObject;
    if (!chosen) return;
    std::filesystem::path destination = kestrel::platform::importedFile(self.kind);
    std::error_code error;
    std::filesystem::copy_file(chosen.fileSystemRepresentation, destination, std::filesystem::copy_options::overwrite_existing, error);
    if (!error) kestrel::platform::deliverPickedFile(self.kind, destination.string());
}
@end

namespace kestrel::platform {

namespace {
KestrelFilePicker* activePicker;
}

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

void showFilePicker(FileKind kind)
{
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    if (kind == FileKind::Png) {
        [types addObject:UTTypePNG];
    } else {
        if (UTType* pack = [UTType typeWithFilenameExtension:@"mcpack"]) [types addObject:pack];
        [types addObject:UTTypeZIP];
    }
    activePicker = [KestrelFilePicker new];
    activePicker.kind = kind;
    dispatch_async(dispatch_get_main_queue(), ^{
        UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:types asCopy:YES];
        picker.delegate = activePicker;
        [UIApplication.sharedApplication.delegate.window.rootViewController presentViewController:picker animated:YES completion:nil];
    });
}

}
