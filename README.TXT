Well, Folks,
Here it is: Vernichtung at your hands!

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

No, I don't hate copyleft. I only hate GPLv3 and AGPLv3 because they go 
to the hardware level and network level. In fact, I love GPL-2.0, MPL-2.0, 
and LGPL-2.1.

Many thanks to the people in CREDITS for their hard work.

THE BAD NEWS:
-------------
But as the months passed, I lost my interest in this project. Because of 
that, I am going to release the source code completely "AS IS" for you 
all. This code is broken, very messy, and contains plenty of bugs inside 
of it -- even though I tried my absolute best to iron out the pointer 
limits and structural loops.

THE ORIGINAL VISION (What it was supposed to look like):
--------------------------------------------------------
Vernichtung was designed to be a "Chocolate-Crispy Hybrid." If I hadn't 
given up after months of trying to fix bugs, this is the architecture 
I had in mind for the engine:

* Enhanced 640x400 display resolution.
* Widescreen rendering using all available horizontal space on modern
  screens with aspect ratios up to 16:9 (while keeping classic 4:3).
* Uncapped rendering framerate with interpolation and optional vertical 
  synchronization (VSync) tied to the screen refresh rate.
* NO DOS LIMITS -- it is completely limit-removing.
* MIDI support via Nuked OPL3 (Licensed under LGPL-2.1, so make sure 
  to use Dynamic Linking if you want to add for your project).
* The rest would be Vanilla (meaning there is no "crispness" menu).

SOME IDEAS FOR THE COMMUNITY:
-----------------------------
* Maintain this project (though I know you are probably going to ignore this!).
* Rip out the binary WAD parser or data streams and use them as an 
  un-bloated systems baseline for your own tools.
* Fork it and use it as a foundation for your own engine projects.
* Finish the visual engine by adding rendering features -- transparency, looking up/down, slopes, etc.
* Add modern gameplay features -- weapons, jumping, ducking, flying, etc.
* Create a client/server-based multiplayer network.
* Overhaul the renderer to make this into actual 3D instead of 2.5D.

And that is all.

I don't know how many people are going to look at Vernichtung, but I think at 
least one person is going to look at it, which is me!

Ok, sorry, but stay safe out there.

Have fun.

- Rafael, 8/31/2026
