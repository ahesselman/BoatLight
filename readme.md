# Boat light controller

Controller for a boat light which indicates port and starboard. 

## Description

BoatLight is an LED ring system designed to indicate the port (red) and starboard (green) sides of a boat. It supports eight user-selectable modes, which can be toggled using a physical button. The currently selected mode is saved to persistent storage, so it resumes automatically after a power cycle.

The device is solar-powered. It monitors the voltage from a connected solar panel to determine whether it should be active or idle. When the solar panel voltage drops below 2.35V, the device activates. When the voltage rises above 2.40V, the device enters an idle state to conserve power.

## System overview

* **Power source**: solar panel and USB input
* **Battery**: ?
* **Charging module**: TP4056 for battery charging management
* **Microcontroller**: ATTiny85 for controlling the LEDs and handling user input
* **LEDs**: 16 SK6812 addressable RGBW LEDs
* **User input**: Single push-button for mode selection
* **Power control**: P-channel MOSFET to switch LED power

### Power management

1. Charging circuit

* **TP4056 module**: Connect the solar panel and USB input through appropriate diodes to the TP4056 module's input.
* **Battery connection**: The battery connects to the BAT+ and BAT- terminals of the TP4056.

2. Load sharing

To allow the system to operate while charging and prevent the battery from supplying power when external sources are available, load sharing is implemented

3. Voltage monitoring

A voltage divider is used to monitor the solar panel voltage. This allows the ATTiny85 to detect when the solar voltage drops below 2.35 and activate the LEDs accordingly. To improve stability and prevent unwanted rapid switching, hysterisis is applied. 

### Control circuit

1. ATTiny85 Microcontroller

* **Power supply**: powered from the battery through a voltage regeulator (if necessary).

* **Inputs**:
    * **Voltage divider**: Connected to an ADC pin to monitor solar voltage.
    * **Push-button**: Connected to a digital input pin with a pull-up resistor for mode selection.

* **Outputs**:
    * **LED Control**: A digital output pin connected to the data line of the SK6812 LED strip.
    * **MOSFET control**: A digital output pin to control  the gate of the P-channel MOSFET, switching the LED POWER.

2. P-Channel MOSFET

Used to switch the power for the SK6812 LEDs. The gate is controlled by the the ATTiny85, allowing the microcontroller to disconnect power from the LEDs when not in use, conserving battery life.

## Getting Started

### Dependencies

* Adafruit_NeoPixel

### Setup

* https://www.instructables.com/How-to-Program-an-Attiny85-From-an-Arduino-Uno/

## Authors
LedOptions

### Executing program

## Version History

* 0.1
    * Initial release

## Acknowledgments