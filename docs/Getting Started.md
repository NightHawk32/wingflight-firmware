# Getting Started

A step-by-step guide to setting up a flight controller and the aircraft around it for flight.
Basic RC knowledge is required.

DISCLAIMER: this document is a helping guide, not an authoritative checklist. We cannot guarantee
the safety or success of your project. Read [Safety](Safety.md) first.

## Hardware

NOTE: flight controllers contain accelerometers, which are sensitive to shocks. A board that is
not yet installed has very little mass, so if you drop or bump it, it can see very large forces.
Handle it with care.

* Read the manual that came with your board. Ignore its software setup, which is covered here.
  See [Boards](Boards.md) for how hardware support works.
* Decide how you will connect your receiver: read [Receivers](Rx.md). Work out how many outputs
  you need for your servos and motor: read [Mixer](Mixer.md). A basic airplane needs four servos
  and one motor.
* To monitor the flight battery, see [Battery Monitoring](Battery.md).
* For audible feedback, see [Buzzer](Buzzer.md).
* To send the receiver's signal strength to the board, see [RSSI](Rssi.md).
* For return-to-home and loiter with a GPS unit, see [GPS](Gps.md). These are experimental.
* To work out which extra devices (blackbox, telemetry, GPS) need a serial port, and how to
  connect them, read [Serial](Serial.md).
* Solder only the pins you need.

## Software setup

Install the [Wingflight Configurator](https://github.com/WingFlight/wingflight-configurator/releases)
and flash the firmware following [Installation](Installation.md).

## Configuration

Your board should now run Wingflight and connect to the Configurator. There are two ways to
configure it: the Configurator's tabs, and the [CLI](Cli.md). See [Configuration](Configuration.md).

* Set up your transmitter so it sends at least four channels (aileron, elevator, throttle,
  rudder), and preferably more. Put channels 5 and up on switches and dials for [modes](Modes.md).
* Connect the receiver to the flight controller, and the flight controller to the PC. You may need
  to power the receiver from a BEC (observe polarity).
* In the Configurator's first tab, check that the board model moves correctly when you move the
  board, and calibrate the accelerometer.
* Configuration tab:
  * Set the board alignment if the board is not mounted in its default orientation.
  * Choose the receiver type on the Receiver tab, and configure the serial port for it.
  * Set the ESC/motor protocol.
* Mixer and servos: set up the mixer for your airframe and set servo centres, ends of travel and
  directions. See [Mixer](Mixer.md). Do this with the propeller removed.
* Receiver tab:
  * Check the channel inputs move as you move the transmitter sticks.
  * Check the channel map is correct, along with the RSSI channel if you use one.
  * Verify each channel runs from about 1000 to about 2000. See [Controls](Controls.md).
* Modes tab: set up the modes you want. See [Modes](Modes.md). To begin with you mainly want ARM,
  and probably ANGLE or HORIZON.

## Final testing and safety

Do this on the bench, before the maiden flight.

* Read [Safety](Safety.md).
* Learn how to arm the aircraft and about the other [controls](Controls.md).
* Set up [Failsafe](Failsafe.md), carefully. **The flight controller does not disarm or land on
  its own on link loss**, so the receiver's failsafe is what protects you. Test it, propeller
  removed.
* Check that aileron, elevator and rudder inputs move the right surfaces the right way.
* Check the direction of the stabilisation. With the propeller removed, and the aircraft armed,
  roll and pitch the aircraft by hand. Each surface should move to *oppose* the motion, as if it
  were correcting for a gust.
* Check the direction of self-levelling in ANGLE or HORIZON mode. Tilt the aircraft and check the
  surfaces move to level it.

If any of these fail, do not fly. Go back to the configuration phase: a channel may need
reversing, or the board orientation is wrong.

## Flying

Go to the field, turn the transmitter on, place the aircraft on the ground, connect the flight
battery and wait for the gyro to calibrate. Arm and fly. If the aircraft will not arm, find out
why in [Controls](Controls.md).

## Advanced matters

* [Profiles](Profiles.md)
* [PID tuning](PID%20tuning.md) and [Flight Dynamics](FlightDynamics.md)
* [In-flight Adjustments](Inflight%20Adjustments.md)
* [Blackbox logging](Blackbox.md)
* [Governor](Governor.md)
* [SmartFuel](SmartFuel.md)
* [Spektrum bind](Spektrum%20bind.md)
* [Telemetry](Telemetry.md)
* [Using a display](Display.md)
* [Using an LED strip](LedStrip.md)
