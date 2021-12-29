#!/bin/sh
## Run if button pressed four times
MOD_HOST_PEDAL_DIR=/home/patch/ModHostPedal
PEDALS_DIR=$MOD_HOST_PEDAL_DIR/PEDALS
LED_FLASH=$MOD_HOST_PEDAL_DIR/led_flash
## Set up pedals
rm $PEDALS_DIR/A
rm $PEDALS_DIR/B
rm $PEDALS_DIR/C

runuser -u patch -- ln -s $PEDALS_DIR/RVB $PEDALS_DIR/A
runuser -u patch -- ln -s $PEDALS_DIR/Chorus $PEDALS_DIR/B
runuser -u patch -- ln -s $PEDALS_DIR/Coubler $PEDALS_DIR/C


$LED_FLASH 1 0.35 4
sleep 1
$LED_FLASH 1 0.35 4
