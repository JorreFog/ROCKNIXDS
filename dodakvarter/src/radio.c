// radio.c: Kert Barlsson on the radio (0.3). Now and then a box comes up on the top screen with his face and a few
// lines: the story (you wake up in a world the forces of evil have taken, and he talks you through taking it back,
// district by district, Väktare by Väktare) and tips. He talks when something happens (the run starts, a round, the
// first door, the power, a perk, the box, Vargnatt, the moose, a boss coming, a boss down, low on health or ammo),
// and between rounds when he has nothing else to say, a tip. Lines wait in a queue and come one at a time with a
// pause between; each event speaks once a run. Its state is in G (a saved run picks up where he was); the tip it's
// on and whether you've heard his introduction before are in the settings. Settings > Radio turns him off.
#include "game.h"
#include "save.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { const char *en, *sv; } Line;

/* ---------------------------------------------------------------- the story, by event (one line or a few in a row) */
enum {
    L_INTRO1, L_INTRO2, L_INTRO3, L_INTRO4, L_AGAIN1, L_AGAIN2,
    L_KILL, L_ROUND2, L_ROUND3, L_DOOR, L_DOOR2, L_POWER, L_POWER2, L_PERK, L_BOX, L_WOLF, L_MOOSE,
    L_ROUND5, L_ROUND8, L_ROUND10, L_ROUND12, L_ROUND15, L_BOSSNEAR, L_BOSSHERE, L_BOSSDOWN1, L_BOSSDOWN2,
    L_BOSSDOWN3, L_BOSSDOWN4, L_BOSSDOWN5, L_BOSSDOWN6, L_BOSSDOWN7, L_BOSSDOWN8, L_ROUND25, L_ROUND30, L_ROUND35,
    L_LOWHP, L_DOWNED, L_NOAMMO, L_PAP, L_ROUND45,
    L_STORY_COUNT
};
static const Line STORY[L_STORY_COUNT] = {
    [L_INTRO1] = { "Hello? Hello! Is this thing on? ...There you are. Awake at last! Kert Barlsson here, on the radio. Don't touch that dial.",
                   "Hallå? Hallå! Är den här på? ...Där är du ju. Äntligen vaken! Kert Barlsson här, på radion. Rör inte ratten." },
    [L_INTRO2] = { "You've been out a long time, friend. While you slept, the world went to the dogs. The dead walk. And worse things walk with them.",
                   "Du har sovit länge, du. Medan du sov gick världen åt skogen. De döda går. Och värre saker går med dem." },
    [L_INTRO3] = { "The forces of evil have taken it all. Mörkret, they call it. It woke up the old ones too: the ones from the stories. They're real.",
                   "Ondskans makter har tagit alltihop. Mörkret, kallas det. Det väckte de gamla också: de ur sagorna. De finns på riktigt." },
    [L_INTRO4] = { "But I'm a businessman, and I know a bargain: you're alive, and you've got a gun. We take it back. One street at a time.",
                   "Men jag är affärsman, och jag vet när det är ett kap: du lever, och du har ett vapen. Vi tar tillbaka det. En gata i taget." },
    [L_AGAIN1] = { "Kert here! Back on your feet again? Good. Mörkret doesn't take a day off, and neither do we.",
                   "Kert här! Uppe igen? Bra. Mörkret tar ingen ledig dag, och det gör inte vi heller." },
    [L_AGAIN2] = { "This is Radio Barlsson, still on the air. New town, same rotten neighbours. Let's get to work.",
                   "Det här är Radio Barlsson, fortfarande i sändning. Ny stad, samma ruttna grannar. Nu kör vi." },
    [L_KILL]     = { "Ha! That's the spirit. Every one you drop is money in your pocket. Kronor still spend, even at the end of the world.",
                     "Ha! Så ska det låta. Varje en du fäller ger pengar på fickan. Kronor går att handla för, även när världen går under." },
    [L_ROUND2]   = { "They come through the windows. Hold B by a broken one to nail the boards back up. Fifty kronor a board from me!",
                     "De kommer in genom fönstren. Håll B vid ett trasigt för att spika upp brädorna igen. Femtio kronor per bräda från mig!" },
    [L_ROUND3]   = { "I'm holed up in an old warehouse, transmitting on scrap and hope. There are others out there listening. They need to see you win.",
                     "Jag har gömt mig i ett gammalt lager och sänder på skrot och hopp. Det finns andra där ute som lyssnar. De behöver se dig vinna." },
    [L_DOOR]     = { "A whole street back in human hands! That's how it's done. Every district you open is one Mörkret doesn't own any more.",
                     "En hel gata tillbaka i människohänder! Så ska det gå till. Varje kvarter du öppnar är ett som Mörkret inte äger längre." },
    [L_DOOR2]    = { "Look around in the new streets: bins, cars, sheds. People left good stuff behind in a hurry.",
                     "Titta runt på de nya gatorna: tunnor, bilar, förråd. Folk lämnade fina grejer efter sig när de flydde." },
    [L_POWER]    = { "THE POWER! You turned the lights back on! I can see the street lamps from here. Do you know what that does to people's spirits?",
                     "STRÖMMEN! Du tände ljuset igen! Jag ser gatlyktorna härifrån. Vet du vad det gör för folks humör?" },
    [L_POWER2]   = { "With the power on, the perk machines work, and Smedjan too: five thousand kronor and your gun comes out something else entirely.",
                     "Med strömmen på funkar dryckesautomaterna, och Smedjan också: fem tusen kronor så kommer ditt vapen ut som något helt annat." },
    [L_PERK]     = { "Swedish food, that's what keeps a person going. Four of those at most, though. Even a stomach has its limits.",
                     "Svensk mat, det är det som håller en människa igång. Fyra stycken som mest, dock. Även en mage har sina gränser." },
    [L_BOX]      = { "Lådan! A gamble, that. I like a gamble. But when the Dalahäst shows up, it packs up and moves somewhere else.",
                     "Lådan! Ett riktigt chansartat köp. Jag gillar sånt. Men när Dalahästen dyker upp packar den ihop och flyttar." },
    [L_WOLF]     = { "Hear that howling? Vargnatt. Mörkret's wolves, fast and hungry. Find a corner, keep your back to a wall.",
                     "Hör du ylandet? Vargnatt. Mörkrets vargar, snabba och hungriga. Hitta ett hörn och håll ryggen mot en vägg." },
    [L_MOOSE]    = { "Is that... a moose? A dead moose? Don't stand in front of it. Trust me, I've done business in Småland.",
                     "Är det där... en älg? En död älg? Stå inte framför den. Lita på mig, jag har gjort affärer i Småland." },
    [L_ROUND5]   = { "I've been listening in on Mörkret's frequency. It's not just mindless. Something is giving the orders.",
                     "Jag har lyssnat på Mörkrets frekvens. Det är inte bara tanklöst. Något ger order." },
    [L_ROUND8]   = { "People are coming out of hiding, because of you. A woman in Gnesta lit a candle in her window last night. First in a year.",
                     "Folk kommer fram ur gömställena, tack vare dig. En kvinna i Gnesta tände ett ljus i fönstret i natt. Det första på ett år." },
    [L_ROUND10]  = { "Round ten and still standing! I'd put you on a poster. 'Vote for this one.' Kidding. Mostly.",
                     "Runda tio och fortfarande på benen! Jag skulle sätta dig på en affisch. 'Rösta på den här.' Skämt. Mest." },
    [L_ROUND12]  = { "Here's what I know: Mörkret has eight Väktare, wardens, old horrors from the stories. Each one holds a piece of this land.",
                     "Det här vet jag: Mörkret har åtta Väktare, gamla fasor ur sagorna. Var och en håller i en bit av det här landet." },
    [L_ROUND15]  = { "Beat the eight Väktare and Mörkret has nothing left to stand on. That's the deal. That's the whole deal.",
                     "Besegra de åtta Väktarna så har Mörkret inget kvar att stå på. Det är affären. Det är hela affären." },
    [L_BOSSNEAR] = { "Heads up! Something big is coming next round. One of the Väktare. Stock up, fill your pockets, and get somewhere open.",
                     "Se upp! Något stort kommer nästa runda. En av Väktarna. Fyll på, fyll fickorna och ta dig till ett öppet ställe." },
    [L_BOSSHERE] = { "There it is. Watch how it moves: every attack has a warning. Learn it, step aside, and hit it while it's open.",
                     "Där är den. Titta hur den rör sig: varje attack har en varning. Lär dig den, kliv undan och slå när den är öppen." },
    [L_BOSSDOWN1] = { "YOU DID IT! One Väktare down! I just heard cheering on three frequencies at once. Seven to go.",
                      "DU KLARADE DET! En Väktare nere! Jag hörde jubel på tre frekvenser samtidigt. Sju kvar." },
    [L_BOSSDOWN2] = { "Two Väktare! Mörkret is getting nervous: its chatter has gone all quiet. Six to go.",
                      "Två Väktare! Mörkret blir nervöst: det har blivit helt tyst på dess frekvens. Sex kvar." },
    [L_BOSSDOWN3] = { "Three! The night sky came back over the river today. Real stars. Five to go.",
                      "Tre! Natthimlen kom tillbaka över ån i dag. Riktiga stjärnor. Fem kvar." },
    [L_BOSSDOWN4] = { "Halfway there, friend. Four Väktare down. I've started a fan club. It has two members, and I'm both.",
                      "Halvvägs, du. Fyra Väktare nere. Jag har startat en fanklubb. Den har två medlemmar, och jag är båda." },
    [L_BOSSDOWN5] = { "Five! Mörkret is throwing everything it has at you now. That's what losing looks like. Three to go.",
                      "Fem! Mörkret kastar allt det har mot dig nu. Det är så det ser ut när man förlorar. Tre kvar." },
    [L_BOSSDOWN6] = { "Six Väktare. People are leaving their cellars. Children playing outside, in the daytime. Two to go.",
                      "Sex Väktare. Folk lämnar sina källare. Barn leker ute, på dagen. Två kvar." },
    [L_BOSSDOWN7] = { "Seven. One left. Whatever happens now, nobody's going to forget what you've done.",
                      "Sju. En kvar. Vad som än händer nu kommer ingen att glömma vad du har gjort." },
    [L_BOSSDOWN8] = { "That's all eight! Mörkret has lost its hold. The world is ours again! ...But the stragglers won't stop. Neither will we.",
                      "Det var alla åtta! Mörkret har tappat greppet. Världen är vår igen! ...Men eftersläntrarna slutar inte. Det gör inte vi heller." },
    [L_ROUND25]  = { "Twenty-five rounds. In my day you'd get a watch for that. I'll owe you one.",
                     "Tjugofem rundor. Förr fick man en klocka för sånt. Jag är skyldig dig en." },
    [L_ROUND30]  = { "Round thirty. I've stopped taking bets against you. Nobody would take the other side.",
                     "Runda trettio. Jag har slutat ta vad mot dig. Ingen ville stå på andra sidan." },
    [L_ROUND35]  = { "Still going! You're not a survivor any more, friend. You're a legend. Keep the radio on.",
                     "Fortfarande igång! Du är ingen överlevare längre, du. Du är en legend. Ha radion på." },
    [L_LOWHP]    = { "You're bleeding! Get away from them for a few seconds and you'll pull yourself together. Bandages in the bag, too.",
                     "Du blöder! Kom undan från dem några sekunder så hämtar du dig. Förband i väskan också." },
    [L_DOWNED]   = { "Up you get! That Kanelbulle saved your life. Buy another one when you can. Cheapest insurance in town.",
                     "Upp med dig! Den där kanelbullen räddade livet på dig. Köp en till när du kan. Billigaste försäkringen i stan." },
    [L_NOAMMO]   = { "Running dry? Chalk outlines on the walls are guns for sale, and buying the one you hold again refills it.",
                     "Slut på skott? Kritkonturerna på väggarna är vapen till salu, och köper du det du har igen fylls det på." },
    [L_PAP]      = { "You've got the money for Smedjan now. Five thousand. Best investment you'll ever make. I'd know.",
                     "Nu har du råd med Smedjan. Fem tusen. Bästa investeringen du någonsin gör. Jag borde veta." },
    [L_ROUND45]  = { "Forty-five rounds. I've run out of things to say, and that has never happened before. Carry on.",
                     "Fyrtiofem rundor. Jag har slut på saker att säga, och det har aldrig hänt förut. Kör på." },
};

/* ---------------------------------------------------------------- tips, between rounds, in turn across runs */
static const Line TIPS[] = {
    { "Tip from Kert: a shot to the head is a critical hit. More damage, and a hundred kronor instead of sixty.",
      "Tips från Kert: ett skott i huvudet är en kritisk träff. Mer skada, och hundra kronor i stället för sextio." },
    { "Tip from Kert: the knife pays best of all. A hundred and thirty a kill. Risky business, but business.",
      "Tips från Kert: kniven betalar bäst av allt. Hundratrettio per dödad. Riskabla affärer, men affärer." },
    { "Tip from Kert: hold R2 to lock your aim on the nearest one. Let go and hold again for the next.",
      "Tips från Kert: håll R2 för att låsa siktet på den närmaste. Släpp och håll igen för nästa." },
    { "Tip from Kert: the last one in a round always runs. Leave a slow crawler alive and you can take a breather.",
      "Tips från Kert: den sista i en runda springer alltid. Lämna en långsam krypare vid liv så kan du andas ut." },
    { "Tip from Kert: Max Ammo, Insta-Kill, Kaboom, Double Points, Fire Sale. When they drop, go and get them. Fast.",
      "Tips från Kert: Max Ammo, Insta-Kill, Kaboom, Dubbla poäng, Fire Sale. När de faller, hämta dem. Snabbt." },
    { "Tip from Kert: sprint with L. Blåbärssoppa makes you run twice as long. Running is a fine strategy. Ask me.",
      "Tips från Kert: spring med L. Blåbärssoppa gör att du orkar dubbelt så länge. Att springa är en fin strategi. Fråga mig." },
    { "Tip from Kert: helmets and vests take hits for you. Tape fixes them up. Keep a roll in the bag.",
      "Tips från Kert: hjälmar och västar tar smällarna åt dig. Tejp lagar dem. Ha en rulle i väskan." },
    { "Tip from Kert: walk them in a big circle. Train them up behind you, then turn and let them have it.",
      "Tips från Kert: led dem i en stor cirkel. Samla dem bakom dig, vänd dig om och ge dem allt." },
    { "Tip from Kert: three garden gnomes are hidden somewhere in every town. Find them all. Trust me.",
      "Tips från Kert: tre trädgårdstomtar är gömda någonstans i varje stad. Hitta alla. Lita på mig." },
    { "Tip from Kert: the elstängsel by a gap fries anything that crosses it. A thousand kronor, once the power's on.",
      "Tips från Kert: elstängslet vid en lucka grillar allt som går igenom. Tusen kronor, när strömmen är på." },
    { "Tip from Kert: Lingondricka shocks everything around you when you reload. Reload empty for the biggest bang.",
      "Tips från Kert: Lingondricka ger en stöt runt dig när du laddar om. Ladda om tomt för största smällen." },
    { "Tip from Kert: Kaviar gives you room for a third gun. More guns, more options. I always say that.",
      "Tips från Kert: Kaviar ger plats för ett tredje vapen. Fler vapen, fler möjligheter. Det säger jag alltid." },
    { "Tip from Kert: Today's Town on the title is the same for everyone today. Beat your friends' rounds. Bragging rights!",
      "Tips från Kert: Dagens stad på titeln är samma för alla i dag. Slå dina vänners rundor. Skryträtt!" },
    { "Tip from Kert: grenades on L2. Wait until they bunch up in a doorway. Then: goodbye.",
      "Tips från Kert: granater på L2. Vänta tills de klumpar ihop sig i en dörröppning. Sen: hej då." },
    { "Tip from Kert: too hard? Settings, Difficulty. No shame in Easy. I take the easy way whenever I can.",
      "Tips från Kert: för svårt? Inställningar, Svårighet. Ingen skam i Lätt. Jag tar den lätta vägen när jag kan." },
};
#define NTIPS ((int)(sizeof TIPS / sizeof TIPS[0]))

static const char *line_text(int id) {
    const Line *l = id >= 1000 ? &TIPS[(id - 1000) % NTIPS] : &STORY[id];
    return S.lang ? l->sv : l->en;
}

/* ---------------------------------------------------------------- the queue */
#define BIT_SAID(id) (1ull << (id))
static int said(int id) { return id < 64 && (G->radio.said & BIT_SAID(id)) != 0; }
static void say(int id) {                               /* once a run; the queue keeps the latest few */
    Radio *r = &G->radio;
    if (id < 1000) { if (said(id)) return; r->said |= BIT_SAID(id); }
    if (r->nq >= RADIO_QUEUE) return;
    r->q[r->nq++] = (int16_t)id;
}

#define TYPE_RATE 38.0f                                 /* letters a second */
static float hold_for(int id) { return 3.2f + strlen(line_text(id)) * 0.035f; }
static float type_for(int id) { return strlen(line_text(id)) / TYPE_RATE; }

void radio_update(float dt) {
    Radio *r = &G->radio;
    Player *p = &G->p;
    if (!S.radio || G->over) { r->cur = -1; r->nq = 0; return; }
    /* what happened: each event speaks once a run */
    if (!r->started) {                                  /* (nothing before his first words: they come first) */
        if (G->time <= 1.5f) return;
        r->started = 1;
        r->tip_base = S.radio_tip;
        int open = 0; for (int z = 0; z < G->nzones; z++) open += G->zones[z].open;
        r->zones = open;
        if (!S.radio_heard) { say(L_INTRO1); say(L_INTRO2); say(L_INTRO3); say(L_INTRO4); S.radio_heard = 1; }
        else say((G->seed & 1) ? L_AGAIN2 : L_AGAIN1);
    }
    if (p->kills > 0) say(L_KILL);
    if (G->rstate == RS_ACTIVE && G->round != r->round) {
        r->round = G->round;
        static const struct { int round, line; } at[] = { {2, L_ROUND2}, {3, L_ROUND3}, {5, L_ROUND5}, {8, L_ROUND8}, {10, L_ROUND10},
            {12, L_ROUND12}, {15, L_ROUND15}, {25, L_ROUND25}, {30, L_ROUND30}, {35, L_ROUND35}, {45, L_ROUND45} };
        for (int i = 0; i < (int)(sizeof at / sizeof at[0]); i++) if (at[i].round == G->round) say(at[i].line);
        if (G->special) say(L_WOLF);
        if (G->moose_pending) say(L_MOOSE);
    }
    if (boss_kind_for_round(G->round + 1) >= 0 && G->rstate == RS_BREAK && !said(L_BOSSNEAR)) say(L_BOSSNEAR);
    if (G->boss.on && G->z[G->boss.zi].state == ZS_CHASE) say(L_BOSSHERE);
    if (G->boss_kills > r->boss_kills) {
        r->boss_kills = G->boss_kills;
        int k = MIN(G->boss_kills, 8);
        say(L_BOSSDOWN1 + k - 1);
        r->said &= ~(BIT_SAID(L_BOSSNEAR) | BIT_SAID(L_BOSSHERE));   /* (the next boss: a warning again) */
    }
    int open = 0; for (int z = 0; z < G->nzones; z++) open += G->zones[z].open;
    if (r->started && open > r->zones) { r->zones = open; say(L_DOOR); if (said(L_DOOR) && open >= 3) say(L_DOOR2); }
    if (G->power_on) { say(L_POWER); say(L_POWER2); }
    if (p->nperks > 0) say(L_PERK);
    if (G->box_uses > 0) say(L_BOX);
    if (p->revives > 0) say(L_DOWNED);
    if (p->hp > 0 && p->hp < p->maxhp * 0.3f && !p->downed) say(L_LOWHP);
    if (G->power_on && p->kr >= 5000) say(L_PAP);
    {
        int ammo = 0; for (int j = 0; j < p->nslots; j++) if (p->w[j].def >= 0) ammo += p->w[j].mag + p->w[j].reserve;
        if (ammo == 0 && G->round > 1) say(L_NOAMMO);
    }
    /* between rounds, with nothing else to say: a tip */
    if (G->rstate == RS_BREAK && r->tip_round != G->round && r->nq == 0 && r->cur < 0 && G->round >= 2) {
        r->tip_round = G->round;
        say(1000 + (r->tip_base + r->tips) % NTIPS);        /* (from G, so a saved run says the same) */
        r->tips++;
        S.radio_tip = (r->tip_base + r->tips) % NTIPS;      /* the next run goes on from here */
    }
    /* on the air */
    if (r->cur >= 0) {
        r->t += dt;
        if (r->t > type_for(r->cur) + hold_for(r->cur)) { r->cur = -1; r->gap = 2.5f; sfx(SFX_RADIO_OFF, 0.35f, 0); }
    } else if (r->gap > 0) r->gap -= dt;
    else if (r->nq > 0) {
        r->cur = r->q[0];
        memmove(r->q, r->q + 1, sizeof r->q[0] * (size_t)(r->nq - 1)); r->nq--;
        r->t = 0;
        sfx(SFX_RADIO, 0.5f, 0);
    }
}

/* the box, on the top screen: slides in from the top (under a boss's bar), his face talking while the words come */
void radio_draw(Surf *s) {
    Radio *r = &G->radio;
    if (r->cur < 0 || !S.radio) return;
    const char *txt = line_text(r->cur);
    float tt = type_for(r->cur), end = tt + hold_for(r->cur);
    float in = MIN(1.0f, r->t / 0.18f), out = MIN(1.0f, (end - r->t) / 0.25f), k = MAX(0.0f, MIN(in, out));
    int bw = MIN(s->w - 8, 312), bh = 52, x0 = (s->w - bw) / 2;
    int top = G->boss.bar > 0 ? 28 : 3;
    int y0 = top - (int)((1 - k) * (bh + top + 2));
    rect_blend(s, x0, y0, bw, bh, 0x0a0c10, 205);
    rect_line(s, x0, y0, bw, bh, 0x5a6a7a);
    rect_line(s, x0 + 1, y0 + 1, bw - 2, bh - 2, 0x1c222a);
    /* his face: talking while the words come, a blink now and then */
    int talking = r->t < tt && ((int)(r->t * 9) & 1);
    int blink = !talking && fmodf(r->t, 3.1f) > 2.95f;
    const Img *im = art(talking ? "kert_1" : blink ? "kert_2" : "kert_0");
    rectf(s, x0 + 4, y0 + 4, 42, 44, 0x1e2a36);
    if (im) { Surf c = *s; surf_clip(&c, x0 + 4, y0 + 4, 42, 44); blit(&c, im, x0 + 5, y0 + 4, 0); }
    for (int y = y0 + 4; y < y0 + 48; y += 3) rect_blend(s, x0 + 4, y, 42, 1, 0x000000, 40);   /* a screen's lines */
    rect_line(s, x0 + 3, y0 + 3, 44, 46, 0x3a4652);
    /* the name, a light that blinks while he talks, and the words so far */
    int tx = x0 + 52;
    text(s, FONT_SMALL, tx, y0 + 5, 0xe0b040, "KERT BARLSSON");
    if (r->t < tt && ((int)(r->t * 6) & 1)) rectf(s, tx + text_w(FONT_SMALL, "KERT BARLSSON") + 4, y0 + 5, 3, 5, 0xff5a3a);
    text(s, FONT_SMALL, x0 + bw - 6 - text_w(FONT_SMALL, "RADIO"), y0 + 5, 0x4a5868, "RADIO");
    char shown[200];
    int n = (int)(r->t * TYPE_RATE), len = (int)strlen(txt);
    if (n > len) n = len;
    while (n > 0 && n < len && ((unsigned char)txt[n] & 0xc0) == 0x80) n--;   /* (not half a letter) */
    snprintf(shown, sizeof shown, "%.*s", n, txt);
    Surf c = *s; surf_clip(&c, tx, y0 + 12, bw - (tx - x0) - 5, bh - 14);
    text_wrap(&c, FONT_NORMAL, tx, y0 + 13, bw - (tx - x0) - 6, 0xe8ecf0, shown);
}

int radio_on_air(void) { return G->radio.cur >= 0; }
const char *radio_text(int id) { return line_text(id); }
int radio_lines(void) { return L_STORY_COUNT; }
int radio_tips(void) { return NTIPS; }
