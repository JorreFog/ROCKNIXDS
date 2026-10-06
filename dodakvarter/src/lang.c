// lang.c: the interface in English or Swedish. Strings are written in English in the code; tr() looks up the
// Swedish when that's the language. Names of places, things and perks are Swedish in both.
#include "game.h"

static const char *SV[][2] = {
    {"PLAY", "SPELA"}, {"HIGH SCORES", "TOPPLISTA"}, {"SETTINGS", "INSTÄLLNINGAR"}, {"HOW TO PLAY", "SÅ SPELAR DU"},
    {"QUIT", "AVSLUTA"}, {"RESUME", "FORTSÄTT"}, {"QUIT RUN", "AVSLUTA RUNDAN"}, {"BACK", "TILLBAKA"},
    {"ROUND", "RUNDA"}, {"YOU SURVIVED", "DU ÖVERLEVDE"}, {"ROUNDS", "RUNDOR"}, {"ROUND ONE", "EN RUNDA"},
    {"PAUSED", "PAUS"}, {"Kills", "Döda"}, {"Score", "Poäng"}, {"Round", "Runda"}, {"Rounds", "Rundor"},
    {"Volume", "Volym"}, {"Music", "Musik"}, {"Screen shake", "Skakningar"}, {"Aim assist", "Siktstöd"},
    {"Controls", "Kontroller"}, {"Season", "Årstid"}, {"Language", "Språk"}, {"Swap A/B", "Byt A/B"},
    {"Show FPS", "Visa FPS"}, {"Touch aiming", "Sikta med pekskärm"},
    {"Off", "Av"}, {"On", "På"}, {"Low", "Låg"}, {"High", "Hög"}, {"Classic", "Klassisk"}, {"Twin buttons", "Fyrknapp"},
    {"Random", "Slumpa"}, {"Autumn", "Höst"}, {"Winter", "Vinter"}, {"Midsummer", "Midsommar"},
    {"English", "English"}, {"Svenska", "Svenska"},
    {"Common", "Vanlig"}, {"Uncommon", "Ovanlig"}, {"Rare", "Sällsynt"}, {"Epic", "Episk"}, {"Legendary", "Legendarisk"},
    {"Buy", "Köp"}, {"Ammo", "Ammo"}, {"Open", "Öppna"}, {"Clear the way", "Röj vägen"}, {"Search", "Sök"},
    {"Repair", "Laga"}, {"Take", "Ta"}, {"Turn on the power", "Slå på strömmen"}, {"Upgrade", "Uppgradera"},
    {"Mystery Box", "Lådan"}, {"Needs power", "Behöver ström"}, {"Not enough kr", "För lite kronor"},
    {"Perk limit", "Max fyra förmåner"}, {"Already have it", "Har redan"}, {"Already upgraded", "Redan uppgraderad"},
    {"POWER ON", "STRÖMMEN ÄR PÅ"}, {"MAX AMMO", "FULLT FÖRRÅD"}, {"INSTA-KILL", "INSTADÖD"},
    {"DOUBLE POINTS", "DUBBLA KRONOR"}, {"KABOOM", "KABOOM"}, {"CARPENTER", "SNICKARE"}, {"FIRE SALE", "REA"},
    {"WOLF NIGHT", "VARGNATT"}, {"THE MOOSE IS HERE", "ÄLGEN ÄR HÄR"}, {"Bag full", "Väskan är full"},
    {"Picked up", "Tog"}, {"Empty", "Tomt"}, {"Reloading", "Laddar om"}, {"No ammo", "Slut på ammo"},
    {"Box moved", "Lådan flyttade"}, {"New high score!", "Nytt rekord!"}, {"Enter your initials", "Skriv dina initialer"},
    {"Press A", "Tryck A"}, {"Press START", "Tryck START"}, {"Downed!", "Nere!"}, {"Revived", "Uppe igen"},
    {"Health", "Hälsa"}, {"Armour", "Skydd"}, {"Bag", "Väska"}, {"Perks", "Förmåner"}, {"Map", "Karta"}, {"Weapon", "Vapen"},
    {"Grenades", "Granater"}, {"Head", "Huvud"}, {"Body", "Kropp"}, {"none", "inget"}, {"Swap", "Byt"},
    {"Use", "Använd"}, {"New round", "Ny runda"}, {"Zone opened", "Nytt område"}, {"no scores yet", "inga rekord än"},
    {"Survive as many rounds as you can.", "Överlev så många rundor du kan."},
    {"a zombie roguelike in Swedish suburbia", "en zombie-roguelike i svensk förort"},
    {"Loading", "Laddar"}, {"Upgrading", "Uppgraderar"}, {"Pack-a-Punch", "Smedjan"}, {"Power", "Ström"},
    {"Wall buy", "Väggköp"}, {"Axe", "Yxa"}, {"Barricade", "Barrikad"}, {"Carpenter", "Snickare"},
    {"Self-revive", "Återupplivning"}, {"Hold", "Håll"}, {"Wolves", "Vargar"}, {"Moose", "Älg"},
    {"FPS", "FPS"}, {"Map seed", "Kartfrö"}, {"Time", "Tid"}, {"Downs", "Fall"}, {"Doors", "Dörrar"},
    {"Boxes", "Lådor"}, {"Fire", "Skjut"}, {"Interact", "Använd"}, {"Reload", "Ladda om"}, {"Switch weapon", "Byt vapen"},
    {"Knife", "Kniv"}, {"Grenade", "Granat"}, {"Use item", "Använd sak"}, {"Next item", "Nästa sak"}, {"Sprint", "Spring"},
    {"Move", "Gå"}, {"Pause", "Paus"}, {"Aim and fire", "Sikta och skjut"},
};

const char *tr(const char *en) {
    if (S.lang != LANG_SV) return en;
    for (int i = 0; i < ARRAY_LEN(SV); i++) if (!strcmp(SV[i][0], en)) return SV[i][1];
    return en;
}
