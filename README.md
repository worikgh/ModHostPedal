# Effects Pedal Simulator Raspberry PI with Pisound

**This is brand new alpha software.**

[Pisound](https://blokas.io/pisound/) provides the digital signal processing and the [Raspberry Pi](https://www.raspberrypi.org/) runs [LV2](https://lv2plug.in/) effects.  A [USB foot pedal](https://www.cdholso.com/foot-pedal-switch/usb-3-triple-pedal-foot-switch.html) switches between digital effects.  

The USB foot pedal is essentially a USB keyboard with only a few keys.

## MODEP

[MODEP](https://blokas.io/modep/) is a front end for LV2.  It facilitates setting chains of effects with an intuitive web app front end.

# Installation

* On a Raspberry PI with modep, modep-mod-ui, and all the associated LV2 plugins

* Install [`mod-host`](https://github.com/moddevices/mod-host) in `/home/patch` and build using `make  PREFIX=/home/patch/mod-host` and `make  PREFIX=/home/patch/mod-host install`

* Clone this repository into `/home/patch`

* Compile driver: `make zip` in `/home/patch`

* Set up some pedals (see `Configuring the Pedal` below)

* Run `./EffectsStart`  in `/home/patch`

# Setup

Using https://blokas.io/modep/ set up some pedal boards and save them into the library

In a terminal switch to the ModHostPedal directory

Run `./EffectsStart` as root

To restore `Modep/mod-host` run `./EffectsStop` as root

## Configuring the Pedal

The pedal is a USB keyboard with three keys. 'A', 'B', and 'C'

They load  instructions from the files in PEDALS

Put a link in PEDALS from 'A' -> WhateverPedalFile and the left most pedal will trigger loading that pedal

## Control

The script `EffectsStart` stops the `Modep/mod-host` process and starts this pedal's process.  `EffectsStop` stops the software for this pedal and restarts the `Modep/mod-host` process

* The scripts setpedals03.sh  and  setpedals04.sh are example scripts for changing the pedal board.  They must be run as root (to access the led on the Pisound.  Otherwise they could be run as the `patch` user)

# How Fast?

A design goal is sub 10,000 micro seconds, one hundredth.  This has not nearly been achieved.

Using data the driver writes into `/tmp/driver.log` the following averages 

`grep Implemen /tmp/driver.log |cut -d ' ' -f 2-3|sort |perl -e 'while(<>){/^(..)\s+(\d+)\s*$/ or die $_; defined $cnt{$1} or $cnt{$1} = 0; $cnt{$1}++; defined $reg{$1} or $reg{$1} = 0; $reg{$1} += $2; } print join("\n", map{"$_ => " . ($reg{$_}/$cnt{$_})} sort keys %reg)."\n";'`

```
A: => 63489.3333333333
B: => 51726.6666666667
C: => 82500.1666666667
```


Ridiculously precise!  And there is quite a bit of variation.
