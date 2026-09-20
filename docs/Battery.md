# Battery Monitoring

Wingflight can measure the main battery voltage and current. It uses them for low-battery warnings
(buzzer, status LED, telemetry), for the used and remaining capacity, and for
[SmartFuel](SmartFuel.md).

Low-battery warnings help ensure you have time to land, and help protect LiPo/LiFe packs, which
should not be discharged below the manufacturer's recommendation.

Minimum and maximum cell voltages can be set, and are used to detect the number of cells when the
battery is first connected. Per-cell monitoring is not supported, because a single ADC reads the
pack voltage.

## Connections

When dealing with batteries **ALWAYS CHECK POLARITY!**

Measure the expected voltage **first**, and then connect it to the flight controller. Powering the
flight controller with the wrong voltage or reversed polarity will likely destroy it. Make sure the
flight controller has a voltage divider that can measure your battery voltage, and that the pin
never sees more than 3.3 V. See your board's documentation for the pin and the divider it has.

## Voltage

Choose the source for the voltage with `battery_meter`:

| Value | Source |
|---|---|
| `NONE` | No voltage monitoring |
| `ADC` | The board's voltage-sense ADC input |
| `ESC` | Voltage reported by an ESC telemetry sensor |
| `FBUS` | Voltage reported by an FBUS sensor |

For `ADC`, configure the pack limits with these CLI settings:

`vbat_scale` - Adjust this to match the actual measured battery voltage to the reported value, which
you can see with the `status` command.

`vbat_max_cell_voltage` - Maximum voltage per cell, used to detect the number of cells, in 0.01 V
units, i.e. 430 = 4.3 V.

`vbat_min_cell_voltage` - Minimum voltage per cell. This triggers battery-critical alarms, in 0.01 V
units, i.e. 330 = 3.3 V.

`vbat_warning_cell_voltage` - Warning voltage per cell. This triggers battery-warning alarms, in
0.01 V units, i.e. 340 = 3.4 V.

`vbat_hysteresis` - Hysteresis for low-battery alarms, in 0.01 V units, i.e. 10 = 0.10 V.

`vbat_duration_for_warning` - How long the voltage has to stay low before the battery state is set to
warning, in 0.1 s units, i.e. 60 = 6.0 seconds.

`vbat_duration_for_critical` - The same for the critical state, in 0.1 s units.

For example:

```
set vbat_scale = 110
set vbat_max_cell_voltage = 430
set vbat_min_cell_voltage = 330
set vbat_warning_cell_voltage = 340
set vbat_hysteresis = 1
set vbat_duration_for_warning = 60
set vbat_duration_for_critical = 20
```

## Current

Choose the source for the current with `current_meter`, with the same values as `battery_meter`:
`NONE`, `ADC`, `ESC` or `FBUS`. Current monitoring gives you these values for telemetry:

* Amps
* mAh used
* Capacity remaining

Set the pack capacity with `bat_capacity`, in mAh. It is an array of values, one for each battery
profile, and `bat_profile` selects the active one.

See also [SmartFuel](SmartFuel.md), which derives a remaining-charge percentage from pack voltage
and current.

### ADC current sensor

The current meter must be calibrated so that the value read at the ADC input matches the actual
current draw. Voltage sensing is usually accurate from the factory, but current sensing varies a
lot from board to board.

A linear-response sensor converts the current through it into a voltage for the ADC. The maximum
the flight controller can read is 3.3 V (3300 mV), which is usually the limit on the maximum
measurable current. Most sensors use a shunt resistor. A few use a hall-effect sensor.

The flight controller converts the measured voltage to current with:

```
Current (A) = ADC (mV) / ibata_scale * 10 + ibata_offset / 1000
```

| Setting | Description |
|---|---|
| `ibata_scale` | Scaling factor, in mV per 10 A |
| `ibata_offset` | Offset, in mA |
| `ibata_cutoff` | Low-pass filter cutoff |

This is a straight line, y = x/m + b, so a few measurements along it are enough to calibrate any
sensor and flight controller pair.

#### Calibrate using an ammeter

**Always take off the propeller before any testing.**

1. Put an ammeter in series with the aircraft and a charged battery, for example with an XT60
   extender with one lead cut. It shows the true current.
2. Connect to the flight controller with the Configurator, and check the current calibration.
3. In the Motors tab, raise the throttle so that the ammeter reads about 1 A. (It does not have to
   be exact.)
4. Note the current the ammeter reads and the current the Configurator reports.
5. Repeat at three or more currents from 0 up to the rating of your ammeter.
6. Do a linear regression of measured on reported current, and update `ibata_scale` and
   `ibata_offset` to match. A spreadsheet works.

A shunt resistor usually has an offset of less than +/- 1000 mA. Hall-effect sensors often have
more.

Even if the calibration is right, there is a maximum current the sensor can measure. If the
aircraft exceeds that at full throttle, the reported mAh used will be lower than the true value.
Always keep an eye on the battery voltage as well.

#### Calibrate using a battery charger

If you cannot measure the current directly, you can approximate it with your charger:

1. Fully charge the flight battery.
2. Fly, using more than 50% of the pack capacity (estimated).
3. Note the mAh drawn that Wingflight reports.
4. Fully charge the battery again, and note the mAh put back.
5. Set `ibata_scale = old_ibata_scale * (mAh_recharged / reported_mAh_drawn)`.
6. Repeat and test.

This depends on the accuracy of your charger, and needs recalibrating if you change anything that
changes the current draw, such as the motor, propeller, ESC or video transmitter.
