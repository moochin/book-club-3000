# Notes for Claude

## Releasing firmware

Cloud sessions can't push tags or edit the club gist, and the owner often
isn't at a computer. Once a firmware version bump is merged to `main`, send
the owner both steps below in one message, as links they can open on a
phone, with `X.Y.Z` replaced by `BC_FW_VERSION` from
`firmware/src/config.h`. Spell out the exact version so a typo doesn't tag
the wrong one.

Since 1.5.0 a unit rolls back an update that doesn't prove itself (see "The
safety net" in `firmware/README.md`). For anything beyond a small fix,
suggest trying it on the owner's own unit first: add `"only": ["Pete"]` to the
block in step 2, check that unit's ABOUT a day later, then remove `"only"`.

1. **Publish the release.** This link pre-fills the form; they tap
   **Publish release**:

       https://github.com/moochin/book-club-3000/releases/new?tag=vX.Y.Z&target=main&title=vX.Y.Z

   Publishing creates the tag. The Firmware workflow checks that the tag
   matches `BC_FW_VERSION`, builds the firmware and attaches `firmware.bin`
   and `firmware-st7789.bin`. A tag that doesn't match fails that check and
   publishes no binaries. Before step 2, wait for the build to finish
   (about two minutes).

2. **Point the units at it.** Units update only when the `firmware` block
   in the club's `bookclub.json` gist names a newer version. The gist is
   secret, so never write its address into this public repository. Send
   the owner `https://gist.github.com/moochin`, which lists their gists
   (secret ones included) when they are signed in. They open the club
   gist, tap **Edit**, set the block to the following and save:

   ```json
   "firmware": {
     "version": "X.Y.Z",
     "url": "https://github.com/moochin/book-club-3000/releases/download/vX.Y.Z/firmware.bin",
     "url_st7789": "https://github.com/moochin/book-club-3000/releases/download/vX.Y.Z/firmware-st7789.bin"
   }
   ```

Units pick it up on their next sync. See "Firmware updates" in `firmware/README.md`.
