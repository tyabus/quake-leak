CFLAGS=-O2 -Wall -ffast-math -fomit-frame-pointer -fno-strength-reduce -funroll-loops
LDFLAGS=-lm -lXext -lX11

OUTPUT=xquake

all:	$(OUTPUT)

clean:
	rm -f src/*.o $(OUTPUT)

PORTABLE_OBJS= \
 src/console.o \
 src/r_light.o \
 src/r_misc.o \
 src/pr_cmds.o \
 src/cd_null.o \
 src/menu.o \
 src/draw.o \
 src/cl_demo.o \
 src/cl_input.o \
 src/keys.o \
 src/cl_main.o \
 src/cl_parse.o \
 src/cl_tent.o \
 src/cmd.o \
 src/common.o \
 src/crc.o \
 src/cvar.o \
 src/d_edge.o \
 src/d_fill.o \
 src/d_init.o \
 src/d_modech.o \
 src/d_part.o \
 src/d_polyse.o \
 src/d_scan.o \
 src/d_sky.o \
 src/d_sprite.o \
 src/d_surf.o \
 src/d_zpoint.o \
 src/host.o \
 src/host_cmd.o \
 src/mathlib.o \
 src/model.o \
 src/net_main.o \
 src/net_vcr.o \
 src/pr_edict.o \
 src/pr_exec.o \
 src/r_aclip.o \
 src/r_efrag.o \
 src/r_alias.o \
 src/r_bsp.o \
 src/r_draw.o \
 src/r_edge.o \
 src/r_main.o \
 src/r_part.o \
 src/r_sky.o \
 src/r_sprite.o \
 src/r_surf.o \
 src/r_vars.o \
 src/sbar.o \
 src/screen.o \
 src/sv_main.o \
 src/sv_move.o \
 src/sv_phys.o \
 src/sv_user.o \
 src/view.o \
 src/wad.o \
 src/world.o \
 src/zone.o

LINUX_OBJS= \
 src/d_vars.o \
 src/sys_linux.o \
 src/net_bsd.o \
 src/net_dgrm.o \
 src/net_loop.o \
 src/net_udp.o \
 src/nonintel.o \
 src/snd_dma.o \
 src/snd_mem.o \
 src/snd_linux.o \
 src/snd_mix.o

OBJS=$(PORTABLE_OBJS) $(LINUX_OBJS)

xquake:	$(OBJS) src/vid_x.o
	$(CC) -L/usr/X11R6/lib $(OBJS) src/vid_x.o -o xquake $(LDFLAGS)
