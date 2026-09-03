// mtldrawprobe.m — standalone experiment: which CAMetalLayer integration
// strategy makes [layer nextDrawable] non-nil on a GLFW 3.5.1 NO_API window
// on this macOS? Each strategy uses a FRESH window, pumps the run loop, and
// samples nextDrawable. Prints a per-strategy verdict.
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/AppKit.h>
#include <stdio.h>
#include <unistd.h>

// A content view whose layer class is CAMetalLayer (strategy G).
@interface MetalContentView : NSView @end
@implementation MetalContentView
+ (Class)layerClass { return [CAMetalLayer class]; }
- (BOOL)wantsUpdateLayer { return YES; }
@end

struct Strategy { const char* name; int mode; };

// mode:
//  A = attach(view.layer=layer;wantsLayer) THEN setsize   (current behavior)
//  B = setsize BEFORE attach
//  C = wantsLayer=YES first, then view.layer=layer, then setsize
//  D = setsize before attach + wantsLayer after
//  E = like A but pump extra run-loop turns before first nextDrawable
//  F = sublayer: wantsLayer=YES; [view.layer addSublayer:metal]; metal.frame=bounds
//  G = custom NSView subclass (+layerClass=CAMetalLayer) as contentView
static void runStrategy(const char* name,int mode,int W,int H) {
    if (!glfwInit()) { fprintf(stderr,"glfwInit failed\n"); return; }
    glfwWindowHint(GLFW_RESIZABLE,GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE,GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
    GLFWwindow* win=glfwCreateWindow(W,H,name,nullptr,nullptr);
    if (!win) { fprintf(stderr,"%s: createWindow failed\n",name); glfwTerminate(); return; }
    NSView* view=(NSView*)glfwGetCocoaView(win);
    id<MTLDevice> dev=MTLCreateSystemDefaultDevice();
    id<MTLCommandQueue> q=[dev newCommandQueue];
    double scale=[((NSWindow*)glfwGetCocoaWindow(win)) backingScaleFactor];
    if (scale<=0) scale=1.0;
    // H5: force the process to be an active foreground GUI app before/with the
    // attach. Command-line processes are not "active" by default, which can
    // prevent the window server from allocating CAMetalLayer drawables.
    if (mode=='H' || mode=='I') {
        NSApplication* app=[NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app activateIgnoringOtherApps:YES];
        [((NSWindow*)glfwGetCocoaWindow(win)) makeKeyAndOrderFront:nil];
    }
    CAMetalLayer* layer=[CAMetalLayer layer];
    layer.pixelFormat=MTLPixelFormatBGRA8Unorm;
    layer.contentsScale=scale;
    layer.displaySyncEnabled=NO; // isolate: keep vsync OFF across all strategies
    CGSize ds=CGSizeMake(W*scale,H*scale);
    int attached=0;
    switch (mode) {
      case 'A': view.layer=layer; view.wantsLayer=YES; attached=1; break;
      case 'B': layer.drawableSize=ds; view.layer=layer; view.wantsLayer=YES; attached=1; break;
      case 'C': view.wantsLayer=YES; view.layer=layer; layer.drawableSize=ds; attached=1; break;
      case 'D': layer.drawableSize=ds; view.layer=layer; view.wantsLayer=YES; attached=1; break;
      case 'F':
        view.wantsLayer=YES;
        [view.layer addSublayer:layer];
        layer.frame=view.bounds;
        layer.drawableSize=ds;
        attached=1; break;
      case 'H': view.layer=layer; view.wantsLayer=YES; attached=1; break; // activate + A
      case 'I': layer.drawableSize=ds; view.layer=layer; view.wantsLayer=YES; attached=1; break; // activate + B
      case 'G':
        view.layer=layer; // on our MetalContentView, layerClass is CAMetalLayer
        layer.drawableSize=ds;
        attached=1; break;
    }
    // For G we actually need the contentView to BE a MetalContentView.
    if (mode=='G') {
        MetalContentView* mv=[[MetalContentView alloc] initWithFrame:NSMakeRect(0,0,W,H)];
        mv.layer=layer; layer.drawableSize=ds;
        [((NSWindow*)glfwGetCocoaWindow(win)) setContentView:mv];
        view=mv; attached=1;
    }
    // Pump the run loop ~1.5s, sampling nextDrawable.
    int nonNil=0, firstOKat=-1; int samples=0;
    for (int i=0;i<150;++i) {
        glfwPollEvents();
        if (mode=='E' && i<40) { usleep(8000); continue; } // E: delay first probe
        samples++;
        id<CAMetalDrawable> d=[layer nextDrawable];
        if (d) {
            if (firstOKat<0) firstOKat=samples;
            nonNil++;
            // present a clear frame to release the drawable
            id<MTLCommandBuffer> cb=[q commandBuffer];
            MTLRenderPassDescriptor* rd=[MTLRenderPassDescriptor renderPassDescriptor];
            rd.colorAttachments[0].texture=d.texture;
            rd.colorAttachments[0].loadAction=MTLLoadActionClear;
            rd.colorAttachments[0].storeAction=MTLStoreActionStore;
            rd.colorAttachments[0].clearColor=MTLClearColorMake(1,0,0,1);
            id<MTLRenderCommandEncoder> e=[cb renderCommandEncoderWithDescriptor:rd];
            [e endEncoding];
            [cb presentDrawable:d];
            [cb commit];
            [cb waitUntilCompleted];
        }
        usleep(10000);
    }
    fprintf(stderr,"[%s] mode=%c attached=%d scale=%.1f ds=(%.0f,%.0f) "
              "samples=%d nonNilDrawables=%d firstOKsample=%d\n",
            name,mode,attached,scale,ds.width,ds.height,samples,nonNil,firstOKat);
    glfwDestroyWindow(win);
    glfwTerminate();
    [q release]; [layer release]; [dev release];
}

int main(int argc,char** argv) {
    // default runs A,B,C,D,E,F,G ; or pass specific letters
    const char* sel=(argc>1)?argv[1]:"ABCDE FG";
    int W=688,H=702;
    for (const char* p=sel;*p;++p) {
        char c=*p;
        if (c==' ') continue;
        runStrategy("probe",c,W,H);
    }
    return 0;
}