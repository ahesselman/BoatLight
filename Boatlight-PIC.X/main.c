// CONFIG
#pragma config FOSC = INTRCIO
#pragma config WDTE = OFF
#pragma config PWRTE = OFF
#pragma config MCLRE = OFF
#pragma config BOREN = OFF
#pragma config CP = OFF
#pragma config CPD = OFF

#include <xc.h>
#include <stdbool.h>

#define _XTAL_FREQ          4000000

// Pin definitions
#define SECTION_RED         GP0
#define SECTION_GREEN       GP1
#define SECTION_WHITE_01_10 GP2
#define SECTION_WHITE_11_16 GP3
#define BUTTON              GP4

typedef enum {
    OFF = 0,
    RED_GREEN,
    WHITE_01_10,
    WHITE_11_16,
    FULL_COMBO,
    ALL_WHITE,
    ALL_WHITE_FLASH,
    ALL_WHITE_SOS,
    STATE_MAX
} SystemState;

volatile unsigned int msTicks       = 0;
volatile unsigned int flash_timer   = 0;
volatile unsigned int sos_timer     = 0;
volatile unsigned char sos_index    = 0;
volatile bool sosState              = false;
volatile bool flashState            = false;

// Morse code pattern: 1 = short (200ms), 3 = long (600ms), 0 = OFF
const unsigned char sos_pattern[] = {
    1,0, 1,0, 1,0,    // S
    3,0, 3,0, 3,0,    // O
    1,0, 1,0, 1,0,    // S
    0                // End
};

SystemState state = FULL_COMBO;

void resetSections(void) {
    SECTION_RED = 0;
    SECTION_GREEN = 0;
    SECTION_WHITE_01_10 = 0;
    SECTION_WHITE_11_16 = 0;
}

// === TIMER0 ISR ===
void __interrupt() isr(void) {
    if (T0IF) {
        T0IF = 0;             // Clear interrupt flag
        TMR0 = 256 - 250;     // Preload for ~1ms tick
        msTicks++;

        // Flash for ALL_WHITE_FLASH state
        if (++flash_timer >= 500) {
            flash_timer = 0;
            flashState = !flashState;
        }

        // SOS logic
        if (state == ALL_WHITE_SOS) {
            unsigned int duration = (sos_pattern[sos_index] == 1) ? 200 :
                                    (sos_pattern[sos_index] == 3) ? 600 : 0;

            sos_timer++;
            if (sos_timer >= duration) {
                sos_timer = 0;
                sosState = !sosState;
                sos_index++;
                if (sos_pattern[sos_index] == 0) sos_index = 0;
            }
        }
    }
}

void setupTimer0(void) {
    OPTION_REG = 0b00000111;  // Prescaler 1:256, assigned to TMR0
    TMR0 = 256 - 250;         // ~1ms at 4MHz
    T0IE = 1;                 // Enable Timer0 interrupt
    GIE = 1;                  // Enable global interrupts
}

void main(void) {
    TRISIO = 0b00010000;   // GP4 = input (button), others = output
    CMCON = 0x07;          // Disable comparator (enables digital I/O)
    GPIO = 0x00;
    WPU = 0b00010000;      // Pull-up on GP4
    OPTION_REGbits.nGPPU = 0;  // Enable pull-ups

    setupTimer0();

    unsigned char lastButton = 1; 
    unsigned long button_pressed_time = 0;
    bool button_held = false;
    unsigned int cycle_state_timer = 0;
    SystemState last_active_state = FULL_COMBO;

    while (1) {
        // Read current button state
        unsigned char button = BUTTON;

        // Detect falling edge (button just pressed)
        if (button == 0 && lastButton == 1) {
            button_pressed_time = msTicks;
            button_held = false;
        }

        // Button held
        if (button == 0 && !button_held) {
            if ((msTicks - button_pressed_time) >= 1000) {  // 1 sec = long press
                button_held = true;
                cycle_state_timer = msTicks;
                // Skip OFF state
                if (state == OFF) state = 1;
            }
        }

        // While holding, cycle through states every 2 sec
        if (button == 0 && button_held) {
            if ((msTicks - cycle_state_timer) >= 2000) {
                state = (SystemState)(((int)state + 1) % STATE_MAX);
                if (state == OFF) state = 1;  // Skip OFF
                resetSections();
                cycle_state_timer = msTicks;
            }
        }

        // Button released
        if (button == 1 && lastButton == 0) {
            if (!button_held) {
                // Short press: toggle OFF
                if (state != OFF) {
                    last_active_state = state;
                    state = OFF;
                } else {
                    state = last_active_state;
                }
                resetSections();
            }
        }

        lastButton = button;
        
        if (state >= STATE_MAX) {
            state = OFF;
        }
        
        // Output logic
        switch (state) {
            case OFF:
            case STATE_MAX:
                resetSections();
                break;
            case RED_GREEN:
                SECTION_RED = 1;
                SECTION_GREEN = 1;
                break;
            case WHITE_01_10:
                SECTION_WHITE_01_10 = 1;
                break;
            case WHITE_11_16:
                SECTION_WHITE_11_16 = 1;
                break;
            case FULL_COMBO:
                SECTION_RED = 1;
                SECTION_GREEN = 1;
                SECTION_WHITE_11_16 = 1;
                break;
            case ALL_WHITE:
                SECTION_WHITE_01_10 = 1;
                SECTION_WHITE_11_16 = 1;
                break;
            case ALL_WHITE_FLASH:
                SECTION_WHITE_01_10 = flashState;
                SECTION_WHITE_11_16 = flashState;
                break;
            case ALL_WHITE_SOS:
                SECTION_WHITE_01_10 = sosState;
                SECTION_WHITE_11_16 = sosState;
                break;
        }
    }
}
