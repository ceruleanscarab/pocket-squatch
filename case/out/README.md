# Pocket Pony barn case

A little red barn for the Waveshare ESP32-S3-Touch-AMOLED-1.8 **in its own black case**. The board slides into a
pocket from the back, the screen shows through the framed window, and the back door (with a speaker grille)
pegs in to hold it. Made by `../barn_case.py`; run it again with your measurements.

**Measure first.** The pocket is sized for a case of 37.6 x 45.2 mm and **13.0 mm deep** (the depth is a guess:
check it with calipers) with 0.4 mm of play. If yours differs:
`python3 barn_case.py --depth 13.6` (also `--width`, `--height`, `--screen-up` if the screen isn't centred).

## Printing (Bambu A1 with the AMS lite)

1. In Bambu Studio, import the five `barn-1` to `barn-5` files **together** and answer **Yes** to
   "load as a single object with multiple parts". Give each part its colour: walls red, roof dark brown or
   grey, trim and sign white, base green.
2. Print it standing up, as it comes in (base on the plate). 0.2 mm layers, 3 walls, 15% infill;
   no supports needed (the window top and the pocket roof are short bridges).
3. Print `barn-back-door.stl` on its own, flat, pegs up, in red.

Barn: about 66 x 34 x 95 mm. Door: 43.6 x 50.7 x 2.2 mm.

## Putting it together

- Plug the USB-C cable into the board before sliding it in: the plug sits in the "cellar" under the pocket
  and the cable leaves through the back. (A right-angle USB-C cable is neatest.)
- Slide the board in from the back, screen first, then press the door's pegs into the four holes.
  The side windows reach the PWR and BOOT buttons.
