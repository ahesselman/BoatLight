# Boat light controller

Controller for a boat light which indicates port and starboard. 

## Description

BoatLight is an LED ring system designed to indicate the port (red) and starboard (green) sides of a boat. It supports eight user-selectable modes, which can be toggled using a physical button. The currently selected mode is saved to persistent storage, so it resumes automatically after a power cycle.

The device is solar-powered. It monitors the voltage from a connected solar panel to determine whether it should be active or idle. When the solar panel voltage drops below 2.35V, the device activates. When the voltage rises above 2.35V, the device enters an idle state to conserve power.

## Getting Started

### Dependencies

### Setup

* https://www.instructables.com/How-to-Program-an-Attiny85-From-an-Arduino-Uno/

## Authors
LedOptions

### Executing program

## Version History

* 0.1
    * Initial release

## Acknowledgments