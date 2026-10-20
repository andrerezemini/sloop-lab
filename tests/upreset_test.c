/* SPDX-License-Identifier: GPL-3.0-only */
/* v1 -> v2 migration, byte-packed records, parser and A/B power-loss tests. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define __attribute__(x)
#define UP_HOST 1
#include "../firmware/src/core.h"
static uint8_t nor[0x100000];
static int fail_after = -1;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst,nor+off,n); return 0; }
static int st_erase(uint32_t off) { memset(nor+off,255,4096); return 0; }
static int st_prog(uint32_t off,const void *src,uint32_t n) {
    const uint8_t *p=src;
    for(uint32_t i=0;i<n;i++) { if(fail_after==0) return -1; if(fail_after>0) fail_after--; nor[off+i]&=p[i]; }
    return 0;
}
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
static void find_bank(uint32_t b) {
    st_hdr_t h; int c=st_current(OBJ_UPRESET0+b,&h);
    const up_bank_t *p=c<0?0:(const up_bank_t *)(nor+st_sector(OBJ_UPRESET0+b,c)+ST_PAYLOAD_OFF);
    up_bank_xip[b]=p&&up_bank_ok(p,h.len)?p:0;
}
static uint32_t frame(uint8_t *a) {
    uint32_t n=0;
    a[n++]=17; a[n++]=0; memcpy(a+n,"MATRIX",7); n+=7;
    for(uint32_t i=0;i<P_COUNT;i++) { int32_t v=(int32_t)i-64; uint32_t u=(uint32_t)(v+8192); a[n++]=u&127; a[n++]=(u>>7)&127; }
    for(uint32_t i=0;i<16;i++) { a[n++]=i?60+i:0; a[n++]=i==2?4:3; }
    return n;
}
int main(void) {
    uint8_t a[640]; uint32_t n=frame(a),slot=0;
    up_rec_t r,got; up_bank_t bank;
    int16_t values[P_COUNT],def[P_COUNT];
    memset(nor,255,sizeof nor);
    assert(sizeof r==256 && sizeof bank==2568 && sizeof bank<=ST_PAYLOAD_MAX);
    assert(!up_parse(a,n,&r,&slot) && slot==17 && up_valid(&r));
    assert(r.p[P_M1AMT]==P_M1AMT-64 && r.note[0]==0 && r.flags[0]==0 && r.note[2]==0 && r.flags[2]==4);
    assert(up_parse(a,n-1,&got,&slot)==1);
    a[0]=32; assert(up_parse(a,n,&got,&slot)==1); a[0]=17;
    a[1]=NENGINES; assert(up_parse(a,n,&got,&slot)==1); a[1]=0;
    assert(!up_bank_convert(&bank,0)); assert(!up_encode(&bank.r[1],&r));
    assert(!st_save(OBJ_UPRESET0+1,&bank,sizeof bank)); find_bank(1);
    got=*up_rec(17); assert(up_valid(&got) && !memcmp(&got,&r,sizeof r));
    assert(!up_used(16)&&!up_used(18)&&!up_used(0));
    r.p[0]=128; assert(!up_encode(&bank.r[2],&r)); up_decode(&bank,2,&got); assert(got.p[0]==127);     /* clamped, */
    r.p[0]=-129; assert(!up_encode(&bank.r[2],&r)); up_decode(&bank,2,&got); assert(got.p[0]==-128);   /* not refused */
    r.p[0]=-128; assert(!up_encode(&bank.r[2],&r)); up_decode(&bank,2,&got); assert(got.p[0]==-128);
    {   /* map by count (SLOOP 1.0 .. 2.6 records): ids below the record's count keep their values, the ones a
         * newer firmware added take the defaults, E0..E7 are the record's last eight (up_params) */
        static const uint32_t olds[] = {53u, 58u, 61u, 71u, P_COUNT - 2u};
        for (uint32_t o = 0; o < 5u; o++) {
            uint32_t np = olds[o], nc = np - 8u, i;
            for (i = 0; i < P_COUNT; i++) { def[i] = (int16_t)(1000 + i); r.p[i] = (int16_t)(i < np ? 2000 + i : 7777); }
            r.np = (uint8_t)np;
            up_params(&r, values, def);
            for (i = 0; i < P_E0; i++) assert(values[i] == (i < nc ? (int16_t)(2000 + i) : def[i]));
            for (i = 0; i < 8u; i++) assert(values[P_E0 + i] == (int16_t)(2000 + nc + i));
        }
        r.np = P_COUNT;
    }
    /* Known 192-byte records from SLOOP 2.6, held in their original sectors. */
    struct legacy_rec { uint8_t used,ver,engine,np; char name[12]; int16_t p[72]; uint8_t note[16],flags[16]; };
    struct legacy_bank { uint32_t magic; uint16_t rsize,nslot; struct legacy_rec r[16]; } old={0};
    _Static_assert(sizeof(struct legacy_rec)==192,"legacy record");
    old.magic=UP_BANK_MAGIC; old.rsize=192; old.nslot=16;
    for(uint32_t j=0;j<16;j++) {
        old.r[j].used=UP_USED; old.r[j].ver=1; old.r[j].np=71; old.r[j].engine=j%12;
        memcpy(old.r[j].name,"OLD SOUND",10);
        for(uint32_t i=0;i<71;i++) old.r[j].p[i]=(int16_t)i-32;
        old.r[j].note[0]=60+j; old.r[j].flags[0]=1;
    }
    assert(!st_save(OBJ_UPRESET0,&old,sizeof old)); find_bank(0);
    assert(up_used(0)&&up_used(15)); got=*up_rec(15);
    memset(def,0,sizeof def); up_params(&got,values,def);
    assert(P_M1SRC==63&&P_E0==81&&P_COUNT==89);
    for(uint32_t i=0;i<63;i++) assert(values[i]==(int)i-32);
    for(uint32_t i=63;i<P_E0;i++) assert(values[i]==0);
    for(uint32_t i=0;i<8;i++) assert(values[P_E0+i]==(int)(63+i)-32);
    assert(!up_bank_convert(&bank,up_bank_xip[0]));
    /* Each torn v2 save must leave the full v1 bank current. */
    for(int cut=0;cut<2600;cut+=257) {
        fail_after=cut; assert(st_save(OBJ_UPRESET0,&bank,sizeof bank)); fail_after=-1;
        find_bank(0); assert(up_bank_xip[0]->rsize==192&&up_used(15));
    }
    assert(!st_save(OBJ_UPRESET0,&bank,sizeof bank)); find_bank(0);
    assert(up_bank_xip[0]->rsize==160);
    for(uint32_t i=0;i<16;i++) { got=*up_rec(i); assert(up_valid(&got)&&got.ver==2&&got.note[0]==60+i&&got.np==71); }
    assert(st_sector(OBJ_UPRESET0,0)==0xDC000u && st_sector(OBJ_UPRESET0+1,1)==0xDF000u);
    bank.rsize=256; assert(!up_bank_ok(&bank,sizeof bank));
    bank.rsize=160; bank.r[0].ver=3; up_decode(&bank,0,&got); assert(!up_valid(&got));
    {   /* a save into a bank carries over every record in use that this build cannot read: a preset of an engine it
         * lacks (an index past its engines: a FELUCCA_SLICE=1 synth build's TERRAIN, 13; a later SLOOP's) or without
         * a name stays in flash, as SLOOP 2.7.1's copy of the whole bank kept it; up_valid still hides it. A v2
         * record is kept byte for byte; a v1 one converts with every byte its layout allows. Garbage is not carried:
         * a free slot, another record version, an np its layout cannot hold (which must not make the bank unsaveable
         * either) */
        static up_bank_t src, dst;
        up_rec_t lr, back;
        up_wire_t w;
        const uint32_t keep[] = {0, 1, 2, 3, 4, 5}, drop[] = {6, 7, 8, 9, 15};
        uint32_t i, k;
        memset(&src, 0, sizeof src);
        src.magic = UP_BANK_MAGIC; src.rsize = sizeof(up_wire_t); src.nslot = UP_PER_BANK;
        assert(!up_parse(a, n, &r, &slot));
        assert(!up_encode(&src.r[0], &r));                               /* 0: one this build reads */
        lr = r; lr.engine = NENGINES; memcpy(lr.name, "ENGINE 13", 10);
        assert(!up_encode(&src.r[1], &lr));                              /* 1: the first engine past this build's */
        lr.engine = 255; assert(!up_encode(&src.r[2], &lr));             /* 2: any engine byte */
        lr = r; memset(lr.name, 0, sizeof lr.name);
        assert(!up_encode(&src.r[3], &lr));                              /* 3: no name */
        lr = r; lr.engine = NENGINES + 1u; lr.np = 4;
        assert(!up_encode(&src.r[4], &lr));                              /* 4: a short np */
        lr = r; lr.engine = NENGINES; lr.np = 60;
        assert(!up_encode(&src.r[5], &lr));
        src.r[5].p[UP_PMAX - 1u] = 9; src.r[5].reserved[0] = 0x5A; src.r[5].reserved[7] = 1;   /* 5: bytes past np */
        src.r[6] = src.r[0]; src.r[6].ver = 3;                           /* 6: another record version */
        src.r[7] = src.r[0]; src.r[7].np = UP_PMAX + 1u;                 /* 7: an np past the layout */
        src.r[8] = src.r[0]; src.r[8].used = 0x5A;                       /* 8: not in use */
        for (k = 0; k < 6u; k++) {                                       /* 1..5 hidden at read, as before */
            up_decode(&src, keep[k], &got);
            assert(up_valid(&got) == (keep[k] == 0u));
        }
        assert(!up_bank_convert(&dst, &src));
        assert(dst.magic == UP_BANK_MAGIC && dst.rsize == sizeof(up_wire_t) && dst.nslot == UP_PER_BANK);
        for (k = 0; k < 6u; k++)
            assert(!memcmp(&dst.r[keep[k]], &src.r[keep[k]], sizeof(up_wire_t)));
        memset(&w, 0, sizeof w);
        for (k = 0; k < 5u; k++)
            assert(!memcmp(&dst.r[drop[k]], &w, sizeof w));
        for (k = 0; k < 5u; k++) {                                       /* what up_encode wrote round-trips exactly */
            up_decode(&src, keep[k], &lr);
            assert(!up_encode(&w, &lr) && !memcmp(&w, &src.r[keep[k]], sizeof w));
            up_decode(&dst, keep[k], &back);
            assert(!memcmp(&back, &lr, sizeof lr));
        }
        /* through the flash, as up_put does it: a save into slot 26 of bank 1 */
        assert(!st_save(OBJ_UPRESET0 + 1, &src, sizeof src)); find_bank(1);
        assert(up_used(16) && !up_used(17) && !up_used(19) && !up_used(26));
        assert(!up_bank_convert(&bank, up_bank_xip[1]) && !up_encode(&bank.r[10], &r));
        assert(!st_save(OBJ_UPRESET0 + 1, &bank, sizeof bank)); find_bank(1);
        assert(up_used(16) && up_used(26) && !up_used(17) && !up_used(22));
        for (k = 0; k < 6u; k++)
            assert(!memcmp(&up_bank_xip[1]->r[keep[k]], &src.r[keep[k]], sizeof(up_wire_t)));
        assert(up_bank_xip[1]->r[1].engine == NENGINES && !memcmp(up_bank_xip[1]->r[1].name, "ENGINE 13", 10));

        /* a v1 bank (SLOOP 2.6 / 2.7.1) with the same kinds of record: converted, nothing lost a byte can hold */
        memset(&old, 0, sizeof old);
        old.magic = UP_BANK_MAGIC; old.rsize = 192; old.nslot = 16;
        for (k = 0; k < 6u; k++) {
            old.r[k].used = UP_USED; old.r[k].ver = 1; old.r[k].np = 71; old.r[k].engine = (uint8_t)k;
            memcpy(old.r[k].name, "OLD SOUND", 10);
            for (i = 0; i < 72u; i++) old.r[k].p[i] = (int16_t)((int32_t)(i * 7u % 256u) - 128);
            old.r[k].note[k] = (uint8_t)(60u + k); old.r[k].flags[k] = 3;
        }
        old.r[1].engine = NENGINES; old.r[2].engine = 200;               /* engines this build lacks */
        memset(old.r[3].name, 0, sizeof old.r[3].name);                  /* no name */
        old.r[4].np = 72;                                                /* every v1 parameter */
        old.r[5].np = 5;
        old.r[6] = old.r[0]; old.r[6].ver = 2;                           /* not v1: dropped */
        old.r[7] = old.r[0]; old.r[7].np = 73;                           /* past v1's 72: dropped */
        assert(!up_bank_convert(&dst, (const up_bank_t *)&old));
        for (k = 0; k < 6u; k++) {
            up_decode((const up_bank_t *)&old, k, &lr);
            up_decode(&dst, k, &back);
            assert(back.used == UP_USED && back.ver == UP_VER && back.engine == old.r[k].engine && back.np == old.r[k].np);
            assert(!memcmp(back.name, old.r[k].name, 12) && !memcmp(back.note, lr.note, 32));
            for (i = 0; i < UP_PMAX; i++) assert(back.p[i] == (i < lr.np ? lr.p[i] : 0));
            assert(up_valid(&back) == (k == 0u || k == 4u));               /* 4: a whole v1 record of a known engine */
        }
        memset(&w, 0, sizeof w);
        assert(!memcmp(&dst.r[6], &w, sizeof w) && !memcmp(&dst.r[7], &w, sizeof w) && !memcmp(&dst.r[8], &w, sizeof w));
    }
    puts("user presets: v1/v2, every slot, signed params, parser, legacy mapping and torn migration PASS");
    puts("user presets: a save keeps the bank's records this build cannot read (other engines, no name) PASS");
    return 0;
}
