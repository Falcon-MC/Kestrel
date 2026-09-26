#include "platform/Window.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

@interface KestrelWindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic) bool open;
@property (nonatomic) bool resized;
@end

@implementation KestrelWindowDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender
{
    self.open = false;
    return YES;
}

- (void)windowDidResize:(NSNotification*)notification
{
    self.resized = true;
}

- (void)windowDidChangeBackingProperties:(NSNotification*)notification
{
    self.resized = true;
}

@end

namespace kestrel {

namespace {

class CocoaWindow final : public Window {
public:
    CocoaWindow(const std::string& title, uint32_t w, uint32_t h)
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];

        NSRect frame = NSMakeRect(0, 0, w, h);
        NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable | NSWindowStyleMaskFullSizeContentView;
        window = [[NSWindow alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO];
        window.title = [NSString stringWithUTF8String:title.c_str()];
        window.titlebarAppearsTransparent = YES;
        window.titleVisibility = NSWindowTitleHidden;
        window.backgroundColor = [NSColor colorWithSRGBRed:15 / 255.0 green:13 / 255.0 blue:19 / 255.0 alpha:1.0];
        window.acceptsMouseMovedEvents = YES;
        window.contentMinSize = NSMakeSize(720, 480);
        [window center];

        delegate = [KestrelWindowDelegate new];
        delegate.open = true;
        window.delegate = delegate;

        NSView* view = window.contentView;
        view.wantsLayer = YES;
        CAMetalLayer* layer = [CAMetalLayer layer];
        layer.contentsScale = window.backingScaleFactor;
        view.layer = layer;

        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    }

    bool pump() override
    {
        state.beginFrame();
        @autoreleasepool {
            NSEvent* event;
            while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:nil inMode:NSDefaultRunLoopMode dequeue:YES])) {
                if (event.type == NSEventTypeKeyDown && event.window == window) {
                    heldKey(event.keyCode, true);
                    key(event);
                    continue;
                }
                if (event.type == NSEventTypeKeyUp && event.window == window) {
                    heldKey(event.keyCode, false);
                    continue;
                }
                if (event.type == NSEventTypeFlagsChanged) {
                    state.setKey(Key::Shift, (event.modifierFlags & NSEventModifierFlagShift) != 0);
                    state.setKey(Key::Control, (event.modifierFlags & NSEventModifierFlagControl) != 0);
                    state.setKey(Key::Alt, (event.modifierFlags & NSEventModifierFlagOption) != 0);
                }
                if (captured && (event.type == NSEventTypeMouseMoved || event.type == NSEventTypeLeftMouseDragged || event.type == NSEventTypeRightMouseDragged)) {
                    CGFloat scale = window.backingScaleFactor;
                    state.mouseDeltaX += static_cast<float>(event.deltaX * scale);
                    state.mouseDeltaY += static_cast<float>(event.deltaY * scale);
                }
                switch (event.type) {
                case NSEventTypeLeftMouseDown:
                    mouse(event);
                    if (event.window == window && inDragRegion()) {
                        [window performWindowDragWithEvent:event];
                        continue;
                    }
                    state.mouseDown = true;
                    state.mousePressed = true;
                    break;
                case NSEventTypeLeftMouseUp:
                    mouse(event);
                    state.mouseDown = false;
                    state.mouseReleased = true;
                    break;
                case NSEventTypeMouseMoved:
                case NSEventTypeLeftMouseDragged:
                    mouse(event);
                    break;
                case NSEventTypeScrollWheel:
                    state.wheel += static_cast<float>(event.scrollingDeltaY) / (event.hasPreciseScrollingDeltas ? 40.0f : 1.0f);
                    break;
                default:
                    break;
                }
                [NSApp sendEvent:event];
            }
        }
        return delegate.open;
    }

    void* nativeHandle() const override
    {
        return (__bridge void*)window.contentView.layer;
    }

    uint32_t width() const override
    {
        return static_cast<uint32_t>(window.contentView.bounds.size.width * window.backingScaleFactor);
    }

    uint32_t height() const override
    {
        return static_cast<uint32_t>(window.contentView.bounds.size.height * window.backingScaleFactor);
    }

    float contentScale() const override
    {
        return static_cast<float>(window.backingScaleFactor);
    }

    bool consumeResize() override
    {
        bool value = delegate.resized;
        delegate.resized = false;
        if (value) {
            window.contentView.layer.contentsScale = window.backingScaleFactor;
        }
        return value;
    }

    InputState& input() override
    {
        return state;
    }

    void setChrome(WindowChrome value) override
    {
        chrome = std::move(value);
    }

    void setCursor(Cursor value) override
    {
        if (value == cursor) {
            return;
        }
        cursor = value;
        if (cursor == Cursor::Hand) {
            [[NSCursor pointingHandCursor] set];
        } else if (cursor == Cursor::Text) {
            [[NSCursor IBeamCursor] set];
        } else {
            [[NSCursor arrowCursor] set];
        }
    }

    void setMouseCaptured(bool value) override
    {
        if (value == captured) {
            return;
        }
        captured = value;
        CGAssociateMouseAndMouseCursorPosition(captured ? false : true);
        if (captured) {
            [NSCursor hide];
        } else {
            [NSCursor unhide];
        }
    }

    bool drawsCaptionButtons() const override
    {
        return false;
    }

    float captionInsetLeft() const override
    {
        return 78.0f;
    }

    bool maximized() const override
    {
        return window.zoomed;
    }

    void minimize() override
    {
        [window miniaturize:nil];
    }

    void toggleMaximize() override
    {
        [window zoom:nil];
    }

    void close() override
    {
        delegate.open = false;
    }

private:
    bool inDragRegion() const
    {
        if (state.mouseY < 0.0f || state.mouseY >= chrome.captionHeight) {
            return false;
        }
        for (const ui::Rect& rect : chrome.interactive) {
            if (rect.contains(state.mouseX, state.mouseY)) {
                return false;
            }
        }
        return true;
    }

    void mouse(NSEvent* event)
    {
        if (event.window != window) {
            return;
        }
        NSView* view = window.contentView;
        NSPoint point = [view convertPoint:event.locationInWindow fromView:nil];
        CGFloat scale = window.backingScaleFactor;
        state.mouseX = static_cast<float>(point.x * scale);
        state.mouseY = static_cast<float>((view.bounds.size.height - point.y) * scale);
    }

    static Key translateKey(unsigned short code)
    {
        static const std::pair<unsigned short, Key> table[] = {
            { 0, Key::A }, { 11, Key::B }, { 8, Key::C }, { 2, Key::D }, { 14, Key::E }, { 3, Key::F }, { 5, Key::G },
            { 4, Key::H }, { 34, Key::I }, { 38, Key::J }, { 40, Key::K }, { 37, Key::L }, { 46, Key::M }, { 45, Key::N },
            { 31, Key::O }, { 35, Key::P }, { 12, Key::Q }, { 15, Key::R }, { 1, Key::S }, { 17, Key::T }, { 32, Key::U },
            { 9, Key::V }, { 13, Key::W }, { 7, Key::X }, { 16, Key::Y }, { 6, Key::Z },
            { 29, Key::Num0 }, { 18, Key::Num1 }, { 19, Key::Num2 }, { 20, Key::Num3 }, { 21, Key::Num4 },
            { 23, Key::Num5 }, { 22, Key::Num6 }, { 26, Key::Num7 }, { 28, Key::Num8 }, { 25, Key::Num9 },
            { 49, Key::Space }, { 48, Key::Tab }, { 36, Key::Enter }, { 51, Key::Backspace }, { 53, Key::Escape },
            { 126, Key::Up }, { 125, Key::Down }, { 123, Key::Left }, { 124, Key::Right },
            { 122, Key::F1 }, { 120, Key::F2 }, { 99, Key::F3 }, { 118, Key::F4 }, { 96, Key::F5 }, { 97, Key::F6 },
            { 98, Key::F7 }, { 100, Key::F8 }, { 101, Key::F9 }, { 109, Key::F10 }, { 103, Key::F11 }, { 111, Key::F12 },
        };
        for (const auto& [native, key] : table) {
            if (native == code) {
                return key;
            }
        }
        return Key::None;
    }

    void heldKey(unsigned short code, bool down)
    {
        state.setKey(translateKey(code), down);
    }

    void key(NSEvent* event)
    {
        if ((event.modifierFlags & NSEventModifierFlagCommand) && [event.charactersIgnoringModifiers isEqualToString:@"q"]) {
            delegate.open = false;
            return;
        }
        switch (event.keyCode) {
        case 51:
            state.backspace = true;
            return;
        case 36:
        case 76:
            state.enter = true;
            return;
        case 53:
            state.escape = true;
            return;
        case 48:
            state.tab = true;
            return;
        default:
            break;
        }

        NSString* characters = event.characters;
        NSUInteger length = characters.length;
        for (NSUInteger i = 0; i < length; ++i) {
            unichar c = [characters characterAtIndex:i];
            char32_t cp = c;
            if (c >= 0xD800 && c < 0xDC00 && i + 1 < length) {
                unichar low = [characters characterAtIndex:i + 1];
                cp = 0x10000 + ((static_cast<char32_t>(c) - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
            if (cp >= 32 && cp != 127 && (cp < 0xF700 || cp > 0xF8FF)) {
                state.text.push_back(cp);
            }
        }
    }

    NSWindow* window;
    KestrelWindowDelegate* delegate;
    InputState state;
    WindowChrome chrome;
    Cursor cursor = Cursor::Arrow;
    bool captured = false;
};

}

std::unique_ptr<Window> Window::create(const std::string& title, uint32_t width, uint32_t height)
{
    return std::make_unique<CocoaWindow>(title, width, height);
}

}
