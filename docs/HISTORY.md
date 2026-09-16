# The game, and its history


Pokémon Snap was made at HAL Laboratory, with Pax Softnica assisting, and
published by Nintendo: Japan on 21 March 1999, North America in the summer
of 1999 (sources give 30 June and 26 July), Europe on 15 September 2000.

It did not begin as a Pokémon game. In 1995 a small team at HAL under
Yoichi Yamamoto, with Satoru Iwata (then HAL's president, later Nintendo's)
and Shigeru Miyamoto producing, began a photography game called *Jack and
the Beanstalk* for the 64DD disk drive, and took the name **Jack and
Beans** for itself. Iwata told the story in an Iwata Asks interview in
2010. The game, he said, "wasn't a Pokémon game, but rather a normal game
in which you took photos, but the motivation for playing the game wasn't
clear". The question of what players would want to photograph was answered
with Pokémon, in what he called a somewhat forced switch. Masanobu
Yamamoto, a designer on the team, said in the same interview that the
change "clarified what we should do and the direction we should head" and
"had saved us". Pokémon Snap was shown for the 64DD at Nintendo Space World
in November 1997; the disk version was dropped (reported in January 1999)
and the game shipped on a cartridge.

The team's name is still in the game. Its "JACK and BEANS" logo is shown in
the opening beside HAL's and Nintendo's, and the staff roll opens with it,
under "POKéMON SNAP Staff" and above the directors (the credits table is
`src/credits/A94940.c` in the decompilation). That is the name the port's
author took, and why.

Yoichi Yamamoto, Koji Inokuchi and Akira Takeshima directed; Iwata, Miyamoto
and Kenji Miki produced; Ikuko Mimori wrote the music. Sixty-three of the
first 151 Pokémon appear, across seven courses: Beach, Tunnel, Volcano, River,
Cave, Valley and Rainbow Cloud. The game sold more than 1.5 million copies by
the end of 1999, was the best-selling Nintendo 64 game in the United States
that year, and took the Interactive Achievement Award for console children's
and family title of the year.

Two things about the original release shaped this port. The first was the
kiosk. In 1999 Nintendo put Pokémon Snap Station kiosks into Blockbuster
Video stores across North America (the deal was announced in May 1999 and
the stations were in stores by November), Lawson convenience stores in
Japan, and Myer department stores in Australia (Toys "R" Us there too, by
one account), and the promotion ran until late 2000. Each was a Nintendo 64
in a blue child-height cabinet with a slot for the player's own cartridge, a
Canon photo printer on controller port 4, and a Gemco card reader. A player
bought print credit on one of five Pokémon smart cards (Bulbasaur,
Charmander, Squirtle, Pikachu and Jigglypuff), brought a save in, and left
with a sheet of sixteen postage-stamp stickers of their own photos, for
three dollars or 300 yen; Blockbuster ran a "Take Your Best Shot" contest
around them. About 4,500 units were built, by the Arcade Museum's count;
when the promotion ended most were scrapped or turned into demo units for
other games, Nintendo recalled some, and a working one is a rarity. The
cartridge's code for the printer was recovered without a station by James
Chambers in 2021, from the ROM, a debugger and a controller-bus tool of his
own, and it matches the decompilation; the port emulates the device ([SNAP-STATION.md](SNAP-STATION.md)).

The second was the re-release. When Nintendo brought the game to the Wii's
Virtual Console in December 2007 (Wii U in 2016 and 2017, Nintendo Switch
Online on 24 June 2022), it replaced the kiosk with saving photos to the
Wii Message Board, from which they could go to an SD card or to friends
(the Wii U version sent them to Miiverse instead). It also recoloured Jynx
from black to purple, as it had in its other early Pokémon re-releases.
The port's photo export and Jynx Recolor options are those two changes,
reproduced and off by default.

The port could not exist without the
[decompilation](https://github.com/ethteck/pokemonsnap), the community's
years of work turning the cartridge back into readable C. Every statement
in this README about the game's own behaviour was checked there.

Sources:

* [Wikipedia](https://en.wikipedia.org/wiki/Pok%C3%A9mon_Snap).
* [Iwata Asks: Kirby's Epic Yarn, part 4](https://www.nintendo.com/en-gb/Iwata-Asks/Iwata-Asks-Kirby-s-Epic-Yarn/Iwata-Asks-Kirby-s-Epic-Yarn/4-Surprise-Fun-and-Warmth/4-Surprise-Fun-and-Warmth-207100.html)
  (Nintendo, October 2010), for Iwata's and Masanobu Yamamoto's words.
* [Nintendo Life, "Pokémon Snap: The 64DD Origins Of A Picture-Perfect Spin-Off"](https://www.nintendolife.com/news/2021/04/feature_pokemon_snap_-_the_64dd_origins_of_a_picture-perfect_spin-off)
  (2021).
* [Unseen64, "Jack and the Beanstalk [N64 DD - Cancelled]"](https://www.unseen64.net/2010/10/29/jack-and-the-beanstalk-nintendo-64-dd-cancelled/)
  (2010).
* [Nintendo World Report, "Know Your Nintendo Developers: Pokémon Snap"](http://www.nintendoworldreport.com/feature/43893/know-your-nintendo-developers-pokemon-snap)
  (2017).
* [Bulbapedia](https://bulbapedia.bulbagarden.net/wiki/Pok%C3%A9mon_Snap),
  for the kiosk's cards and prices.
* [Serebii, Virtual Console changes](https://www.serebii.net/snap/virtualconsole.shtml).
* [Museum of the Game, "Pokemon Snap Station"](https://www.arcade-museum.com/Vending/pokemon-snap-station),
  for the unit count, the Gemco reader and Canon printer, the run to late
  2000, and what became of the units.
* [Nintendo Wiki, "Pokémon Snap Station"](https://nintendo.fandom.com/wiki/Pok%C3%A9mon_Snap_Station),
  for Toys "R" Us in Australia, which no other source read names.
* [Den of Geek, "How Pokemon Snap Stations Defined the Original N64 Game's Legacy"](https://www.denofgeek.com/games/pokemon-snap-original-n64-blockbuster/),
  for the November 1999 rollout and for Myer in Australia; Myer's own kiosk
  cards still turn up for sale, which is the corroboration.
* [TheGamer, "I Almost Bought A Pokemon Snap Station (Twice)"](https://www.thegamer.com/nintendo-pokemon-snap-station/)
  (2021).
* [jamchamb, "Reversing the Pokémon Snap Station without a Snap Station"](https://jamchamb.net/2021/08/17/snap-station.html)
  (2021).

## Further watching


The sources above are the ones to read; these are the ones to watch. How the
game was made, the kiosk this port emulates, what its camera never showed,
and the advertisements that sold it. Each is listed under the channel it
comes from, with that channel's own title.

* **CorruptionQuest** -- [How a Cult Classic Nearly Broke Its Developers
  (Pokémon Snap Retrospective) - CQ](https://youtu.be/sKd9-1xT8fY).
* **Leonhart** -- [Using Pokemon Snap Station For First Time In 20
  Years!](https://youtu.be/lCnvpIEVpqo) A working kiosk, printing: the
  recording the port's own printer display was measured against, frame by
  frame.
* **The Obsessive Gamer** -- [Pokemon Snap Unused Content & History | Pokemon
  Cut Content](https://youtu.be/RFSr9TiL3ME).
* **Boundary Break** -- [Off Camera Secrets | Pokemon Snap - Boundary Break
  ft. The Real Professor Oak (Stuart Zagnit)](https://youtu.be/mQLYWeQjAIw).
  The world the game's rails keep the camera away from; the port's
  Widescreen shows a little more of it.

## The commercials

* **90s Nostalgia** -- [Pokemon Snap N64 Commercial
  1999](https://youtu.be/ICv7IsCxt2E). The main spot, in full: a live-action
  safari.
* **90s Nostalgia** -- [Pokemon Snap N64 Blockbuster Contest Commercial
  1999](https://youtu.be/VQ5eX9K2GXw). The "Take Your Best Shot" contest
  Blockbuster ran around the kiosks.
* **Transmit Him** -- [Pokemon Snap, N64 (Nintendo, 2000) UK TV
  advert](https://youtu.be/FrB9hBb2O3Q). Europe waited until 15 September
  2000.
* **83Chrisaaron** -- [Pokemon Snap (Katsuhiko Wakabiki) Japanese
  Commercial](https://youtu.be/cZAd3r498cA). Japan had the game first, on 21
  March 1999.
* **83Chrisaaron** -- [Pokemon Snap (Katsuhiko Wakabiki) (Lawson Stickers)
  Japanese Commercial](https://youtu.be/dypI8_sU2_E). Lawson is the
  convenience-store chain that carried Snap Stations in Japan.
