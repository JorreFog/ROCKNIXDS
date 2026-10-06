# ROCKNIXDS: terms of use and legal notice

**Version 1** · 6 October 2026

These terms cover ROCKNIXDS (the frontend, SuperDrastic/libdsflip and the other tools in this repository) and
ROCKNIXDS Bank & Trade (the app in `bank/`). They are written in plain English. Please read them before you use
ROCKNIXDS.

## 1. In short

- ROCKNIXDS does not support, encourage or condone piracy in any form.
- Use only game data you made yourself from original cartridges or discs that you own.
- Do not download, share, sell or trade copies of games (ROM files), BIOS or firmware files.
- Nintendo, Pokémon and the other names mentioned belong to their owners. ROCKNIXDS is a fan project and is not
  affiliated with or endorsed by any of them.
- ROCKNIXDS is free software, provided without warranty. Your rights under the law of your country are not affected.

## 2. What ROCKNIXDS is, and what it is not

ROCKNIXDS is a free, open-source, non-commercial fan project. It contains no games, no ROM images, no BIOS or
firmware files, and no other copyrighted game data, and it does not tell you where to get any. It does not include
or provide any tool to remove or get around copy protection (technological protection measures).

ROCKNIXDS Bank & Trade reads and writes save files of games you already have, keeps Pokémon from those saves on your
handheld, and lets two handhelds exchange single Pokémon taken from their owners' own saves. It cannot copy, send or
receive games.

## 3. Your games: only your own

By using ROCKNIXDS you agree that:

- you will only use game data (ROM images and save files) that you have made yourself from original cartridges or
  discs that you own, for your own private and non-commercial use;
- you will only make and keep such copies where, and as far as, the law of your country allows it;
- you will not use ROCKNIXDS with pirated or unlawfully obtained copies of games, BIOS or firmware, and you will not
  download, upload, share, sell, lend or trade such copies;
- you will not use ROCKNIXDS to circumvent copy protection or other technological protection measures.

In the European Union, copyright in video games is protected under Directive 2001/29/EC (the "InfoSoc Directive"),
and the software in them also under Directive 2009/24/EC. Exceptions for private copies and back-up copies are
narrow and differ from one Member State to another. The Court of Justice of the European Union has held that the
private copying exception does not cover copies made from an unlawful source (Case C-435/12, ACI Adam), and that video
games and the technological measures protecting them are protected (Case C-355/12, Nintendo v PC Box). Downloading a
game you do not have the right to copy is therefore not made lawful by owning a handheld, by owning another copy, or
by deleting the file later. If you are unsure whether a copy is lawful where you live, do not use it.

You alone are responsible for the game data you use with ROCKNIXDS and for complying with the law that applies to
you. The developers of ROCKNIXDS do not check, store or receive your game data.

## 4. Trading Pokémon

The trade feature exchanges single Pokémon between two handhelds on the same local network, directly, without a
server. You agree:

- to trade only Pokémon from your own save files, made with your own games;
- not to trade for money or anything of monetary value, and not to use the feature commercially;
- to respect the rights and the terms of use of the games' publishers;
- that trades are between you and the other player: the developers are not a party to them, do not operate any
  service for them and cannot undo them.

The app checks Pokémon with PKHeX's legality analysis. That check is a technical aid, not a guarantee, and it is not
legal advice.

## 5. Trademarks and other rights

Nintendo, Nintendo DS, Game Boy, Game Boy Advance and Pokémon are trademarks of Nintendo. Pokémon and the names of
Pokémon species, moves, items and places are trademarks and copyrights of Nintendo, Creatures Inc., GAME FREAK inc.
and The Pokémon Company. The Pokémon pictures shown by the Bank & Trade app (taken from the PKHeX project), and the box
art, cartridge scans and screenshots the ROCKNIXDS menu can download for your games, remain the property of their
respective rights holders. Anbernic, RG DS, ROCKNIX, DraStic, PKHeX, RetroAchievements, LaunchBox and all other names,
logos and trademarks mentioned belong to their respective owners.

They are mentioned only to describe what ROCKNIXDS works with, as allowed for example by Article 14(1)(c) of
Regulation (EU) 2017/1001 on the European Union trade mark. Their use does not mean that their owners are affiliated
with, sponsor or endorse ROCKNIXDS, and ROCKNIXDS is not affiliated with, sponsored or endorsed by any of them.

If you hold rights in something included in or shown by ROCKNIXDS and believe it is used wrongly, please tell us
(section 11). We will look at it promptly and remove it where that is justified.

## 6. Your data

ROCKNIXDS has no accounts, no advertising and no analytics. The developers do not receive any data about you or your
games, except in the optional case below.

- **Trading** (Bank & Trade): when you open a trade room or a lobby, the name you chose, your handheld's ID (made from
  a key created on your handheld) and the Pokémon you list are announced to every device on your local network.
  During a trade, the other handheld also receives your network address and the Pokémon you offer. All of this goes
  directly between the devices and is not sent to the developers. Your handheld remembers the handhelds you traded with
  in `trainers.json` in its data folder; you can delete that file, and the identity key next to the settings, at any
  time.
- **Performance logs** (ROCKNIXDS menu, optional): only if you answer yes when asked, logs of game sessions (the game's
  name, frame rate, clock speeds, temperature, battery, and a random ID for your handheld, with secrets removed) are
  sent through ntfy.sh and published openly in this repository on GitHub, to improve performance. They are not used for
  anything else. You can switch this off at any time in the menu; to have published logs removed, open an issue
  (section 11).
- Features you choose to use that connect to other services (for example RetroAchievements, or the download of box
  art) are subject to those services' own terms and privacy policies.

Where personal data is processed only for your own personal use on your own devices, the General Data Protection
Regulation does not apply to it (Article 2(2)(c) GDPR). Where it does apply, you keep all your rights under it.

## 7. No warranty

ROCKNIXDS is provided free of charge, "as is", as developed by volunteers. It may contain errors. It changes save
files on your card: the Bank & Trade app makes a backup of a save before its first change in a session, but you
should keep your own backups of anything you care about.

## 8. Liability

As far as the law allows, the developers and contributors are not liable for any loss or damage arising from the use
of ROCKNIXDS, including the loss of save data, Pokémon or game progress.

Nothing in these terms excludes or limits any liability that cannot be excluded or limited under the law that
applies to you, in particular liability for intent or gross negligence, for death or personal injury, or under
mandatory product liability or consumer protection law. If you are a consumer, nothing in these terms affects the
rights you have under the mandatory law of your country of residence.

## 9. Open-source licences

ROCKNIXDS is licensed under the MIT licence, and ROCKNIXDS Bank & Trade under the GNU General Public License version 3
(see `LICENSE` and `bank/LICENSE`). Nothing in these terms restricts the rights those licences give you over the
source code. Sections 3 and 4 describe how ROCKNIXDS is meant to be used and remind you of obligations you have under
the law anyway; they add no restriction on your rights under those licences.

## 10. General

- If a provision of these terms is invalid or unenforceable, the rest of the terms stay in force, and the invalid
  provision is to be read as the closest valid provision to what it intended.
- If you are under 18, please read these terms with a parent or guardian.
- These terms may be updated. The version and date are at the top.
- These terms are not legal advice. If you are unsure about the law where you live, ask a qualified professional.

## 11. Contact

Questions, rights holders' notices and requests about published logs: open an issue at
https://github.com/JorreFog/ROCKNIXDS/issues.
