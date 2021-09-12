# simple-battery-voltage-alarm
A Linux program which can make alarm sound in the terminal while battery voltage is out of proper range (not good for the battery) and make some statistic information.

## Installation
```
git clone https://github.com/wuwbobo2021/simple-battery-voltage-alarm
cd simple-battery-voltage-alarm
sudo g++ simplevoltagealarm.cpp -std=c++11 -pthread -o /usr/bin/simple-battery-voltage-alarm
```

## Known problems
1. This is a terminal program, which cannot run on startup or in background, and it may supsend in sleep mode.
2. This program only suports single battery.
3. Calculation of 'mAh' don't care the internal resistance (maybe it's not a problem).
4. Yet this program don't use the interface functions declared in 'power_supply.h' of Linux kernel headers. (it might not be a problem)
Reference: https://www.kernel.org/doc/html/latest/power/power_supply_class.html
