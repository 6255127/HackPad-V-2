# Hackpad

A small wireless macropad with 9 keys, a rotary encoder and an OLED screen. It works over Bluetooth or USB-C, and it runs off a little LiPo battery that charges through the same USB-C port.

![](https://fabricate.hackclub-assets.com/673392f2a2daee45ec3748da2f062a700f86d97457b96b8251969106a660d054/image-png/Screenshot%202026-11-26%20091126.png)

## What's on it

- 9 keys in a 3x3 grid
- A rotary encoder (volume by default)
- 5 LEDs that show battery level while charging, go green when it's full, and blink blue when it isn't paired
- A 2.42" OLED
- Seeed XIAO nRF52840 as the brain, since it already has Bluetooth and battery charging built in

## The PCB

I picked the XIAO nRF52840 so I wouldn't have to build the Bluetooth and charging parts myself. I tried a bunch of layouts and kept the one with the smallest footprint.

![](https://fabricate.hackclub-assets.com/ecdb585d35ec802040a3512f4c7a0eb19de8211ca3ec9f5a743cabb74ba56685/image-png/Screenshot%202026-11-26%20073557.png)
![](https://fabricate.hackclub-assets.com/9fbd55987df7046d6786a0e655e4353db124186b15d2988642f3c9cd44a1765a/image-png/Screenshot%202026-11-26%20073914.png)

## The case

It has a clear cover so you can see the LEDs. It's pretty thick, and that's because of the battery.

![](https://fabricate.hackclub-assets.com/3c85a27e2e630f88a066f75f1251cd7a7d2b05fcd72d1b0a32158318efd7364b/image-png/Screenshot%202026-11-26%20091134.png)
![](https://fabricate.hackclub-assets.com/2ad2392b0397a6a2642e16e026b9ce3c3f5ca764eb4903460981123ccfc253e7/image-png/Screenshot%202026-11-26%20091116.png)

## Firmware

The firmware is basic, and AI helped write it. It sends keys over Bluetooth and USB, and drives the LEDs and screen.

## Status

Design is finished and I think it's ready to ship. The full build log is in [journal.md](journal.md).
