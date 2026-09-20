# Display

Wingflight supports displays to provide information to you about your aircraft and Wingflight state.

When the aircraft is armed the display does not update so flight is not affected.  When disarmed the display cycles between various pages.

There is currently no way to change the information on the pages, the list of pages or the time between pages - Code submissions via pull-requests are welcomed!

## Supported Hardware

At this time no other displays are supported other than the SSD1306 / UG-2864HSWEG01.

## Configuration

From the CLI enable the `DISPLAY` feature

```
feature DASHBOARD
```


### SSD1306 OLED displays

The SSD1306 display is a 128x64 OLED display that is visible in full sunlight, small and consumes very little current.  
This makes it ideal for aircraft use.

There are various models of SSD1306 boards out there, they are not all equal and some require addtional modifications
before they work.  Choose wisely!

Links to displays:

 * [banggood.com](http://www.banggood.com/0_96-Inch-4Pin-White-IIC-I2C-OLED-Display-Module-12864-LED-For-Arduino-p-958196.html) 0.96 Inch 4Pin White IIC I2C OLED Display Module 12864 LED For Arduino 
 * [banggood.com](http://www.banggood.com/0_96-Inch-4Pin-IIC-I2C-Blue-OLED-Display-Module-For-Arduino-p-969147.html) 0.96 Inch 4Pin IIC I2C Blue OLED Display Module For Arduino
 * [wide.hk](http://www.wide.hk/products.php?product=I2C-0.96%22-OLED-display-module-%28-compatible-Arduino-%29) I2C 0.96" OLED display module
 * [witespyquad.gostorego.com](http://witespyquad.gostorego.com/accessories/readytofly-1-oled-128x64-pid-tuning-display-i2c.html) ReadyToFlyQuads 1" OLED Display
 * [multiwiicopter.com](http://www.multiwiicopter.com/products/1-oled) PARIS 1" OLED 128x64 PID tuning screen AIR

The banggood.com display is the cheapest at the time fo writing and will correctly send I2C ACK signals.

## Connections

Connect +5v, Ground, I2C SDA and I2C SCL from the flight controller to the display.


