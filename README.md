# Descent 3MASTERED

**A light remastering of Descent 3.**

Descent 3MASTERED (pronounced "threemastered"... get it?) is a new engine for
Descent 3, forked from PiccuEngine, itself downstream of the original 1.5 source
release.

This project does **not** include the commercial Descent 3 game data. A legally
obtained installation of Descent 3 is required.

## Goals

The broad, non-exclusive goals of the engine are:

1. **Produce the best-resolved, most stable image possible**, enhancing rather
   than supplanting the original art direction.
2. **Add features that improve the experience**, like 3D positional audio, Wooting
   analog keyboard support, and so on.
3. **Optionally fix obvious flaws in the base game**, erring on the side
   of caution. Vanilla's higher difficulties, for instance, are near-unplayable:
   enemies get tankier and dodgier while resources become too sparse. Switch on custom
   difficulty and those knobs come apart.
4. **Improve the user experience** (sliders drag now, new hotkeys to cycle to previous weapons, small stuff like that).

## Notes

**On resolution:** The recommended way to play 3MASTERED is at a relatively low
base resolution (eg. 720p) at least 2x SSAA. Whatever the people at
Nightdive may believe, the vast majority of old games do *not* look good at high
resolutions, which have the habit of exposing inconsistent asset quality, Descent 3
being no exception. Of course, edge aliasing and pixel crawl aren't pleasant
either. A low base resolution combined with supersampling solves both at once.

**On performance:** Many of the modern post-processing effects in 3MASTERED are
not cheap. They are often *more* expensive than similar implementations in modern
titles: 3MASTERED places a strong emphasis on stability, and composites its post
stack in a way that preserves and complements the 1999 art direction, whereas
modern games get to design art and tech together from the start. Therefore: high base resolution + supersampling + post processing is a footgun.
Pick any two. Choose all three and do not expect great framerates.

**On custom levels:** 3MASTERED is a 64-bit binary, like the official 1.5 release
but unlike Piccu. Custom Descent 3 levels may ship with 32-bit embedded code. 3MASTERED offers **experimental** backwards
compatibility via a 32-bit background process that interops with the 64-bit engine.

**On multiplayer:** Not yet well tested. Godspeed.
