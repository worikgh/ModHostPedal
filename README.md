# Effects Pedal Simulator

## Uses MODEP and LV2

# Installation

0. On a Raspberry PI with modep, modep-mod-ui, and all the associated LV2 plugins
:
:

3. Compile driver: `make zip`

4. Add the user modep ito the group `input`  

5. Run `./EffectsStart

# Setup

Using https://blokas.io/modep/ set up some pedal boards and save them into the library

In a teminal switch to the ModHostPedal directory

Copy `patch-mod-host.service` to /etc/systemd/system/

Run ./Effects

## Configuring the pedal

The pedal is a USB keyboard with three keys. 'A', 'B', and 'C'

They load  instructions from the files in PEDALS

Put a link in PEDALS from 'A' -> WhateverPedalFile and the left most pedal will trigger loading that pedal
