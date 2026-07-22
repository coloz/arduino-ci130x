#ifndef CI130X_ARDUINO_PIN_ALIASES_H
#define CI130X_ARDUINO_PIN_ALIASES_H

#include <stdint.h>

// The SDK uses PAx/PBx/etc. for physical pad numbers, while Arduino APIs use
// logical port numbering defined by each variant. Rename the SDK enumerators
// while parsing its header so sketches can use conventional port names without
// mapping PA2 to the SDK pad number 6 by mistake. In the normal Arduino include
// order, physical pads remain available as CI130X_PAD_PAx, CI130X_PAD_PBx, etc.
#define PA0 CI130X_PAD_PA0
#define PA1 CI130X_PAD_PA1
#define PA2 CI130X_PAD_PA2
#define PA3 CI130X_PAD_PA3
#define PA4 CI130X_PAD_PA4
#define PA5 CI130X_PAD_PA5
#define PA6 CI130X_PAD_PA6
#define PA7 CI130X_PAD_PA7
#define PB0 CI130X_PAD_PB0
#define PB1 CI130X_PAD_PB1
#define PB2 CI130X_PAD_PB2
#define PB3 CI130X_PAD_PB3
#define PB4 CI130X_PAD_PB4
#define PB5 CI130X_PAD_PB5
#define PB6 CI130X_PAD_PB6
#define PB7 CI130X_PAD_PB7
#define PC0 CI130X_PAD_PC0
#define PC1 CI130X_PAD_PC1
#define PC2 CI130X_PAD_PC2
#define PC3 CI130X_PAD_PC3
#define PC4 CI130X_PAD_PC4
#define PC5 CI130X_PAD_PC5
#define PD0 CI130X_PAD_PD0
#define PD1 CI130X_PAD_PD1
#define PD2 CI130X_PAD_PD2
#define PD3 CI130X_PAD_PD3
#define PD4 CI130X_PAD_PD4
#define PD5 CI130X_PAD_PD5
#include <ci130x_scu.h>
#undef PA0
#undef PA1
#undef PA2
#undef PA3
#undef PA4
#undef PA5
#undef PA6
#undef PA7
#undef PB0
#undef PB1
#undef PB2
#undef PB3
#undef PB4
#undef PB5
#undef PB6
#undef PB7
#undef PC0
#undef PC1
#undef PC2
#undef PC3
#undef PC4
#undef PC5
#undef PD0
#undef PD1
#undef PD2
#undef PD3
#undef PD4
#undef PD5

#endif
