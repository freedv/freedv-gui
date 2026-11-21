// Checks the icon that macOS shows for an app bundle (in the Finder, the Dock
// and so on), on whichever version of macOS this is run on:
//
//     macos_app_icon <path to FreeDV.app> [where to save what's shown, as PNG]
//
// Each of these has to be FreeDV's icon (see looks_like_freedv_icon() in
// test/icon_images.py, which this follows):
//
// - what NSWorkspace gives for the app: what the Finder and the Dock show,
//   whichever of the bundle's icons this version of macOS goes by;
// - the image named by CFBundleIconName, from Assets.car, which macOS 11 and
//   later go by (as Liquid Glass on macOS 26);
// - the file named by CFBundleIconFile (freedv.icns), for anything that only
//   reads that.
//
// Each is drawn at 256x256 and at 32x32 (as in a list view), which on macOS
// before 26 come from different images in Assets.car.
//
// Built with (see test_macos_app_icon.sh):
//     clang++ -std=c++17 -fobjc-arc -framework AppKit -framework UniformTypeIdentifiers macos_app_icon.mm

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

static int failures = 0;

static NSBitmapImageRep* Draw(NSImage* image, int size)
{
    NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL pixelsWide:size pixelsHigh:size bitsPerSample:8 samplesPerPixel:4
        hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    NSGraphicsContext* context = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:context];
    [[NSColor clearColor] set];
    NSRectFill(NSMakeRect(0, 0, size, size));
    [image drawInRect:NSMakeRect(0, 0, size, size) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver
             fraction:1.0 respectFlipped:YES hints:nil];
    [NSGraphicsContext restoreGraphicsState];
    return bitmap;
}

// (r, g, b, a), 0-255, with (0, 0) at the top left.
static void Pixel(NSBitmapImageRep* bitmap, int x, int y, int rgba[4])
{
    NSUInteger samples[4] = {0, 0, 0, 0};
    [bitmap getPixel:samples atX:x y:y];
    for (int i = 0; i < 4; i++)
    {
        rgba[i] = (int)samples[i];
    }
    // Un-premultiply, to compare colours as they'd be seen.
    if (rgba[3] > 0 && rgba[3] < 255)
    {
        for (int i = 0; i < 3; i++)
        {
            rgba[i] = MIN(255, rgba[i] * 255 / rgba[3]);
        }
    }
}

static BOOL IsBlue(const int p[4])
{
    return p[3] >= 192 && p[2] >= 128 && p[2] > p[0] + 40 && p[2] >= p[1];
}

static void Check(NSImage* image, NSString* what, NSString* savePath)
{
    if (image == nil)
    {
        printf("FAILED: there's no %s\n", what.UTF8String);
        failures++;
        return;
    }
    const int sizes[] = {256, 32};
    for (int size : sizes)
    {
        NSBitmapImageRep* bitmap = Draw(image, size);
        if (savePath != nil && size == 256)
        {
            [[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:savePath atomically:YES];
        }

        NSString* problem = nil;
        struct { const char* name; int x, y; } points[] = {
            {"middle", size / 2, size / 2}, {"left of the middle", size / 4, size / 2}, {"above the middle", size / 2, size / 4}};
        int p[4];
        for (auto& point : points)
        {
            Pixel(bitmap, point.x, point.y, p);
            if (problem == nil && !IsBlue(p))
            {
                problem = [NSString stringWithFormat:@"the %s is (%d, %d, %d, %d), not blue", point.name, p[0], p[1], p[2], p[3]];
            }
        }
        Pixel(bitmap, 0, 0, p);
        if (problem == nil && p[3] > 32)
        {
            problem = [NSString stringWithFormat:@"the corner is (%d, %d, %d, %d), not transparent", p[0], p[1], p[2], p[3]];
        }

        if (problem != nil)
        {
            printf("FAILED: the %s, at %dx%d, doesn't look like FreeDV's icon: %s\n", what.UTF8String, size, size, problem.UTF8String);
            failures++;
        }
        else
        {
            printf("ok: the %s, at %dx%d\n", what.UTF8String, size, size);
        }
    }
}

int main(int argc, char** argv)
{
    @autoreleasepool
    {
        if (argc < 2)
        {
            fprintf(stderr, "usage: %s <app bundle> [PNG to save what's shown to]\n", argv[0]);
            return 2;
        }
        // Made absolute: NSWorkspace doesn't look for a relative path from
        // the current directory, so it would show the plain document icon
        // for one (for a file it can't find) rather than the app's.
        NSString* appPath = [[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]] URLByStandardizingPath].path;
        NSString* savePath = argc > 2 ? [NSString stringWithUTF8String:argv[2]] : nil;
        NSBundle* bundle = [NSBundle bundleWithPath:appPath];
        if (bundle == nil)
        {
            printf("FAILED: %s isn't a bundle\n", appPath.UTF8String);
            return 1;
        }
        NSOperatingSystemVersion os = NSProcessInfo.processInfo.operatingSystemVersion;
        printf("macOS %ld.%ld.%ld, %s\n", (long)os.majorVersion, (long)os.minorVersion, (long)os.patchVersion, appPath.UTF8String);

        NSString* iconName = bundle.infoDictionary[@"CFBundleIconName"];
        NSString* iconFile = bundle.infoDictionary[@"CFBundleIconFile"];
        printf("CFBundleIconName = %s, CFBundleIconFile = %s\n", iconName.UTF8String ?: "(none)", iconFile.UTF8String ?: "(none)");
        if (iconName == nil || iconFile == nil)
        {
            printf("FAILED: Info.plist doesn't name both icons\n");
            failures++;
        }

        // What the Finder and the Dock show.
        Check([NSWorkspace.sharedWorkspace iconForFile:appPath], @"icon macOS shows for the app", savePath);

        if (iconName != nil)
        {
            NSString* car = [bundle pathForResource:@"Assets" ofType:@"car"];
            if (car == nil)
            {
                printf("FAILED: there's no Assets.car in the bundle\n");
                failures++;
            }
            Check([bundle imageForResource:iconName], [NSString stringWithFormat:@"image %@ in Assets.car", iconName], nil);
        }
        if (iconFile != nil)
        {
            NSString* name = iconFile.pathExtension.length > 0 ? iconFile.stringByDeletingPathExtension : iconFile;
            NSString* path = [bundle pathForResource:name ofType:@"icns"];
            Check(path != nil ? [[NSImage alloc] initWithContentsOfFile:path] : nil,
                  [NSString stringWithFormat:@"icon file %@.icns", name], nil);
        }

        // Not the plain one that's shown for an app without an icon of its own.
        NSImage* generic = [NSWorkspace.sharedWorkspace iconForContentType:UTTypeApplicationBundle];
        NSBitmapImageRep* shown = Draw([NSWorkspace.sharedWorkspace iconForFile:appPath], 64);
        NSBitmapImageRep* plain = Draw(generic, 64);
        long difference = 0;
        for (int y = 0; y < 64; y++)
        {
            for (int x = 0; x < 64; x++)
            {
                int a[4], b[4];
                Pixel(shown, x, y, a);
                Pixel(plain, x, y, b);
                for (int i = 0; i < 4; i++)
                {
                    difference += labs((long)(a[i] - b[i]));
                }
            }
        }
        double average = difference / (64.0 * 64 * 4);
        if (average < 10)
        {
            printf("FAILED: the icon macOS shows for the app is the plain one for apps without their own (average difference %.1f)\n", average);
            failures++;
        }

        printf(failures == 0 ? "PASS\n" : "%d problem(s)\n", failures);
        return failures == 0 ? 0 : 1;
    }
}
