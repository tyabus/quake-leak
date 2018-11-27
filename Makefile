CC=gcc
CFLAGS=-O2 -Wall -ffast-math -fomit-frame-pointer -fno-strength-reduce -funroll-loops
LDFLAGS=
LIBS=-lm
O=linux

OUTPUT=	xquake

all:	$(OUTPUT)

clean:
	rm -f *.o $(OUTPUT)

%.o : %.s
	$(CC) $(CFLAGS) -DELF -x assembler-with-cpp -c $<

HEADERS = asm_draw.h client.h cmd.h common.h cvar.h d_iface.h\
         d_local.h mathlib.h model.h modelgen.h net.h quakeasm.h\
         quakedef.h spritegn.h zone.h crc.h net_vcr.h wad.h sbar.h\
         world.h

PORTABLE_OBJS= \
 console.o \
 r_light.o \
 r_misc.o \
 pr_cmds.o \
 cd_null.o \
 menu.o \
 draw.o \
 cl_demo.o \
 cl_input.o \
 keys.o \
 cl_main.o \
 cl_parse.o \
 cl_tent.o \
 cmd.o \
 common.o \
 crc.o \
 cvar.o \
 d_edge.o \
 d_fill.o \
 d_init.o \
 d_modech.o \
 d_part.o \
 d_polyse.o \
 d_scan.o \
 d_sky.o \
 d_sprite.o \
 d_surf.o \
 d_zpoint.o \
 host.o \
 host_cmd.o \
 mathlib.o \
 model.o \
 net_main.o \
 net_vcr.o \
 pr_edict.o \
 pr_exec.o \
 r_aclip.o \
 r_efrag.o \
 r_alias.o \
 r_bsp.o \
 r_draw.o \
 r_edge.o \
 r_main.o \
 r_part.o \
 r_sky.o \
 r_sprite.o \
 r_surf.o \
 r_vars.o \
 sbar.o \
 screen.o \
 sv_main.o \
 sv_move.o \
 sv_phys.o \
 sv_user.o \
 view.o \
 wad.o \
 world.o \
 zone.o

LINUX_OBJS= \
 d_vars.o \
 sys_linux.o \
 net_bsd.o \
 net_dgrm.o \
 net_loop.o \
 net_udp.o \
 nonintel.o \
 snd_dma.o \
 snd_mem.o \
 snd_linux.o \
 snd_mix.o \
 d_draw.o \
 d_draw16.o \
 d_parta.o \
 d_polysa.o \
 d_scana.o\
 d_spr8.o \
 math.o \
 r_aliasa.o \
 r_drawa.o \
 r_edgea.o \
 surf16.o \
 surf8.o \
 worlda.o \
 r_aclipa.o \
 snd_mixa.o \
 sys_dosa.o

SLOWER_C_OBJS= \
 d_vars.o \
 snd_mix.o \
 nonintel.o

OBJS=$(PORTABLE_OBJS) $(LINUX_OBJS)

xquake:	$(OBJS) vid_x.o
	$(CC) $(LDFLAGS) -L/usr/X11R6/lib $(OBJS) vid_x.o -o xquake $(LIBS) -lXext -lX11

