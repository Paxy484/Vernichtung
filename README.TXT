Well, Ladies and Gentlemen!
here it is: Vernichtung at your hands!

WHAT IS IT?
-----------
Vernichtung is a friendly, clean-room reverse-engineering reconstruction 
of the DOOM Engine. This project wasn't built because I hate DOOM -- I 
absolutely love the game! I decided to create this independent engine 
layer simply because John Carmack expressed his wish that he had released 
the original id Software source code under a Permissive License instead 
of the GPL.

"The recent discussions around copilot are a good opportunity to say 
this: I wish I could have licensed the Id source code releases as BSD. 
The GPL virality wound up being a net negative, and more value would 
have come from BSD. My partners would never have gone for it, though."
- John Carmack, 7/6/2021

No, I dont hate copyleft, i only hate GPLv3 and AGPLv3 because they go 
to hardware level and network level, in fact, i love GPL-2.0, MPL-2.0 
and LGPL-2.1.

Many thanks to the people in CREDITS for their hard work.

THE BAD NEWS:
-------------
But as the months passed, I lost my interest in this project. Because of 
that, I am going to release the source code completely "AS IS" for you 
all. This code is broken, very messy, and contains plenty of bugs inside 
of it -- even though I tried my absolute best to iron out the pointer 
limits and structural loops.

HOW IT GONNA BE LOOKED LIKE:
----------------------------
Vernichtung was designed to be a "Chocolate-Crispy Hybrid":

* Enhanced 640x400 display resolution.
* Widescreen rendering using all available horizontal space on modern
  screens with aspect ratios up to 16:9 (while keeping classic 4:3).
* Uncapped rendering framerate with interpolation and optional vertical 
  synchronization (VSync) tied to the screen refresh rate.
* NO DOS LIMITS -- it is completely limit-removing.
* MIDI support via Nuked OPL3 (Licensed under LGPL-2.1, so make sure 
  to use Dynamic Linking).
* The rest is Vanilla (that means there no "crispness" menu).

SOME IDEAS:
-----------
* Maintain this project (but I know you are probably going to ignore this).
* Rip out the binary WAD parser or data streams and use them as an 
  un-bloated systems baseline for your own tools.
* Fork it and use for your projects.
* Add some rendering features -- transparency, look up / down, slopes, etc.
* Add some features -- weapons, jumping, ducking, flying, etc.
* Create a client / server based internet.
* Make this into actual 3D instead of 2.5D.

And that is all.

I dont know how many people are gonna look at Vernichtung, but i think at 
least 1 person is gonna look at it, witch is me!
Ok sorry but stay safe out there

Have Fun.

- Rafael, 8/31/2026
