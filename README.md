arduino-brouwkuyp-control
=========================

Brouwkuyp brewery control software Arduino part

### Sensors used: ###
Temperatuursensor Hygrosens Temperatuursensor M10, kabel 2 m. -55 tot 125 °C. Soort behuizing Roestvrijstalen sensorbuis. 

<img src="http://www.conrad.nl/medias/global/ce/1000_1999/1800/1840/1840/184082_BB_00_FB.EPS_1000.jpg" alt="M10 Temperature Sensor" style="width: 400px;"/>

You can get this at [Conrad](http://www.conrad.nl/ce/nl/product/184082/Temperatuursensor-Hygrosens-Temperatuursensor-M10-kabel-2-m-55-tot-125-C-Soort-behuizing-Roestvrijstalen-sensorbuis/SHOP_AREA_37353?).

### Electrical schema ###

@todo add image

Basically you put **5V**, **Ground** and **Control Wire (PIN 2)** on bread board (left side). You align your 5V, Ground and Control Wire of the Temperature Sensor directly (on right side). Between the 5V and Control Wire of Arduino and Sensor you put a **2.2K** resistor closest to the Arduino (since your only need one resistor if your using multiple sensors). Put one end of the resistor in the 5V and the other end in the Control Wire.
It's necessary to use a **2.2K** resistor since all other tutorials refer to a *4.7K* resistor, which doesn't work.

### Upload ###

Upload the code to your Arduino and you should see the temperature in your Serial Monitor (cmd + shift + M), prefixed with the sensors' HEX address.
