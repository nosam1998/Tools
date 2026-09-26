#include "TapAudio.h"
#include "OutputSelection.h"
#import <Cocoa/Cocoa.h>
#include <cmath>
#include <memory>

@interface DriftDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
    drift::Controls controls;
    std::unique_ptr<TapAudio> audio;
    std::vector<MacDevice> devices;
    NSWindow *window;
    NSStatusItem *statusItem;
    NSPopUpButton *output;
    NSPopUpButton *preset;
    NSSlider *depth, *pace, *volume;
    NSTextField *depthLabel, *paceLabel, *volumeLabel, *status;
    NSButton *start, *stop, *bypass, *refresh;
    NSTimer *timer;
    AudioObjectID observedDefault;
    bool routingRequested;
}
@end
@implementation DriftDelegate
- (NSTextField *)label:(NSString *)text y:(CGFloat)y {
    NSTextField *label = [NSTextField wrappingLabelWithString:text];
    label.frame = NSMakeRect(24, 600 - y - 42, 552, 42);
    [window.contentView addSubview:label];
    return label;
}
- (NSButton *)button:(NSString *)title
                   x:(CGFloat)x
                   y:(CGFloat)y
               width:(CGFloat)width
              action:(SEL)action {
    NSButton *button = [NSButton buttonWithTitle:title target:self action:action];
    button.frame = NSMakeRect(x, 600 - y - 34, width, 34);
    [window.contentView addSubview:button];
    return button;
}
- (NSSlider *)slider:(double)value minimum:(double)min maximum:(double)max y:(CGFloat)y {
    NSSlider *slider = [NSSlider sliderWithValue:value
                                        minValue:min
                                        maxValue:max
                                          target:self
                                          action:@selector(change:)];
    slider.continuous = YES;
    slider.frame = NSMakeRect(24, 600 - y - 28, 552, 28);
    [window.contentView addSubview:slider];
    return slider;
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    observedDefault = 0;
    routingRequested = false;
    audio = std::make_unique<TapAudio>(controls);
    NSUserDefaults *prefs = NSUserDefaults.standardUserDefaults;
    [prefs registerDefaults:@{
        @"depth" : @0.8,
        @"seconds" : @6,
        @"volume" : @0.6,
        @"enabled" : @YES,
        @"pulse" : @NO
    }];
    drift::Parameters p{[prefs floatForKey:@"depth"], [prefs floatForKey:@"seconds"],
                        [prefs floatForKey:@"volume"], bool([prefs boolForKey:@"enabled"]),
                        bool([prefs boolForKey:@"pulse"])};
    controls.set(p);
    p = controls.read();
    window =
        [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 600, 600)
                                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                              NSWindowStyleMaskMiniaturizable
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
    window.title = @"Drift EQ — Calm chaos";
    window.delegate = self;
    window.releasedWhenClosed = NO;
    [window center];
    NSTextField *title = [self label:@"Drift EQ" y:20];
    title.font = [NSFont systemFontOfSize:28 weight:NSFontWeightSemibold];
    [self label:@"Subtle, moving tone for the sound you already enjoy." y:66];
    [self label:@"Play through these speakers or headphones" y:112];
    output = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(24, 600 - 144 - 32, 552, 32)
                                        pullsDown:NO];
    output.target = self;
    output.action = @selector(save);
    [window.contentView addSubview:output];
    refresh = [self button:@"Refresh devices"
                         x:24
                         y:184
                     width:160
                    action:@selector(refreshDevices)];
    preset = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(24, 600 - 236 - 32, 270, 32)
                                        pullsDown:NO];
    [preset addItemsWithTitles:@[ @"Subtle", @"Wander", @"Soft pulse" ]];
    [preset selectItem:nil];
    preset.target = self;
    preset.action = @selector(choosePreset);
    [window.contentView addSubview:preset];
    depthLabel = [self label:@"" y:284];
    depth = [self slider:p.depth minimum:0 maximum:2 y:314];
    paceLabel = [self label:@"" y:352];
    pace = [self slider:p.seconds minimum:0.5 maximum:16 y:382];
    volumeLabel = [self label:@"" y:420];
    volume = [self slider:p.volume minimum:0 maximum:1 y:450];
    start = [self button:@"Start" x:24 y:492 width:110 action:@selector(startAudio)];
    stop = [self button:@"Stop routing" x:144 y:492 width:130 action:@selector(stopAudio)];
    bypass = [self button:@"Bypass EQ" x:284 y:492 width:135 action:@selector(toggleBypass)];
    [self button:@"Quit" x:434 y:492 width:142 action:@selector(quit)];
    status = [self label:@"Ready. Start to request permission and process system audio." y:542];
    status.font = [NSFont systemFontOfSize:12];
    [depth setAccessibilityLabel:@"Maximum EQ movement in decibels"];
    [pace setAccessibilityLabel:@"Motion timing in seconds"];
    [volume setAccessibilityLabel:@"Output level"];
    statusItem = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
    statusItem.button.title = @"Drift";
    NSMenu *menu = [[NSMenu alloc] init];
    for (NSArray *item in @[
             @[ @"Open Drift EQ", @"show" ], @[ @"Toggle EQ bypass", @"toggleBypass" ],
             @[ @"Stop and restore audio", @"stopAudio" ], @[ @"Quit", @"quit" ]
         ]) {
        NSMenuItem *entry = [[NSMenuItem alloc] initWithTitle:item[0]
                                                       action:NSSelectorFromString(item[1])
                                                keyEquivalent:@""];
        entry.target = self;
        [menu addItem:entry];
    }
    statusItem.menu = menu;
    NSMenu *main = [[NSMenu alloc] init];
    NSMenuItem *appItem = [[NSMenuItem alloc] init];
    [main addItem:appItem];
    NSMenu *appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:@"Quit Drift EQ" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    NSApp.mainMenu = main;
    [self labels];
    [self refreshDevices];
    [self show];
    timer = [NSTimer scheduledTimerWithTimeInterval:1
                                             target:self
                                           selector:@selector(tick)
                                           userInfo:nil
                                            repeats:YES];
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(sleep:)
                                               name:NSApplicationWillTerminateNotification
                                             object:nil];
    [NSWorkspace.sharedWorkspace.notificationCenter addObserver:self
                                                       selector:@selector(sleep:)
                                                           name:NSWorkspaceWillSleepNotification
                                                         object:nil];
}
- (void)show {
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}
- (BOOL)windowShouldClose:(NSWindow *)sender {
    [sender orderOut:nil];
    return NO;
}
- (void)labels {
    const auto p = controls.read();
    depthLabel.stringValue = [NSString stringWithFormat:@"Maximum movement: ±%.1f dB", p.depth];
    paceLabel.stringValue =
        [NSString stringWithFormat:p.pulse ? @"Time per tonal pulse: %.1f seconds"
                                           : @"Typical time between targets: %.1f seconds",
                                   p.seconds];
    volumeLabel.stringValue = [NSString stringWithFormat:@"Output level: %.0f%%", p.volume * 100];
    bypass.title = p.enabled ? @"Bypass EQ" : @"Enable EQ";
}
- (void)change:(id)sender {
    (void)sender;
    controls.depth = float(depth.doubleValue);
    controls.seconds = float(pace.doubleValue);
    controls.volume = float(volume.doubleValue);
    [preset selectItem:nil];
    [self labels];
    [self save];
}
- (void)choosePreset {
    const auto old = controls.read();
    auto p = drift::preset(unsigned(preset.indexOfSelectedItem));
    p.volume = old.volume;
    p.enabled = old.enabled;
    controls.set(p);
    depth.doubleValue = p.depth;
    pace.doubleValue = p.seconds;
    [self labels];
    [self save];
}
- (void)toggleBypass {
    controls.enabled = !controls.enabled.load();
    [self labels];
    [self save];
}
- (void)save {
    const auto p = controls.read();
    NSUserDefaults *prefs = NSUserDefaults.standardUserDefaults;
    [prefs setFloat:p.depth forKey:@"depth"];
    [prefs setFloat:p.seconds forKey:@"seconds"];
    [prefs setFloat:p.volume forKey:@"volume"];
    [prefs setBool:p.enabled forKey:@"enabled"];
    [prefs setBool:p.pulse forKey:@"pulse"];
    const auto i = output.indexOfSelectedItem;
    if (i == 0)
        [prefs setObject:@"" forKey:@"outputUID"];
    else if (i > 0 && std::size_t(i - 1) < devices.size())
        [prefs setObject:[NSString stringWithUTF8String:devices[i - 1].uid.c_str()]
                  forKey:@"outputUID"];
}
- (void)updateDefaultLabel {
    const auto id = macDefaultOutput();
    const auto current = macOutputs();
    const auto *device = drift::findOutput(current, id);
    NSString *name = device ? [NSString stringWithUTF8String:device->name.c_str()] : @"unavailable";
    if (output.numberOfItems)
        [output itemAtIndex:0].title = [NSString stringWithFormat:@"System default — %@", name];
}
- (void)refreshDevices {
    if (routingRequested)
        return;
    devices = macOutputs();
    [output removeAllItems];
    [output addItemWithTitle:@"System default"];
    NSInteger selected = 0;
    NSString *saved = [NSUserDefaults.standardUserDefaults stringForKey:@"outputUID"];
    for (std::size_t i = 0; i < devices.size(); ++i) {
        [output addItemWithTitle:[NSString stringWithUTF8String:devices[i].name.c_str()]];
        if ([[NSString stringWithUTF8String:devices[i].uid.c_str()] isEqualToString:saved])
            selected = NSInteger(i + 1);
    }
    // Preserve an explicit, disconnected choice instead of moving audio elsewhere.
    if (saved.length && selected == 0) {
        devices.push_back({0, saved.UTF8String, "Saved device (unavailable)"});
        [output addItemWithTitle:@"Saved device (unavailable)"];
        selected = NSInteger(devices.size());
    }
    [output selectItemAtIndex:selected];
    [self updateDefaultLabel];
    start.enabled = YES;
    stop.enabled = NO;
}
- (void)openOutput:(AudioObjectID)device {
    std::string error;
    if (!audio->start(device, error)) {
        routingRequested = false;
        status.stringValue = [NSString
            stringWithFormat:@"Could not start: %s Normal audio restored.", error.c_str()];
    }
}
- (void)startAudio {
    const auto i = output.indexOfSelectedItem;
    if (i < 0 || (i > 0 && std::size_t(i - 1) >= devices.size()))
        return;
    [self save];
    observedDefault = macDefaultOutput();
    const auto available = macOutputs();
    AudioObjectID chosen = 0;
    if (i > 0)
        for (const auto &device : available)
            if (device.uid == devices[i - 1].uid)
                chosen = device.id; // Core Audio IDs can change after reconnecting.
    const auto *device = drift::resolveOutput(available, chosen, observedDefault);
    if (!device || (i > 0 && !chosen)) {
        status.stringValue =
            @"Output unavailable. Choose a working default in Sound settings or refresh devices.";
        return;
    }
    routingRequested = true;
    [self openOutput:device->id];
    [self tick];
}
- (void)stopAudio {
    routingRequested = false;
    if (audio)
        audio->stop();
    status.stringValue = @"Stopped. Normal app audio is restored.";
    start.enabled = YES;
    stop.enabled = NO;
    output.enabled = YES;
    refresh.enabled = YES;
}
- (void)sleep:(NSNotification *)notification {
    (void)notification;
    [self stopAudio];
}
- (void)tick {
    const auto currentDefault = macDefaultOutput();
    if (routingRequested && output.indexOfSelectedItem == 0 &&
        (currentDefault != observedDefault || !audio->running())) {
        observedDefault = currentDefault;
        const auto available = macOutputs();
        const auto *device = drift::resolveOutput(available, AudioObjectID(0), currentDefault);
        audio->stop(); // Release the old tap (and its mute) before starting a new route.
        if (device)
            [self openOutput:device->id];
        else
            status.stringValue =
                @"Waiting for a usable system default output. Normal app audio is restored.";
    }
    [self updateDefaultLabel];
    std::string reason;
    if (!audio->healthy(reason)) {
        routingRequested = false;
        status.stringValue = [NSString
            stringWithFormat:@"Stopped: %s Normal audio restored; refresh devices and start again.",
                             reason.c_str()];
    }
    const bool running = audio->running();
    start.enabled = !routingRequested;
    stop.enabled = routingRequested;
    output.enabled = !routingRequested;
    refresh.enabled = !routingRequested;
    if (running)
        status.stringValue = [NSString
            stringWithFormat:@"%@%@ · Buffer underruns: %llu · Overruns: %llu",
                             controls.enabled ? @"Drift is processing"
                                              : @"EQ bypassed; audio still routed",
                             output.indexOfSelectedItem == 0 ? @" · Following system default" : @"",
                             (unsigned long long)audio->underruns(),
                             (unsigned long long)audio->overruns()];
}
- (void)quit {
    [NSApp terminate:nil];
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    (void)sender;
    [timer invalidate];
    [self save];
    [self stopAudio];
    return NSTerminateNow;
}
@end
int main(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication *app = NSApplication.sharedApplication;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        __attribute__((objc_precise_lifetime)) DriftDelegate *delegate =
            [[DriftDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
