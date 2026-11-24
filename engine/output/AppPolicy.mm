#import <AppKit/AppKit.h>

namespace vj::output {

// Dentro del programa instalado, el motor no muestra su propio icono en el Dock.
void hideFromDock() { [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory]; }

}  // namespace vj::output
