# Notes for Claude

## Releasing firmware

Cloud sessions can't push tags, and the owner often isn't at a computer. After
a firmware version bump is merged to `main`, give the owner a one-tap link that
pre-fills the GitHub release form. They can open it in a phone browser:

    https://github.com/moochin/book-club-3000/releases/new?tag=vX.Y.Z&target=main&title=vX.Y.Z

Replace `X.Y.Z` with `BC_FW_VERSION` from `firmware/src/config.h`. Publishing
the release creates the tag. The Firmware workflow then checks that the tag
matches `BC_FW_VERSION`, builds the firmware and attaches the `.bin` files that
units download over the air.
