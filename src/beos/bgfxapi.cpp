/*         ______   ___    ___ 
 *        /\  _  \ /\_ \  /\_ \ 
 *        \ \ \L\ \\//\ \ \//\ \      __     __   _ __   ___ 
 *         \ \  __ \ \ \ \  \ \ \   /'__`\ /'_ `\/\`'__\/ __`\
 *          \ \ \/\ \ \_\ \_ \_\ \_/\  __//\ \L\ \ \ \//\ \L\ \
 *           \ \_\ \_\/\____\/\____\ \____\ \____ \ \_\\ \____/
 *            \/_/\/_/\/____/\/____/\/____/\/___L\ \/_/ \/___/
 *                                           /\____/
 *                                           \_/__/
 *
 *      Stuff for BeOS.
 *
 *      By Jason Wilkins.
 *      
 *      Windowed mode by Peter Wang.
 *
 *      Various bug fixes, windowed driver optimizations and mouse wheel
 *      support by Angelo Mottola
 *
 *      See readme.txt for copyright information.
 */

#include "bealleg.h"
#include "allegro/aintern.h"
#include "allegro/aintbeos.h"

#ifndef ALLEGRO_BEOS
#error something is wrong with the makefile
#endif


#define BE_DRAWING_THREAD_PERIOD    16000
#define BE_DRAWING_THREAD_NAME	    "drawing thread"
#define BE_DRAWING_THREAD_PRIORITY  B_REAL_TIME_DISPLAY_PRIORITY


typedef void (*BLITTER_FUNCTION)(void **src, void **dest, 
   int sx, int sy, int sw, int sh);

const BE_MODE_TABLE be_mode_table[] = {
   { 8,    640,  400, B_8_BIT_640x400    },
   { 8,    640,  480, B_8_BIT_640x480    },
   { 8,    800,  600, B_8_BIT_800x600    },
   { 8,   1024,  768, B_8_BIT_1024x768   },
   { 8,   1152,  900, B_8_BIT_1152x900   },
   { 8,   1280, 1024, B_8_BIT_1280x1024  },
   { 8,   1600, 1200, B_8_BIT_1600x1200  },
   { 15,   640,  480, B_15_BIT_640x480   },
   { 15,   800,  600, B_15_BIT_800x600   },
   { 15,  1024,  768, B_15_BIT_1024x768  },
   { 15,  1152,  900, B_15_BIT_1152x900  },
   { 15,  1280, 1024, B_15_BIT_1280x1024 },
   { 15,  1600, 1200, B_15_BIT_1600x1200 },
   { 16,   640,  480, B_16_BIT_640x480   },
   { 16,   800,  600, B_16_BIT_800x600   },
   { 16,  1024,  768, B_16_BIT_1024x768  },
   { 16,  1152,  900, B_16_BIT_1152x900  },
   { 16,  1280, 1024, B_16_BIT_1280x1024 },
   { 16,  1600, 1200, B_16_BIT_1600x1200 },
   { 32,   640,  480, B_32_BIT_640x480   },
   { 32,   800,  600, B_32_BIT_800x600   },
   { 32,  1024,  768, B_32_BIT_1024x768  },
   { 32,  1152,  900, B_32_BIT_1152x900  },
   { 32,  1280, 1024, B_32_BIT_1280x1024 },
   { 32,  1600, 1200, B_32_BIT_1600x1200 },
   { -1 }
};

static volatile bool be_gfx_initialized;

sem_id be_fullscreen_lock = -1;
sem_id be_window_lock = -1;
static int32  lock_count  = 0;

int *be_dirty_lines = NULL;
int be_mouse_z = 0;

static int refresh_rate = 70;

BeAllegroWindow	    *be_allegro_window	    = NULL;
BeAllegroView	    *be_allegro_view	    = NULL;
BeAllegroScreen	    *be_allegro_screen	    = NULL; 
 
static uint32 cmap[0x1000];
static uint32 rmap[256];
static uint32 gmap[256];
static uint32 bmap[256];
 
static int rsize, gsize, bsize;
static int rshift, gshift, bshift;



static void _be_gfx_set_truecolor_shifts()
{
   _rgb_r_shift_15 = 10;
   _rgb_g_shift_15 = 5;
   _rgb_b_shift_15 = 0;
   
   _rgb_r_shift_16 = 11;
   _rgb_g_shift_16 = 5;
   _rgb_b_shift_16 = 0;
   
   _rgb_r_shift_24 = 16;
   _rgb_g_shift_24 = 8;
   _rgb_b_shift_24 = 0;
   
   _rgb_a_shift_32 = 24;
   _rgb_r_shift_32 = 16; 
   _rgb_g_shift_32 = 8; 
   _rgb_b_shift_32 = 0;
}



/*----------------------------------------------------------------*/
/*    Colour conversion code					  */
/*----------------------------------------------------------------*/

/* Only slightly adapted from src/x/xwin.c.  Yay, Michael!  */

#define MAKE_FAST_COPY(name,stype,dtype)					        \
static void name(void **src, void **dest, int sx, int sy, int sw, int sh)	        \
{										        \
   int y, x;									        \
   for (y = sy; y < (sy + sh); y++) {						        \
      stype *s = (stype*) (src[y]) + sx;					        \
      dtype *d = (dtype*) (dest[y]) + sx;					        \
      for (x = sw - 1; x >= 0; x--) {						        \
	 *d++ = *s++;								        \
      }										        \
   }										        \
}

#define MAKE_FAST_TRUECOLOR(name,stype,dtype,rshift,gshift,bshift,rmask,gmask,bmask)    \
static void name(void **src, void **dest, int sx, int sy, int sw, int sh)	        \
{                                                                                       \
   int y, x;                                                                            \
   for (y = sy; y < (sy + sh); y++) {                                                   \
      stype *s = (stype*) (src[y]) + sx;					        \
      dtype *d = (dtype*) (dest[y]) + sx;					        \
      for (x = sw - 1; x >= 0; x--) {                                                   \
	 uint32 color = *s++;							        \
  	 *d++ = (rmap[(color >> (rshift)) & (rmask)]				        \
  		 | gmap[(color >> (gshift)) & (gmask)]                                  \
  		 | bmap[(color >> (bshift)) & (bmask)]);                                \
      }                                                                                 \
   }                                                                                    \
}

#define MAKE_FAST_TRUECOLOR24(name,dtype)                                               \
static void name(void **src, void **dest, int sx, int sy, int sw, int sh)	        \
{                                                                                       \
   int x, y;                                                                            \
   for (y = sy; y < (sy + sh); y++) {                                                   \
      uint8 *s = (uint8*) (src[y]) + 3 * sx;					        \
      dtype *d = (dtype*) (dest[y]) + sx;					        \
      for (x = sw - 1; x >= 0; s += 3, x--) {                                           \
	 *d++ = (rmap[s[2]] | gmap[s[1]] | bmap[s[0]]);		              	        \
      }                                                                                 \
   }                                                                                    \
}

#define MAKE_FAST_PALETTE(name,stype,dtype,rshift,gshift,bshift)                        \
static void name(void **src, void **dest, int sx, int sy, int sw, int sh)	        \
{                                                                                       \
   int x, y;                                                                            \
   for (y = sy; y < (sy + sh); y++) {                                                   \
      stype *s = (stype*) (src[y]) + sx;						\
      dtype *d = (dtype*) (dest[y]) + sx;						\
      for (x = sw - 1; x >= 0; x--) {                                                   \
	 unsigned long color = *s++;                                                    \
	 *d++ = cmap[((((color >> (rshift)) & 0x0F) << 8)                               \
		       | (((color >> (gshift)) & 0x0F) << 4)                            \
		       | ((color >> (bshift)) & 0x0F))];                                \
      }                                                                                 \
   }                                                                                    \
}

#define MAKE_FAST_PALETTE8_TO8(name)						        \
static void name(void **src, void **dest, int sx, int sy, int sw, int sh)	        \
{										        \
   int y, x;									        \
   for (y = sy; y < (sy + sh); y++) {						        \
      uint8 *s = (uint8*) (src[y]) + sx;					        \
      uint8 *d = (uint8*) (dest[y]) + sx;					        \
      for (x = sw - 1; x >= 0; x--) {						        \
	 *d++ = cmap[*s++];							        \
      }										        \
   }										        \
}

#define MAKE_FAST_PALETTE24(name,dtype)                                                 \
static void name(void **src, void **dest, int sx, int sy, int sw, int sh)	        \
{                                                                                       \
   int x, y;                                                                            \
   for (y = sy; y < (sy + sh); y++) {                                                   \
      uint8 *s = (uint8*) (src[y]) + 3 * sx;						\
      dtype *d = (dtype*) (dest[y]) + sx;						\
      for (x = sw - 1; x >= 0; s += 3, x--) {                                           \
	 *d++ = cmap[((((unsigned long) s[2] << 4) & 0xF00)				\
		       | ((unsigned long) s[1] & 0xF0)					\
		       | (((unsigned long) s[0] >> 4) & 0x0F))];			\
      }                                                                                 \
   }                                                                                    \
}

MAKE_FAST_PALETTE8_TO8(blit_8_to_8);
MAKE_FAST_PALETTE(blit_15_to_8,   uint16, uint8, 11, 6, 1);
MAKE_FAST_PALETTE(blit_16_to_8,   uint16, uint8, 12, 7, 1);
MAKE_FAST_PALETTE24(blit_24_to_8, uint8);
MAKE_FAST_PALETTE(blit_32_to_8,	 uint32, uint8, 20, 12, 4);

MAKE_FAST_TRUECOLOR(blit_8_to_16,    uint8,  uint16, 0,  0, 0, 0xFF, 0xFF, 0xFF);
MAKE_FAST_TRUECOLOR(blit_15_to_16,   uint16, uint16, 10, 5, 0, 0x1F, 0x1F, 0x1F);
MAKE_FAST_COPY(blit_16_to_16,	    uint16, uint16);
MAKE_FAST_TRUECOLOR24(blit_24_to_16, uint16);
MAKE_FAST_TRUECOLOR(blit_32_to_16,   uint32, uint16, 16, 8, 0, 0xFF, 0xFF, 0xFF);

MAKE_FAST_TRUECOLOR(blit_8_to_32,    uint8,  uint32, 0,  0, 0, 0xFF, 0xFF, 0xFF);
MAKE_FAST_TRUECOLOR(blit_15_to_32,   uint16, uint32, 10, 5, 0, 0x1F, 0x1F, 0x1F);
MAKE_FAST_TRUECOLOR(blit_16_to_32,   uint16, uint32, 11, 5, 0, 0x1F, 0x3F, 0x1F);
MAKE_FAST_TRUECOLOR24(blit_24_to_32, uint32);
MAKE_FAST_COPY(blit_32_to_32,	    uint32, uint32);



static BLITTER_FUNCTION blitter_table[5][3] = {
   {  
      blit_8_to_8,
      blit_8_to_16,
      blit_8_to_32
   },
   {
      blit_15_to_8,
      blit_15_to_16,
      blit_15_to_32,
   },
   {
      blit_16_to_8,
      blit_16_to_16,
      blit_16_to_32,
   },
   {
      blit_24_to_8,
      blit_24_to_16,
      blit_24_to_32,
   },
   {
      blit_32_to_8,
      blit_32_to_16,
      blit_32_to_32,
   }
};



static BLITTER_FUNCTION _be_gfx_select_blitter(int src_depth, int dest_depth)
{
   int i, j;
   
   switch (src_depth) {
      case 8: i = 0; break;
      case 15: i = 1; break;
      case 16: i = 2; break;
      case 24: i = 3; break;
      case 32: i = 4; break;
      default: i = -1; break;
   }
   
   switch (dest_depth) {
      case 8: j = 0; break;
      case 15: j = 1; break;
      case 16: j = 1; break;
      case 32: j = 2; break;
      default: j = -1; break;
   }

   if ((i < 0) || (j < 0)) {
      return NULL;
   }
   
   return blitter_table[i][j];
}



static void _be_gfx_init_conversion_shifts(int depth)
{
   switch (depth) {
      case 8:
	 rsize = 1;
	 gsize = 1;
	 bsize = 1;
	 rshift = 0;
	 gshift = 0;
	 bshift = 0;	 
	 break;
      case 15:
	 rsize = 1 << 5;
	 gsize = 1 << 5;
	 bsize = 1 << 5;
	 rshift = 10;
	 gshift = 5;
	 bshift = 0;
	 break;	 
      case 16:
	 rsize = 1 << 5;
	 gsize = 1 << 6;
	 bsize = 1 << 5;
	 rshift = 11;
	 gshift = 5;
	 bshift = 0;
	 break;
      case 32:
	 rsize = 1 << 8;
	 gsize = 1 << 8;
	 bsize = 1 << 8;
	 rshift = 16;
	 gshift = 8;
	 bshift = 0;
	 break;
   }
}



static void _be_gfx_create_mapping(unsigned long *map, int ssize, int dsize, int dshift)
{
   int i, smax, dmax;

   smax = ssize - 1;
   dmax = dsize - 1;
   for (i = 0; i < ssize; i++)
      map[i] = ((dmax * i) / smax) << dshift;
   for (; i < 256; i++)
      map[i] = map[i % ssize];
}



static void _be_gfx_create_mapping_tables(int depth)
{
   switch (depth) {
      case 8:
 	 /* Will be modified later in set_palette.  */
	 _be_gfx_create_mapping(rmap, 256, 0, 0);
	 _be_gfx_create_mapping(gmap, 256, 0, 0);
	 _be_gfx_create_mapping(bmap, 256, 0, 0);
	 break;
      case 15:
	 _be_gfx_create_mapping(rmap, 32, rsize, rshift);
	 _be_gfx_create_mapping(gmap, 32, gsize, gshift);
	 _be_gfx_create_mapping(bmap, 32, bsize, bshift);
	 break;
      case 16:
	 _be_gfx_create_mapping(rmap, 32, rsize, rshift);
	 _be_gfx_create_mapping(gmap, 64, gsize, gshift);
	 _be_gfx_create_mapping(bmap, 32, bsize, bshift);
	 break;
      case 24:
      case 32:
	 _be_gfx_create_mapping(rmap, 256, rsize, rshift);
	 _be_gfx_create_mapping(gmap, 256, gsize, gshift);
	 _be_gfx_create_mapping(bmap, 256, bsize, bshift);
	 break;
   }
}



/*----------------------------------------------------------------*/
/*    Fullscreen mode                                             */
/*----------------------------------------------------------------*/



static inline void change_focus(bool active)
{
/*   if (focus_count < 0) {
      focus_count = 0;
   }
*/
   if (active) {
//      focus_count++;
      focus_count = 1;
      if (be_gfx_initialized) {
         switch (_be_switch_mode) {
            case SWITCH_AMNESIA:
            case SWITCH_BACKAMNESIA:
               if (be_allegro_window->drawing_thread_id > 0) {
                  resume_thread(be_allegro_window->drawing_thread_id);
               }
               if (_be_switch_mode == SWITCH_BACKAMNESIA)
                  break;
            case SWITCH_PAUSE:
               be_sound_resume();
               be_time_resume();
               be_sys_resume();
               be_main_resume();
               break;
         }
      }
      be_display_switch_callback(SWITCH_IN);
   }
   else {
//      focus_count--;
      focus_count = 0;
      be_display_switch_callback(SWITCH_OUT);
      if (be_gfx_initialized) {
         switch (_be_switch_mode) {
            case SWITCH_AMNESIA:
            case SWITCH_BACKAMNESIA:
               if (be_allegro_window->drawing_thread_id > 0) {
                  suspend_thread(be_allegro_window->drawing_thread_id);
               }
               if (_be_switch_mode == SWITCH_BACKAMNESIA)
                  break;
            case SWITCH_PAUSE:
               if (be_midisynth)
                  be_midisynth->AllNotesOff(false);
               be_sound_suspend();
               be_time_suspend();
               be_sys_suspend();
               be_main_suspend();
               break;
         }
      }
   }
}



/* BeAllegroScreen::BeAllegroScreen:
 */
BeAllegroScreen::BeAllegroScreen(const char *title, uint32 space, status_t *error, bool debugging)
   : BWindowScreen(title, space, error, debugging)
{
}



/* BeAllegroScreen::ScreenConnected:
 */
void BeAllegroScreen::ScreenConnected(bool connected)
{
   if(connected) {
      change_focus(connected);      
      release_sem(be_fullscreen_lock);
   }
   else {
      acquire_sem(be_fullscreen_lock);
      change_focus(connected);
   }
}



/* BeAllegroScreen::QuitRequested:
 */
bool BeAllegroScreen::QuitRequested(void)
{
   return false;
}



/* BeAllegroScreen::MessageReceived:
 */
void BeAllegroScreen::MessageReceived(BMessage *message)
{
   switch (message->what) {
      case B_SIMPLE_DATA:
         break;
      
      case B_MOUSE_WHEEL_CHANGED:
         float dy; 
         message->FindFloat("be:wheel_delta_y", &dy);
         be_mouse_z += ((int)dy > 0 ? -1 : 1);
         break;

      default:
         BWindowScreen::MessageReceived(message);
         break;
   }
}



static inline uint32 find_gfx_mode(int w, int h, int d)
{
   int index = 0;
   while (be_mode_table[index].d > 0) {
      if(be_mode_table[index].w == w) {
         if(be_mode_table[index].h == h) {
            if(be_mode_table[index].d == d) {
               return be_mode_table[index].mode;
            }
         }
      }

      index++;
   }

   return (uint32)-1;
}



static inline bool be_sort_out_virtual_resolution(int w, int h, int *v_w, int *v_h)
{
   int32 try_v_w;
   int32 try_v_h;
   int step;

   try_v_w = MAX(w, *v_w);
   try_v_h = MAX(h, *v_h);

   if (*v_h > h) {
      if (be_allegro_screen->SetFrameBuffer(try_v_w, try_v_h) != B_OK) {
         step = (*v_h - h) / 32;
         for (int i = *v_h; i >= h; i -= step) {
            try_v_h = i;
            if (be_allegro_screen->SetFrameBuffer(try_v_w, try_v_h) == B_OK) {
               break;
            }
         }
      }
   }

   if (be_allegro_screen->SetFrameBuffer(try_v_w, try_v_h) == B_OK) {
      *v_w = try_v_w;
      *v_h = try_v_h;
      return true;
   }
   else {
      return false;
   }
}



static struct BITMAP *_be_gfx_fullscreen_init(GFX_DRIVER *drv, int w, int h, int v_w, int v_h, int color_depth, bool accel)
{
   BITMAP             *bmp;
   status_t            error;
   uint32              mode;
   graphics_card_info *gfx_card;
   frame_buffer_info  *fbuffer;
   char  path[MAXPATHLEN];
   char *exe;

   if (1
#ifdef ALLEGRO_COLOR8
       && (color_depth != 8)
#endif
#ifdef ALLEGRO_COLOR16
       && (color_depth != 15)
       && (color_depth != 16)
#endif
#ifdef ALLEGRO_COLOR24
       && (color_depth != 24)
#endif
#ifdef ALLEGRO_COLOR32
       && (color_depth != 32)
#endif
       ) {
      ustrcpy(allegro_error, get_config_text("Unsupported color depth"));
      return NULL;
   }

   if ((w == 0) && (h == 0)) {
      w = 640;
      h = 480;
   }
   mode = find_gfx_mode(w, h, color_depth);

   if (mode == (uint32)-1) {
      goto cleanup;
   }

   be_fullscreen_lock = create_sem(0, "screen lock");

   if (be_fullscreen_lock < 0) {
      goto cleanup;
   }

   lock_count = 0;

   _be_sys_get_executable_name(path, sizeof(path));
   path[sizeof(path)-1] = '\0';
   exe = get_filename(path);

   be_sys_set_display_switch_mode(SWITCH_NONE);
   
   be_allegro_screen = new BeAllegroScreen(exe, mode, &error, false);

   if(error != B_OK) {
      ustrcpy(allegro_error, get_config_text("Resolution not supported"));
      goto cleanup;
   }

   be_mouse_view = new BView(be_allegro_screen->Bounds(),
     "allegro mouse view", B_FOLLOW_ALL_SIDES, 0);
   be_allegro_screen->Lock();
   be_allegro_screen->AddChild(be_mouse_view);
   be_allegro_screen->Unlock();

   be_mouse_window = be_allegro_screen;
   be_mouse_window_mode = false;

   release_sem(be_mouse_view_attached);

   be_gfx_initialized = false;
   be_allegro_screen->Show();
   acquire_sem(be_fullscreen_lock);

   gfx_card = be_allegro_screen->CardInfo();

   if (be_allegro_screen->CanControlFrameBuffer()) {
      if (!be_sort_out_virtual_resolution(w, h, &v_w, &v_h)) {
         ustrcpy(allegro_error, get_config_text("Resolution not supported"));
         goto cleanup;
      }
   }
   else {
      v_w = MAX(w, v_w);
      v_h = MAX(h, v_h);
      if ((v_w != w) || (v_h != h)) {
      ustrcpy(allegro_error, get_config_text("Resolution not supported"));
	 goto cleanup;
      }
   }

   fbuffer  = be_allegro_screen->FrameBufferInfo();

   drv->w       = w;
   drv->h       = h;
   drv->linear  = TRUE;
   drv->vid_mem = v_w * v_h * BYTES_PER_PIXEL(color_depth);

   bmp = _make_bitmap(fbuffer->width, fbuffer->height,
            (unsigned long)gfx_card->frame_buffer, drv,
            color_depth, fbuffer->bytes_per_row);

   if(bmp == NULL) {
      ustrcpy(allegro_error, get_config_text("Not enough memory"));
      goto cleanup;
   }

   be_sync_func = NULL;
   if (accel) {
      be_gfx_fullscreen_accelerate(color_depth);
   }

#ifdef ALLEGRO_NO_ASM
   if (gfx_capabilities) {
      bmp->write_bank = be_gfx_fullscreen_accel_read_write_bank;
      bmp->read_bank  = be_gfx_fullscreen_accel_read_write_bank;
   }
   else {
      bmp->write_bank = be_gfx_fullscreen_read_write_bank;
      bmp->read_bank  = be_gfx_fullscreen_read_write_bank;
   }
   _screen_vtable.unwrite_bank = be_gfx_fullscreen_unwrite_bank;
#else
   if (gfx_capabilities) {
      bmp->write_bank = _be_gfx_fullscreen_accel_read_write_bank_asm;
      bmp->read_bank  = _be_gfx_fullscreen_accel_read_write_bank_asm;
   }
   else {
      bmp->write_bank = _be_gfx_fullscreen_read_write_bank_asm;
      bmp->read_bank  = _be_gfx_fullscreen_read_write_bank_asm;
   }
   _screen_vtable.unwrite_bank = _be_gfx_fullscreen_unwrite_bank_asm;
#endif

   _screen_vtable.acquire      = be_gfx_fullscreen_acquire;
   _screen_vtable.release      = be_gfx_fullscreen_release;

   _be_gfx_set_truecolor_shifts();
   
   be_gfx_initialized = true;
   
   release_sem(be_fullscreen_lock);

   return bmp;

   cleanup: {
      be_gfx_fullscreen_exit(NULL);
      return NULL;
   }
}



extern "C" struct BITMAP *be_gfx_fullscreen_init(int w, int h, int v_w, int v_h, int color_depth)
{
   return _be_gfx_fullscreen_init(&gfx_beos_fullscreen, w, h, v_w, v_h, color_depth, true);
}



extern "C" struct BITMAP *be_gfx_fullscreen_safe_init(int w, int h, int v_w, int v_h, int color_depth)
{
   return _be_gfx_fullscreen_init(&gfx_beos_fullscreen_safe, w, h, v_w, v_h, color_depth, false);
}



extern "C" void be_gfx_fullscreen_exit(struct BITMAP *bmp)
{
   (void)bmp;

   if (be_fullscreen_lock > 0) {
      release_sem(be_fullscreen_lock);
   }

   if (be_allegro_screen != NULL) {
      if (be_mouse_view_attached < 1) {
	 acquire_sem(be_mouse_view_attached);
      }

      be_allegro_screen->Lock();
      be_allegro_screen->Quit();

      be_allegro_screen = NULL;
   }

   if (be_fullscreen_lock > 0) {
      delete_sem(be_fullscreen_lock);
      be_fullscreen_lock = -1;
   }

   be_mouse_window   = NULL;
   be_mouse_view     = NULL;

   be_gfx_initialized = false;
   lock_count = 0;
}



#ifdef ALLEGRO_NO_ASM

extern "C" unsigned long be_gfx_fullscreen_accel_read_write_bank(BITMAP *bmp, int line)
{
   if (!(bmp->id & BMP_ID_LOCKED)) {
      be_sync_func();
      acquire_sem(be_fullscreen_lock);
      bmp->id |= (BMP_ID_LOCKED | BMP_ID_AUTOLOCK);
   }
   return (unsigned long)(bmp->line[line]);
}



extern "C" unsigned long be_gfx_fullscreen_read_write_bank(BITMAP *bmp, int line)
{
   if (!(bmp->id & BMP_ID_LOCKED)) {
      acquire_sem(be_fullscreen_lock);
      bmp->id |= (BMP_ID_LOCKED | BMP_ID_AUTOLOCK);
   }
   return (unsigned long)(bmp->line[line]);
}



extern "C" void be_gfx_fullscreen_unwrite_bank(BITMAP *bmp)
{
   if (bmp->id & BMP_AUTOLOCK) {
      bmp->id &= ~(BMP_ID_LOCKED | BMP_ID_AUTOLOCK);
      release_sem(be_fullscreen_lock);
   }
}

#endif



extern "C" void be_gfx_fullscreen_acquire(struct BITMAP *bmp)
{
   if (lock_count == 0) {
      acquire_sem(be_fullscreen_lock);
      bmp->id |= BMP_ID_LOCKED;
   }
   if (be_sync_func)
      be_sync_func();
   lock_count++;
}



extern "C" void be_gfx_fullscreen_release(struct BITMAP *bmp)
{
   lock_count--;

   if (lock_count == 0) {
      bmp->id &= ~BMP_ID_LOCKED;
      release_sem(be_fullscreen_lock);
   }
}



extern "C" void be_gfx_fullscreen_set_palette(AL_CONST struct RGB *p, int from, int to, int vsync)
{
   rgb_color colors[256];

   (void)vsync;

   for(int index = from; index <= to; index++) {
      colors[index].red   = _rgb_scale_6[p[index].r];
      colors[index].green = _rgb_scale_6[p[index].g];
      colors[index].blue  = _rgb_scale_6[p[index].b];
      colors[index].alpha = 255;
   }

   acquire_screen();

   be_allegro_screen->SetColorList(colors, from, to);

   release_screen();
}



extern "C" int be_gfx_fullscreen_scroll(int x, int y)
{
   int rv;

   acquire_screen();
   
   if (be_allegro_screen->MoveDisplayArea(x, y) != B_ERROR) {
      rv = 0;
   }
   else {
      rv = 1;
   }

   release_screen();

   be_gfx_fullscreen_vsync();

   return rv;
}



extern "C" void be_gfx_fullscreen_vsync(void)
{
   if(BScreen(be_allegro_screen).WaitForRetrace() != B_OK) {
      if (_timer_installed) {
         int start_count;

         start_count = retrace_count;

         while (start_count == retrace_count) {
         }
      }
      else {
         snooze (500000 / refresh_rate);
      }
   }
}



/*----------------------------------------------------------------*/
/*    Windowed mode				                  */
/*----------------------------------------------------------------*/


static int32 _be_gfx_window_drawing_thread(void *data) 
{
   BeAllegroWindow *w = (BeAllegroWindow *)data;

   while (!w->dying) {
      if (w->connected && w->blitter) {
         clipping_rect *rect;
         int32 height, i;
         uint32 j;

         acquire_sem(be_window_lock);
         w->locker->Lock();
         height = w->window.bottom - w->window.top + 1;
         for (i=0; i<height; i++) {
            if (be_dirty_lines[i]) {
               rect = w->rects;
               for (j=0; j<w->num_rects; j++, rect++)
                  if ((i >= rect->top - w->window.top) &&
                      (i <= rect->bottom - w->window.top)) {
                     w->blitter(w->screen_line, w->display_line,
                        (rect->left - w->window.left), i,
                        (rect->right - rect->left + 1), 1);
                  }
               be_dirty_lines[i] = 0;
            }
         }
         w->locker->Unlock();
      }      

      snooze(BE_DRAWING_THREAD_PERIOD);
   }  

   return B_OK;
}



#ifdef ALLEGRO_NO_ASM

extern "C" void be_gfx_windowed_unwrite_bank(BITMAP *bmp)
{
   release_sem(be_window_lock);
}



extern "C" unsigned long be_gfx_windowed_read_write_bank(BITMAP *bmp, int line)
{
   be_dirty_lines[bmp->y_ofs + line] = 1;
   return (unsigned long)(bmp->line[line]);
}

#endif



/* BeAllegroWindow::BeAllegroWindow:
 */
BeAllegroWindow::BeAllegroWindow(BRect frame, const char *title,
   window_look look, window_feel feel, uint32 flags, uint32 workspaces,
   uint32 v_w, uint32 v_h, uint32 color_depth)
   : BDirectWindow(frame, title, look, feel, flags, workspaces)
{
   BRect rect = Bounds();
   uint32 bytes_per_pixel, i;

   be_allegro_view = new BeAllegroView(rect, "Allegro", B_FOLLOW_ALL_SIDES, B_WILL_DRAW);
   rgb_color color = {0, 0, 0, 0};
   be_allegro_view->SetViewColor(color);
   AddChild(be_allegro_view);
   
   bytes_per_pixel = (color_depth + 7) / 8;
   
   screen_data = malloc(v_w * v_h * bytes_per_pixel);
   screen_line = (void **)malloc(v_h * sizeof(void *));
   
   for (i = 0; i < v_h; i++) {
      screen_line[i] = (char *)screen_data + (i * v_w * bytes_per_pixel);
   }

   screen_depth = color_depth;

   // we fill this in later in DirectConnected
   display_line = (void **)malloc(v_h * sizeof(void *));
   display_depth = 0;	

   num_rects = 0;
   rects     = NULL;
   
   connected = false;
   dying     = false;
   locker    = new BLocker();
   blitter   = NULL;
   
   be_dirty_lines = (int *)malloc(v_h * sizeof(int));
   
   be_window_lock = create_sem(0, "window lock");
      
   drawing_thread_id = spawn_thread(_be_gfx_window_drawing_thread, BE_DRAWING_THREAD_NAME, 
				    BE_DRAWING_THREAD_PRIORITY, (void *)this);

   resume_thread(drawing_thread_id);
}



/* BeAllegroWindow::~BeAllegroWindow:
 */
BeAllegroWindow::~BeAllegroWindow()
{
   int32 result;

   dying = true;
   release_sem(be_window_lock);
   locker->Unlock();
   Sync();
   wait_for_thread(drawing_thread_id, &result);
   drawing_thread_id = -1;
   Hide();

   delete locker;
   delete_sem(be_window_lock);
   blitter = NULL;
   connected = false;

   if (rects) {
      free(rects);
      rects = NULL;
   }
   if (display_line) {
      free(display_line);
      display_line = NULL;
   }
   if (be_dirty_lines) {   
      free(be_dirty_lines);
      be_dirty_lines = NULL;
   }
   if (screen_line) {
      free(screen_line);
      screen_line = NULL;
   }
   if (screen_data) {
      free(screen_data);
      screen_data = NULL;
   }
}



/* BeAllegroWindow::MessageReceived:
 */
void BeAllegroWindow::MessageReceived(BMessage *message)
{
   switch (message->what) {
      case B_SIMPLE_DATA:
         break;
      
      case B_MOUSE_WHEEL_CHANGED:
         float dy; 
         message->FindFloat("be:wheel_delta_y", &dy);
         be_mouse_z += ((int)dy > 0 ? -1 : 1);
         break;

      default:
         BDirectWindow::MessageReceived(message);
         break;
   }
}



/* BeAllegroWindow::DirectConnected:
 */
void BeAllegroWindow::DirectConnected(direct_buffer_info *info)
{
   size_t size;
   
   if (dying) {
      return;
   }

   locker->Lock();
   connected = false;
   
   switch (info->buffer_state & B_DIRECT_MODE_MASK) {
      case B_DIRECT_START:
	    
	 /* fallthrough */
	 
      case B_DIRECT_MODIFY: {
	    uint8 old_display_depth = display_depth;
	    uint32 stride, i;

	    switch (info->pixel_format) {
	       case B_CMAP8:  
		  display_depth = 8;  
		  break;
	       case B_RGB15:  
	       case B_RGBA15: 
		  display_depth = 15; 
		  break;
	       case B_RGB16:  
		  display_depth = 16; 
		  break;
	       case B_RGB32:  
	       case B_RGBA32: 
		  display_depth = 32; 
		  break;
	       default:	      
		  display_depth = 0;  
		  break;
	    }
	    
	    if (!display_depth) {
	       break;  
	    }

	    if (old_display_depth != display_depth) {
	       _be_gfx_init_conversion_shifts(display_depth);

	       // FIXME move this somewhere else
	       if (display_depth == 8) {
		  BScreen screen(be_allegro_window);
		  int r, g, b;
      
		  /* Adjust cmap to system-wide palette. */
		  for (r = 0; r < 16; r++) {
		     for (g = 0; g < 16; g++) {
			for (b = 0; b < 16; b++) {
			   cmap[(r << 8) | (g << 4) | b] = screen.IndexForColor(r << 4, g << 4, b << 4);
			}
		     }
		  } 
	       }

	       _be_gfx_create_mapping_tables(screen_depth);
	       blitter = _be_gfx_select_blitter(screen_depth, display_depth);
	    }
	 			       
	    if (rects) {
	       free(rects);
	    }

	    num_rects = info->clip_list_count;
	    size = num_rects * (sizeof *rects);
	    rects = (clipping_rect *)malloc(num_rects * size);

	    if (rects) {
	       memcpy(rects, info->clip_list, size);
	    }

	    window = info->window_bounds;
	    stride = info->bytes_per_row;
	    screen_height = window.bottom - window.top + 1;
      
	    for (i = 0; i < screen_height; i++) {
	       display_line[i] = (uint8 *)info->bits + ((i + window.top) * stride) 
			    + (window.left * ((display_depth + 7) / 8));
		   be_dirty_lines[i] = 1;
	    }

        connected = true;
	 }
	 break;

      case B_DIRECT_STOP:
	 break;
   }
  
   if (screen_depth == 8)
      be_gfx_windowed_set_palette(_current_palette, 0, 255, 0);
   else
      release_sem(be_window_lock);

   locker->Unlock();
}



/* BeAllegroWindow::WindowActivated:
 */
void BeAllegroWindow::WindowActivated(bool active)
{
   change_focus(active);
   BDirectWindow::WindowActivated(active);
}



/* BeAllegroWindow::QuitRequested:
 */
bool BeAllegroWindow::QuitRequested(void)
{
    return false;
}



static struct BITMAP *_be_gfx_windowed_init(GFX_DRIVER *drv, int w, int h, int v_w, int v_h, int color_depth, int direct)
{
   BITMAP *bmp;
   int bpp;
   char path[MAXPATHLEN];
   char *exe;

   if (1
#ifdef ALLEGRO_COLOR8
       && (color_depth != 8)
#endif
#ifdef ALLEGRO_COLOR16
       && (color_depth != 15)
       && (color_depth != 16)
#endif
#ifdef ALLEGRO_COLOR24
       && (color_depth != 24)
#endif
#ifdef ALLEGRO_COLOR32
       && (color_depth != 32)
#endif
       ) {
      ustrcpy(allegro_error, get_config_text("Unsupported color depth"));
      return NULL;
   }

   if ((!v_w) && (!v_h)) {
      v_w = w;
      v_h = h;
   }

   if ((w != v_w) || (h != v_h)) {
      ustrcpy(allegro_error, get_config_text("Unsupported virtual resolution"));
      return NULL;
   }
   
   _be_sys_get_executable_name(path, sizeof(path));
   path[sizeof(path)-1] = '\0';
   exe = get_filename(path);

   be_sys_set_display_switch_mode(SWITCH_PAUSE);

   be_allegro_window = new BeAllegroWindow(BRect(0, 0, w-1, h-1), exe,
			      B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
			      B_NOT_RESIZABLE | B_NOT_ZOOMABLE,
			      B_CURRENT_WORKSPACE, v_w, v_h, color_depth);

   if (!be_allegro_window->SupportsWindowMode()) {
      ustrcpy(allegro_error, get_config_text("Windowed mode not supported"));
      goto cleanup;
   }

   be_mouse_view = new BView(be_allegro_window->Bounds(),
			     "allegro mouse view", B_FOLLOW_ALL_SIDES, 0);
   be_allegro_window->Lock();
   be_allegro_window->AddChild(be_mouse_view);
   be_allegro_window->Unlock();

   be_mouse_window = be_allegro_window;
   be_mouse_window_mode = true;

   release_sem(be_mouse_view_attached);

   be_gfx_initialized = false;
   
   be_allegro_window->MoveTo(6, 25);
   be_allegro_window->Show();

   bpp = (color_depth + 7) / 8;

   drv->w       = w;
   drv->h       = h;
   drv->linear	= TRUE;
   drv->vid_mem	= bpp * v_w * v_h;
   
   bmp = _make_bitmap(v_w, v_h, (unsigned long)be_allegro_window->screen_data, drv, 
		      color_depth, v_w * bpp);
  
   if (!bmp) {
      ustrcpy(allegro_error, get_config_text("Not enough memory"));
      goto cleanup;
   }

#ifdef ALLEGRO_NO_ASM
   bmp->read_bank = be_gfx_windowed_read_write_bank;
   bmp->write_bank = be_gfx_windowed_read_write_bank;
   _screen_vtable.unwrite_bank = _be_gfx_windowed_unwrite_bank;
#else
   bmp->read_bank = _be_gfx_windowed_read_write_bank_asm;
   bmp->write_bank = _be_gfx_windowed_read_write_bank_asm;
   _screen_vtable.unwrite_bank = _be_gfx_windowed_unwrite_bank_asm;
#endif
   _screen_vtable.acquire = be_gfx_windowed_acquire;
   _screen_vtable.release = be_gfx_windowed_release;

   _be_gfx_set_truecolor_shifts();

   be_gfx_initialized = true;

   return bmp;

   cleanup: {
      be_gfx_windowed_exit(NULL);
      return NULL;
   }
}



extern "C" struct BITMAP *be_gfx_windowed_init(int w, int h, int v_w, int v_h, int color_depth)
{
   return _be_gfx_windowed_init(&gfx_beos_windowed, w, h, v_w, v_h, color_depth, true);
}



extern "C" void be_gfx_windowed_exit(struct BITMAP *bmp)
{
   (void)bmp;

   be_gfx_initialized = false;

   if (be_allegro_window) {
      if (be_mouse_view_attached < 1) {
	 acquire_sem(be_mouse_view_attached);
      }

      be_allegro_window->Lock();
      be_allegro_window->Quit();

      be_allegro_window = NULL;
   }
   be_mouse_window   = NULL;	    
   be_mouse_view     = NULL;
}



extern "C" void be_gfx_windowed_acquire(struct BITMAP *bmp)
{
   if (!(bmp->id & BMP_ID_LOCKED)) {
      be_allegro_window->locker->Lock();
      bmp->id |= BMP_ID_LOCKED;
   }
}



extern "C" void be_gfx_windowed_release(struct BITMAP *bmp)
{
   if (bmp->id & BMP_ID_LOCKED) {
      bmp->id &= ~BMP_ID_LOCKED;
      be_allegro_window->locker->Unlock();
   }
   release_sem(be_window_lock);
}



extern "C" void be_gfx_windowed_vsync(void)
{
   if (BScreen(be_allegro_window).WaitForRetrace() != B_OK) {
      if (_timer_installed) {
         int start_count;

         start_count = retrace_count;

         while (start_count == retrace_count) {
         }
      }
      else {
         snooze (500000 / refresh_rate);
      }
   }
} 



static void _be_gfx_set_truecolor_colors(AL_CONST PALETTE p, int from, int to)
{
   int i, rmax, gmax, bmax;

   rmax = rsize - 1;
   gmax = gsize - 1;
   bmax = bsize - 1;
   for (i = from; i <= to; i++) {
      rmap[i] = (((p[i].r & 0x3F) * rmax) / 0x3F) << rshift;
      gmap[i] = (((p[i].g & 0x3F) * gmax) / 0x3F) << gshift;
      bmap[i] = (((p[i].b & 0x3F) * bmax) / 0x3F) << bshift;
   }
}



static void _be_gfx_set_palette8_colors(AL_CONST PALETTE p, int from, int to)
{
   BScreen screen(be_allegro_window);
   int i;
   
   for (i = from; i <= to; i++) {
      cmap[i] = screen.IndexForColor(p[i].r << 2, p[i].g << 2, p[i].b << 2);
   }
}



static void _be_gfx_set_palette_colors(AL_CONST PALETTE p, int from, int to)
{
   int i;
   
   for (i = from; i <= to; i++) {
      rmap[i] = (((p[i].r & 0x3F) * 15) / 0x3F) << 8;
      gmap[i] = (((p[i].g & 0x3F) * 15) / 0x3F) << 4;
      bmap[i] = (((p[i].b & 0x3F) * 15) / 0x3F);
   }
}



extern "C" void be_gfx_windowed_set_palette(AL_CONST struct RGB *p, int from, int to, int vsync)
{
   uint32 i;
   
   if (vsync) {
      be_gfx_windowed_vsync();
   }
   
   if (be_allegro_window->display_depth > 8) {
      _be_gfx_set_truecolor_colors(p, from, to);
   }
   else {
      if (be_allegro_window->screen_depth == 8) {
	 _be_gfx_set_palette8_colors(p, from, to);
      }
      else {
	 _be_gfx_set_palette_colors(p, from, to);
      }
   }
   for (i=0; i<be_allegro_window->screen_height; i++)
      be_dirty_lines[i] = 1;
   release_sem(be_window_lock);
}
