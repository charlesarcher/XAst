#import <Cocoa/Cocoa.h>
int main() {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app finishLaunching];
        NSArray* screens = [NSScreen screens];
        NSScreen* main = [NSScreen mainScreen];
        NSLog(@"NSScreen mainScreen: %@", [main deviceDescription]);
        NSLog(@"  mainScreen frame=%@ backingScaleFactor=%.2f", [main frame], [main backingScaleFactor]);
        for (NSUInteger i = 0; i < [screens count]; i++) {
            NSScreen* s = [screens[i]];
            BOOL isMain = (s == main);
            NSLog(@"screen[%zu]: %@ frame=%@ backingScaleFactor=%.2f %s", i,
                  [s deviceDescription], [s frame], [s backingScaleFactor],
                  isMain ? "(MAIN)" : "");
        }
    }
    return 0;
}
