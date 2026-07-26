# Descent 3MASTERED

**A light remastering of Descent 3.**

Descent 3MASTERED (pronounced threemastered. Get it?) is a new engine for Descent 3, forked from PiccuEngine (itself downstream of the initial 1.5 source code release)

This project does **not** include the commercial Descent 3 game data. A legally
obtained installation of Descent 3 is required.

The broad, non-exsluive goals of the engine are:

1) Produce the best resolved, most stable image, enhancing rather than supplanting the original art direction.
2) Add new features that improve the game experience (eg. 3D positional audio, Wooting analog keyboard support)
3) (Optionally) fix obvious flaws in the base game, such as gross balance issues, always erring on the side of caution (eg. multi-axis custom difficulty mode)
4) Improve the user experience wherever possible (eg. draggable sliders)

## Notes

- The recommended way to play 3MASTERED is at a relatively low resolution (eg. 720p) with at least 2x SSAA. Regardless of what the people at Nightdive may think, the vast majority of old games do *not* look good at high resolutions, which have the habit of exposing inconsitent asset quality (Descent 3 being no exception). Still, edge aliasing and pixel crawl are not pleasant. A low base resolution combined with super sampling resolves both problems.
- Many of the modern post processing effects in 3MASTERED are not cheap. In fact, they are often more expensive than in modern title as 3MASTERED has a greater emphasis on stability, and composits its post process stack in a way that preserves and complements the original 1999 art direction, whereas modern games are able to take a holistic approach to art and tech. Therefore, beware: combining high base resolution, supersampling, and post processing is a footgun. You can most likely get away with any two, but do not expect great framerates if you opt for all three.
- 3MASTERED is a 64-bit binary, like the official 1.5 release, but unlike Piccu. Custom Descent 3 levels may be authored with 32-bit embedded code. Unlike 1.5 (as of this writing), 3MASTERED features (experimental) back compatibility, via a 32-bit background process that interops with the 64-bit engine.
- Multiplayer is not yet well tested.
