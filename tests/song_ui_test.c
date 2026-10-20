/* SPDX-License-Identifier: GPL-3.0-only */
/* Real song screen renderer and interaction code, with a framebuffer and
 * panel/flash doubles. No device access. Writes a 240x240 PPM preview. */
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static uint16_t screen[240*240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i,j;
    assert(x+w<=240 && y+h<=240);
    for(j=0;j<h;j++) for(i=0;i<w;i++) screen[(y+j)*240+x+i]=p[j*w+i];
}
#include "../firmware/src/gfx.c"
enum {B_FX,B_SCL,B_ENV,B_LFO,B_EDIT,B_GLO,B_HOME,B_SAVE,B_ARP,B_SEQ,B_PLAY,B_REC,B_OCTDN,B_OCTUP,NB};
enum {EN_SELECT,EN_ALGO,EN_PRESET,EN_K1,EN_K2,EN_K3,EN_K4,NE};
static struct {uint8_t btn[NB];} panel;
static struct {uint8_t home,page,force,msg_t;char msg[24];} ui;
static int32_t enc[NE];
static uint32_t ready, scene_saves, order_saves, loads;
static uint32_t arrangement_ready(void) {return ready;}
static void arrangement_apply(uint32_t scene) {(void)scene;}
static uint32_t section_bars(uint32_t s) {(void)s;return 1;}
static const page_t *cur_page(void) {return &PAGES[ui.page];}
static int project_used(uint32_t i) {return (ready>>i)&1u;}
static void project_save(uint32_t i) {ready|=1u<<i;scene_saves++;}
static void project_load(uint32_t i) {(void)i;loads++;}
static void settings_save(void) {}
static void arrangement_save(void) {order_saves++;}
static void ui_message(const char *s) {str_cpy(ui.msg,s,sizeof ui.msg);ui.msg_t=40;}
static void go_home(void) {ui.home=1;}
static void open_family(uint32_t f) {(void)f;ui.home=1;}
static int32_t panel_enc(uint32_t i) {int32_t s=enc[i];enc[i]=0;return s;}
static void song_backup(void) {}
static void song_restore(void) {}
static int32_t test_section_fx[4][9];
static int32_t section_fx_get(uint32_t scene, uint32_t field) { return test_section_fx[scene & 3u][field % 9u]; }
static void section_fx_adjust(uint32_t scene, uint32_t field, int32_t steps)
{ test_section_fx[scene & 3u][field % 9u] += steps; }
/* the texts the SONG screen draws below start and end inside their canvas (cv_pset clips the rest unseen: the help
 * line "sel: FX  rec: store  save: chain", 32 characters of the 8-pixel font from x 4, ran to 260 and lost "ain"):
 * the header, the chain's entries of 4 and 8 bars, the legend and a message over it, both help lines, both FX pages.
 * Not drawn here: an entry of 62 to 64 bars, whose length text starts at 60 + w + 6 (w = bars * 120 / 64 + 4) and
 * still runs past 240 ("64 bars" from 190 to 246): SLOOP 2.7.1's own, to be fixed on the upstream side */
static uint32_t text_out;
static int32_t cv_text_in(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    if (x < 0 || x + text_w(f, s) > (int32_t)cv_w) {
        printf("song UI: \"%s\" from x %d to %d, the canvas is %u wide\n", s, (int)x, (int)(x + text_w(f, s)), (unsigned)cv_w);
        text_out++;
    }
    return cv_text(x, y, f, s, c);
}
#define cv_text cv_text_in
#include "../firmware/src/ui_song.c"
#undef cv_text
static void press(uint32_t b) {song_screen_input(1u<<panel.btn[b],0);}

int main(int argc,char **argv)
{
    uint32_t i;
    arr_defaults(&arrangement);
    for(i=0;i<NB;i++) panel.btn[i]=(uint8_t)i;
    for(i=0;i<NPAGES;i++) if(PAGES[i].scope==SC_SONG) ui.page=(uint8_t)i;
    assert(on_song_page());
    press(B_SAVE); assert(order_saves==1 && !scene_saves);
    press(B_REC); assert(scene_saves==1 && ready==1);
    press(B_REC); assert(scene_saves==1);           /* replacement needs confirmation */
    press(B_REC); assert(scene_saves==2);
    press(B_REC); fm1_ms+=3001; press(B_REC); assert(scene_saves==2);
    enc[EN_K2]=1;song_screen_input(0,0);             /* different section cancels confirmation */
    assert(arrangement.entry[0].scene==1);
    press(B_REC);assert(scene_saves==3 && ready==3);
    song.playing=1;
    press(B_REC);press(B_SAVE);press(B_OCTUP);
    enc[EN_K3]=20;song_screen_input(0,0);
    assert(scene_saves==3 && order_saves==1 && !loads && arrangement.entry[0].bars==4);
    song.playing=0;transport_req=0;
    press(B_OCTDN);assert(arrangement_enabled);
    press(B_PLAY);assert(!transport_req);           /* later scenes still empty */
    ready=15;press(B_PLAY);assert(transport_req==1);
    transport_req=0;press(B_OCTUP);assert(loads==0);   /* load: a second press confirms */
    press(B_OCTUP);assert(loads==1);
    enc[EN_K4]=99;song_screen_input(0,0);assert(arrangement.count==16);
    enc[EN_K1]=99;song_screen_input(0,0);assert(song_cursor==15);
    enc[EN_K4]=-99;song_screen_input(0,0);assert(arrangement.count==1 && song_cursor==0);
    arr_defaults(&arrangement);ready=15;ui.msg_t=0;ui.force=1;
    arrangement.entry[0]=(arr_entry_t){0,4};
    arrangement.entry[1]=(arr_entry_t){1,8};
    arrangement.entry[2]=(arr_entry_t){2,8};
    arrangement.entry[3]=(arr_entry_t){3,4};
    palette_set(2);song_screen_draw();
    if(argc>1) {
        FILE *f=fopen(argv[1],"wb");assert(f);
        fprintf(f,"P6\n240 240\n255\n");
        for(i=0;i<240*240;i++) {
            uint16_t p=swap16(screen[i]);
            uint8_t rgb[3]={(uint8_t)((p>>11)*255/31),(uint8_t)(((p>>5)&63)*255/63),(uint8_t)((p&31)*255/31)};
            fwrite(rgb,1,3,f);
        }
        fclose(f);
    }
    enc[EN_SELECT]=1; song_screen_input(0,0); assert(song_fx_page==1);
    enc[EN_K2]=2; song_screen_input(0,0); assert(test_section_fx[0][2]==2);
    press(B_FX); assert(test_section_fx[0][1]==1);
    ui.force=1; song_screen_draw();
    enc[EN_SELECT]=1; song_screen_input(0,0); assert(song_fx_page==2);
    enc[EN_K4]=50; song_screen_input(0,0); assert(test_section_fx[0][8]==50);
    press(B_FX); assert(test_section_fx[0][0]==1);
    song.playing=1; enc[EN_K4]=1; song_screen_input(0,0); assert(test_section_fx[0][8]==50);
    ui.force=1; song_screen_draw(); song.playing=0;
    enc[EN_SELECT]=-2; song_screen_input(0,0); assert(!song_fx_page);
    ui.msg_t=0; ui.force=1; song_screen_draw();      /* (the help lines under the legend, and under a message) */
    ui_message("STOP FIRST"); ui.force=1; song_screen_draw();
    assert(!text_out);
    puts("song UI: independent plan/scene save, overwrite confirmation, stopped-only changes, missing scene, bounds, the help lines, legend and FX pages inside PASS");
    return 0;
}
