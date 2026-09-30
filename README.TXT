===============================================================================
                       VERNICHTUNG ENGINE (VERSION 2)
===============================================================================

Well, Folks,
Here it is: Vernichtung at your hands!

WHAT IS IT?
-----------
Vernichtung is a clean-room reverse-engineered 2.5D software game 
engine built completely from scratch in portable C and SDL2. 

Vernichtung exists to a completely 
independent, reconstruction of the id Tech 1 architecture 
released under a fully permissive, non-viral license. (No, i dont hate Copyleft)
And to fulfill John Carmack's and other devs wishes

"The recent discussions around copilot are a good opportunity to say 
this: I wish I could have licensed the Id source code releases as BSD. 
The GPL virality wound up being a net negative, and more value would 
have come from BSD. My partners would never have gone for it, though."
- John Carmack, 7/6/2021

Many thanks to the people in CREDITS (inside of "THIRD-PARTY-NOTICES" folder) for their actual hard work.
LICENSES TEXT CAN BE FOUND IN "THIRD-PARTY-NOTICES" FOLDER

LICENSING
---------
This engine is officially released under the 3-Clause BSD License. 
See the LICENSE file for full details.

FEATURES
---------------
* Hardened WAD Parsing: Fortified binary lump, PLAYPAL, and composite texture 
  loaders built with strict array boundary checks to defensively protect the 
  heap from malformed or corrupted game data loops.
* id Tech 1 Coordinate Alignment: Core vectors locked to the native 
  horizontal and vertical tracking axes (0 degrees East, 90 degrees North).
* BSP Space Partitioning & Physics: Recursive bitwise 2D BSP tree traversal 
  paired with a responsive multi-pass sliding wall vector collision solver
* Bad news is there no "-file" (PWAD) surppot but i gonna add that later

CURRENT STATUS & ROADMAP
------------------------
The core engine loop orchestration, hardware event managers, timing clock gates, 
and basic visplane flat row-casters are fully operational, linked, and active. 
You can launch the executable, parse retail IWAD files, and freely navigate the 
map geometry in real-time.

But there a bug i am stuck: Vertical perspective texture stretching/distortion that 
i still have not found the cause of it

Future milestones include a PWAD ("-file") Surppot and clean-room specification-based MUS-to-MIDI real-time 
translation layer to feed dynamic-link audio device callbacks targeting 
Nuked-OPL3 or FluidSynth (since they are both licensed under LGPL-2.1) and so much more!

CONTRIBUTING TO VERNICHTUNG
----------------------
See the CONTRIBUTING file!

And that is all.

I don't know how many people are going to look at Vernichtung, but I think at 
least one person is going to look at it, which is me!

Ok, sorry, but stay safe out there.

Have fun.

- Rafael, 9/29/2026
