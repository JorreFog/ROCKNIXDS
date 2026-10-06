# Third-party components in ROCKNIXDS Bank & Trade

| Component | What it does here | License |
|---|---|---|
| [PKHeX.Core](https://github.com/kwsch/PKHeX) by Kaphotics and the PKHeX contributors | Reads and writes the game saves, converts Pokémon between generations, checks legality | GPL-3.0-or-later |
| PKHeX's box sprites (`assets/sprites`, from PKHeX.Drawing.PokeSprite) | The Pokémon pictures | GPL-3.0-or-later (with PKHeX) |
| [Pixelify Sans](https://github.com/eifetx/Pixelify-Sans) (`assets/fonts`) | The ROCKNIXDS Pixel font | SIL Open Font License 1.1 (`assets/fonts/OFL.txt`) |
| [StbTrueTypeSharp](https://github.com/StbSharp/StbTrueTypeSharp), [StbImageSharp](https://github.com/StbSharp/StbImageSharp) | Text and PNG decoding | Public domain / MIT |
| [.NET runtime](https://github.com/dotnet/runtime) | Runs the app (included in the package) | MIT |
| [SDL2](https://libsdl.org) | Window, rendering, input (ROCKNIX's own copy) | zlib |

Because it links PKHeX.Core, the app in `bank/` is distributed under the GPL-3.0 (`LICENSE`), while the rest of ROCKNIXDS
stays MIT. Pokémon and its names are trademarks of Nintendo, Creatures and GAME FREAK; this fan-made tool is not
affiliated with them.
