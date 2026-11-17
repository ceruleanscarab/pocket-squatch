# Pocket Pony barn stand (round 1.75C)

A red barn desk stand for the Waveshare ESP32-S3-Touch-AMOLED-1.75C, the round aluminium puck. The puck sits
in the barn's round hayloft window, leaning back 10 degrees, and lifts out to go on its lanyard. Made by
`../barn_stand.py`; run it again with your measurements.

**Measure the puck first.** The window is sized for a puck **54.0 mm across and 14.0 mm thick** (both are
guesses from the product photos: check them with calipers), with 0.4 mm of play all round:
`python3 barn_stand.py --diameter 54.2 --thickness 13.8` (and `--tilt 8` to lean it less).

## Printing (Bambu A1 with the AMS lite)

1. In Bambu Studio, import the five `stand-1` to `stand-5` files **together** and answer **Yes** to
   "load as a single object with multiple parts". Colours: walls red, roof dark brown or grey, trim and sign
   white, base green. (`preview-puck.stl` is only for looking: don't print it.)
2. Print it standing up, as it comes in, base on the plate. 0.2 mm layers, 3 walls, 15% infill. Turn on
   supports **on build plate only** for the cable tunnel under the barn (or skip them: it's a short bridge).

Stand: about 89 x 46 x 95 mm.

## Using it

- The two buttons on the puck's right edge sit in a notch, so the stand never presses them.
- To charge it in the stand: feed a USB-C cable in through the tunnel at the back of the base, up the slot
  under the window, plug it into the puck, then set the puck in, bottom first. A right-angle USB-C cable
  sits neatest.
- To take it out: a fingertip in the notch at the top or on the left, tip it forward and lift.
