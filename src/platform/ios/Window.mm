#include "platform/Window.h"
#include "ui/Utf8.h"

#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <GameController/GameController.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
kestrel::Key keyForCode(UIKeyboardHIDUsage code)
{
    using namespace kestrel;
    if (code >= UIKeyboardHIDUsageKeyboardA && code <= UIKeyboardHIDUsageKeyboardZ)
        return letterKey(code - UIKeyboardHIDUsageKeyboardA);
    if (code >= UIKeyboardHIDUsageKeyboard1 && code <= UIKeyboardHIDUsageKeyboard9)
        return digitKey(code - UIKeyboardHIDUsageKeyboard1 + 1);
    if (code >= UIKeyboardHIDUsageKeyboardF1 && code <= UIKeyboardHIDUsageKeyboardF12)
        return functionKey(code - UIKeyboardHIDUsageKeyboardF1);
    switch (code) {
    case UIKeyboardHIDUsageKeyboard0: return Key::Num0;
    case UIKeyboardHIDUsageKeyboardSpacebar: return Key::Space;
    case UIKeyboardHIDUsageKeyboardLeftShift:
    case UIKeyboardHIDUsageKeyboardRightShift: return Key::Shift;
    case UIKeyboardHIDUsageKeyboardLeftControl:
    case UIKeyboardHIDUsageKeyboardRightControl: return Key::Control;
    case UIKeyboardHIDUsageKeyboardLeftAlt:
    case UIKeyboardHIDUsageKeyboardRightAlt: return Key::Alt;
    case UIKeyboardHIDUsageKeyboardTab: return Key::Tab;
    case UIKeyboardHIDUsageKeyboardReturnOrEnter: return Key::Enter;
    case UIKeyboardHIDUsageKeyboardDeleteOrBackspace: return Key::Backspace;
    case UIKeyboardHIDUsageKeyboardEscape: return Key::Escape;
    case UIKeyboardHIDUsageKeyboardUpArrow: return Key::Up;
    case UIKeyboardHIDUsageKeyboardDownArrow: return Key::Down;
    case UIKeyboardHIDUsageKeyboardLeftArrow: return Key::Left;
    case UIKeyboardHIDUsageKeyboardRightArrow: return Key::Right;
    default: return Key::None;
    }
}
}

@interface KestrelMetalView : UIView <UIKeyInput>
@property(nonatomic, assign) kestrel::InputState* pending;
@property(nonatomic, assign) BOOL captured;
@property(nonatomic, assign) BOOL textEditing;
@property(nonatomic, strong) UIView* emptyKeyboard;
@property(nonatomic, strong) UITouch* pointerTouch;
@end

@implementation KestrelMetalView
+ (Class)layerClass { return CAMetalLayer.class; }
- (BOOL)canBecomeFirstResponder { return YES; }
- (UIView*)inputView { return self.textEditing ? nil : self.emptyKeyboard; }
- (BOOL)hasText { return YES; }
- (void)insertText:(NSString*)text
{
    if (!self.pending) return;
    std::string typed = text.UTF8String ?: "";
    for (size_t i = 0; i < typed.size();) {
        char32_t cp = kestrel::ui::nextCodepoint(typed, i);
        if (cp == U'\n') self.pending->enter = true;
        else if (cp >= 32) self.pending->text.push_back(cp);
    }
}
- (void)deleteBackward { if (self.pending) self.pending->backspace = true; }
- (void)readTouches:(NSSet<UITouch*>*)touches ended:(BOOL)ended cancelled:(BOOL)cancelled
{
    if (!self.pending) return;
    for (UITouch* touch in touches) {
        if (touch.type != UITouchTypeDirect && touch.type != UITouchTypePencil) continue;
        uint64_t id = reinterpret_cast<uintptr_t>((__bridge void*)touch);
        CGPoint point = [touch locationInView:self];
        auto& points = self.pending->touches;
        auto found = std::find_if(points.begin(), points.end(), [id](const auto& value) { return value.id == id; });
        bool began = found == points.end();
        if (began) {
            if (ended) continue;
            points.push_back({ id, float(point.x * self.contentScaleFactor), float(point.y * self.contentScaleFactor), 0, 0, true, true });
            found = points.end() - 1;
        }
        float x = point.x * self.contentScaleFactor, y = point.y * self.contentScaleFactor;
        found->dx += x - found->x;
        found->dy += y - found->y;
        found->x = x; found->y = y;
        found->down = !ended;
        found->released = ended;
        found->cancelled = cancelled;
        if (!self.captured && !self.pointerTouch && began) self.pointerTouch = touch;
        if (!self.captured && self.pointerTouch == touch) {
            self.pending->mouseX = x;
            self.pending->mouseY = y;
            self.pending->mousePressed |= began;
            self.pending->mouseReleased |= ended;
            self.pending->mouseDown = !ended;
            if (!began && !ended && std::abs(found->dy) > 2 * self.contentScaleFactor)
                self.pending->wheel += found->dy / (32 * self.contentScaleFactor);
        }
        if (ended && self.pointerTouch == touch) self.pointerTouch = nil;
    }
}
- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self readTouches:touches ended:NO cancelled:NO]; }
- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self readTouches:touches ended:NO cancelled:NO]; }
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self readTouches:touches ended:YES cancelled:NO]; }
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self readTouches:touches ended:YES cancelled:YES]; }
- (void)layoutSubviews
{
    [super layoutSubviews];
    self.contentScaleFactor = self.window.screen.scale;
    CAMetalLayer* metal = (CAMetalLayer*)self.layer;
    metal.contentsScale = self.contentScaleFactor;
    metal.drawableSize = CGSizeMake(self.bounds.size.width * self.contentScaleFactor, self.bounds.size.height * self.contentScaleFactor);
}
- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    if (!self.pending) return;
    for (UIPress* press in presses) {
        UIKey* key = press.key;
        if (!key) continue;
        kestrel::Key code = keyForCode(key.keyCode);
        self.pending->setKey(code, true);
        self.pending->backspace |= code == kestrel::Key::Backspace;
        self.pending->enter |= code == kestrel::Key::Enter;
        self.pending->escape |= code == kestrel::Key::Escape;
        self.pending->tab |= code == kestrel::Key::Tab;
        if (!(key.modifierFlags & (UIKeyModifierControl | UIKeyModifierCommand))) {
            std::string typed = key.characters.UTF8String ?: "";
            for (size_t i = 0; i < typed.size();) {
                char32_t cp = kestrel::ui::nextCodepoint(typed, i);
                if (cp >= 32 && cp != 127) self.pending->text.push_back(cp);
            }
        }
    }
}
- (void)pressesEnded:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    if (!self.pending) return;
    for (UIPress* press in presses) if (press.key) self.pending->setKey(keyForCode(press.key.keyCode), false);
}
- (void)pressesCancelled:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    [self pressesEnded:presses withEvent:event];
}
- (void)pointerMoved:(UIHoverGestureRecognizer*)gesture
{
    if (!self.pending || self.captured) return;
    CGPoint point = [gesture locationInView:self];
    self.pending->mouseX = point.x * self.contentScaleFactor;
    self.pending->mouseY = point.y * self.contentScaleFactor;
}
@end

namespace kestrel {
namespace {

class UIKitWindow final : public Window {
public:
    UIKitWindow()
    {
        UIWindow* host = UIApplication.sharedApplication.delegate.window;
        view = [[KestrelMetalView alloc] initWithFrame:host.bounds];
        view.pending = &pending;
        view.multipleTouchEnabled = YES;
        view.emptyKeyboard = [[UIView alloc] initWithFrame:CGRectZero];
        view.backgroundColor = UIColor.blackColor;
        view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        [view addGestureRecognizer:[[UIHoverGestureRecognizer alloc] initWithTarget:view action:@selector(pointerMoved:)]];
        [host.rootViewController.view addSubview:view];
        [view becomeFirstResponder];
        [view layoutIfNeeded];
        __weak KestrelMetalView* weakView = view;
        focusObserver = [NSNotificationCenter.defaultCenter addObserverForName:UIApplicationWillResignActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
            KestrelMetalView* target = weakView;
            if (!target.pending) return;
            target.pending->releaseKeys();
            target.pending->mouseDown = false;
            target.pending->rightMouseDown = false;
            for (auto& touch : target.pending->touches) { touch.down = false; touch.released = touch.cancelled = true; }
            target.pointerTouch = nil;
            focusLost = true;
        }];
        mouseObserver = [NSNotificationCenter.defaultCenter addObserverForName:GCMouseDidConnectNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification* event) {
            attachMouse((GCMouse*)event.object);
        }];
        if (GCMouse.current) attachMouse(GCMouse.current);
        keyboardObserver = [NSNotificationCenter.defaultCenter addObserverForName:UIKeyboardWillChangeFrameNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification* event) {
            KestrelMetalView* target = weakView;
            if (!target) return;
            CGRect keyboard = [target.superview convertRect:[event.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue] fromView:nil];
            CGRect frame = target.superview.bounds;
            if (target.textEditing && CGRectIntersectsRect(frame, keyboard)) frame.size.height = std::max<CGFloat>(1, CGRectGetMinY(keyboard));
            target.frame = frame;
            [target setNeedsLayout];
        }];
    }

    ~UIKitWindow() override
    {
        [NSNotificationCenter.defaultCenter removeObserver:focusObserver];
        [NSNotificationCenter.defaultCenter removeObserver:mouseObserver];
        [NSNotificationCenter.defaultCenter removeObserver:keyboardObserver];
        view.pending = nullptr;
        mouse.mouseInput.mouseMovedHandler = nil;
        mouse.mouseInput.leftButton.pressedChangedHandler = nil;
        mouse.mouseInput.rightButton.pressedChangedHandler = nil;
        mouse.mouseInput.middleButton.pressedChangedHandler = nil;
        mouse.mouseInput.scroll.valueChangedHandler = nil;
    }

    bool pump() override
    {
        auto gamepad = state.gamepad;
        state = pending;
        state.gamepad = gamepad;
        pending.beginFrame();
        uint32_t w = width(), h = height();
        resized |= w != previousWidth || h != previousHeight;
        previousWidth = w;
        previousHeight = h;
        return open;
    }
    void* nativeHandle() const override { return (__bridge void*)view.layer; }
    uint32_t width() const override { return std::max(1u, uint32_t(std::lround(view.bounds.size.width * contentScale()))); }
    uint32_t height() const override { return std::max(1u, uint32_t(std::lround(view.bounds.size.height * contentScale()))); }
    float contentScale() const override { return view.window.screen.scale ?: 1.0f; }
    ui::Rect safeArea() const override
    {
        UIEdgeInsets edges = view.safeAreaInsets;
        float scale = contentScale();
        return { float(edges.left) * scale, float(edges.top) * scale,
            float(view.bounds.size.width - edges.left - edges.right) * scale,
            float(view.bounds.size.height - edges.top - edges.bottom) * scale };
    }
    bool consumeResize() override { return std::exchange(resized, false); }
    bool consumeFocusLost() override { return std::exchange(focusLost, false); }
    InputState& input() override { return state; }
    void setChrome(WindowChrome) override { }
    void setCursor(Cursor) override { }
    void setMouseCaptured(bool captured) override
    {
        if (view.captured == captured) return;
        view.captured = captured;
        pending.mouseDown = false;
        pending.mouseReleased = true;
        view.pointerTouch = nil;
    }
    void setTextInput(bool enabled) override
    {
        if (view.textEditing == enabled) return;
        view.textEditing = enabled;
        [view reloadInputViews];
        if (!enabled) view.frame = view.superview.bounds;
    }
    bool drawsCaptionButtons() const override { return false; }
    float captionInsetLeft() const override { return 0.0f; }
    bool maximized() const override { return true; }
    void minimize() override { }
    void toggleMaximize() override { }
    bool fullscreen() const override { return true; }
    void toggleFullscreen() override { }
    void close() override { open = false; }
    bool visible() const override { return true; }

private:
    void attachMouse(GCMouse* connected)
    {
        mouse = connected;
        mouse.handlerQueue = dispatch_get_main_queue();
        __weak KestrelMetalView* weakView = view;
        mouse.mouseInput.mouseMovedHandler = ^(GCMouseInput*, float dx, float dy) {
            KestrelMetalView* target = weakView;
            if (!target.pending || !target.captured) return;
            target.pending->mouseDeltaX += dx;
            target.pending->mouseDeltaY -= dy;
        };
        mouse.mouseInput.leftButton.pressedChangedHandler = ^(GCControllerButtonInput*, float, BOOL down) {
            KestrelMetalView* target = weakView;
            if (!target.pending) return;
            target.pending->mousePressed |= down && !target.pending->mouseDown;
            target.pending->mouseReleased |= !down && target.pending->mouseDown;
            target.pending->mouseDown = down;
        };
        mouse.mouseInput.rightButton.pressedChangedHandler = ^(GCControllerButtonInput*, float, BOOL down) {
            KestrelMetalView* target = weakView;
            if (!target.pending) return;
            target.pending->rightMousePressed |= down && !target.pending->rightMouseDown;
            target.pending->rightMouseReleased |= !down && target.pending->rightMouseDown;
            target.pending->rightMouseDown = down;
        };
        mouse.mouseInput.middleButton.pressedChangedHandler = ^(GCControllerButtonInput*, float, BOOL down) {
            KestrelMetalView* target = weakView;
            if (!target.pending) return;
            target.pending->middleMousePressed |= down;
            target.pending->middleMouseReleased |= !down;
        };
        mouse.mouseInput.scroll.valueChangedHandler = ^(GCControllerDirectionPad*, float, float y) {
            KestrelMetalView* target = weakView;
            if (target.pending) target.pending->wheel += y;
        };
    }

    KestrelMetalView* view;
    GCMouse* mouse;
    id focusObserver;
    id mouseObserver;
    id keyboardObserver;
    InputState state;
    InputState pending;
    uint32_t previousWidth = 0, previousHeight = 0;
    bool resized = true, focusLost = false, open = true;
};
}

std::unique_ptr<Window> Window::create(const std::string&, uint32_t, uint32_t, bool)
{
    return std::make_unique<UIKitWindow>();
}
}
