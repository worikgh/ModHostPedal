# Effects Pedal Simulator

## Uses MODEP and LV2

# Installation

0. On a Raspberry PI with modep, modep-mod-ui, and all the associated LV2 plugins

1. Do the following steps as the user `modep`

2. Unpack this repository: `/var/modep` creating `/var/modep/ModHostPedal`

3. Compile driver: `make`

4. Run `./Effects`

# Setup

Using https://blokas.io/modep/ set up some pedal boards and save them into the library

In a teminal switch to the ModHostPedal directory

Run ./Effects

## Configuring the pedal

The pedal is a USB keyboard with three keys. 'A', 'B', and 'C'

They load  instructions from the files in PEDALS

Put a link in PEDALS from 'A' -> WhateverPedalFile and the left most pedal will trigger loading that pedal
