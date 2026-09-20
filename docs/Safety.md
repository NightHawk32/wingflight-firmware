# Safety

Model aircraft can be dangerous, particularly on the test bench. Some simple rules:

* **Never** arm the aircraft with the propeller fitted unless you intend to fly.
* **Always** remove the propeller when setting up for the first time, flashing firmware, changing
  the mixer or servo settings, or if in any doubt.
* Keep clear of the propeller arc and the control surfaces when the aircraft is powered.

## Before installing

Read [Cli](Cli.md), [Controls](Controls.md), [Failsafe](Failsafe.md) and [Modes](Modes.md). In
particular, read [Failsafe](Failsafe.md): the flight controller does **not** disarm or land the
aircraft on its own when the link is lost, so configure the receiver's failsafe and check it.

Use the Receiver tab in the Configurator to check that your channels are centred at 1500 (1520 for
Futaba) and reach 1000 and 2000 at the ends of travel. If they do not, you may be unable to arm
(the endpoints are not reachable) or you may trigger failsafe unexpectedly. Adjust the endpoints
and sub-trims on your transmitter to get the range.

## Check the control directions

After any change to the mixer, servo reversal, or board orientation, check on the bench (with the
propeller removed) that every surface moves the right way for the stick, and that the stabilised
response, such as moving the aircraft by hand, opposes the motion. Check MANUAL and PASSTHROUGH
too, if you use them.
