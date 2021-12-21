# Effects Pedal Simulator

## Uses MODEP and LV2

# Installation

* On a Raspberry PI with modep, modep-mod-ui, and all the associated LV2 plugins
:
:

* Compile driver: `make zip`

* Run `./EffectsStart

# Setup

Using https://blokas.io/modep/ set up some pedal boards and save them into the library

In a teminal switch to the ModHostPedal directory

Run ./Effects

## Configuring the pedal

The pedal is a USB keyboard with three keys. 'A', 'B', and 'C'

They load  instructions from the files in PEDALS

Put a link in PEDALS from 'A' -> WhateverPedalFile and the left most pedal will trigger loading that pedal

## The Button

The author of this has set up the button (on the Pi Sound) so that:

* One click starts the effects and turns off `modep-mod-host` by running `EffectsStart`
* Two clicks stops the effects and turns on `modep-mod-host` by running `EffectsStop`
* Three clicks runs setpedals03.sh  and four clicks runs setpedals04.sh which change the pedal board.

# How Fast?

Run the driver: `driver 2>&1 > /tmp/d1.out `

Then when it has done some work kill it with a signal: `killall -9 driver` then
`grep Implemen /tmp/d1.out |cut -d ' ' -f 2-3|sort |perl -e 'while(<>){/^(..)\s+(\d+)\s*$/ or die $_; defined $cnt{$1} or $cnt{$1} = 0; $cnt{$1}++; defined $reg{$1} or $reg{$1} = 0; $reg{$1} += $2; } print join("\n", map{"$_ => " . ($reg{$_}/$cnt{$_})} sort keys %reg)."\n";'` 

Produces a listing of the average time for implementing a pedal in micro seconds.  Times less than 100,000 microseconds are necessary for good performance.  The ear looses its ability to hear the changes if they are faster than a tenth of a second.

A design goal is sub 10,000 micro seconds, one hundreth.  Not close to that but benerally the results of the above are like:

```
A: => 59712.2631578947
B: => 62705.5789473684
C: => 68128.5238095238
```

(Ridiculously precise....)
