#pragma once
#if 1  // set to 1 to enable content (LVGL's own convention for this file)

// Minimal, hand-pruned LVGL 9.5.0 configuration for Sunny's UI controller
// (ELEGOO ESP32-WROOM-32E display board) -- deliberately NOT the full
// generated template. Enables only what Screen 1 bring-up (a label, a
// button, basic drawing) needs. Add more widgets/fonts/features here only
// when a specific later screen genuinely needs them -- see
// docs/DISPLAY_HARDWARE.md's "LVGL configuration" section for the
// rationale and what's deliberately left disabled.

/*====================
   COLOR SETTINGS
 *====================*/
// RGB565, 16-bit -- matches the ILI9341's native color format directly;
// no color-format conversion needed in the flush callback.
#define LV_COLOR_DEPTH 16

/*====================
   MEMORY SETTINGS
 *====================*/
// Standard malloc/free -- simplest option for first bring-up on a module
// with no PSRAM (520KB SRAM total, shared with the whole application).
// Revisit only if LVGL's own allocations become a measured problem.
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc

/*====================
   HAL SETTINGS
 *====================*/
// Tick source is fed manually via lv_tick_inc() from millis() in
// DisplayManager's update() call -- no OS/timer integration needed for a
// single-threaded Arduino loop().
#define LV_TICK_CUSTOM 0

// Default refresh/input-read periods -- LVGL's own defaults (30ms/33Hz
// draw, matches this display's practical redraw rate over a 40MHz SPI
// bus at 240x320; not a hard real-time requirement for a status/UI
// screen).
#define LV_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 30

/*====================
   FEATURE CONFIGURATION
 *====================*/
// Logging -- enabled at WARN level for bring-up visibility (LVGL will
// print its own warnings/errors to Serial via a custom print callback
// registered in DisplayManager). Drop to LV_LOG_LEVEL_NONE once bring-up
// is stable, if the serial output becomes noisy.
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

// Asserts on for bring-up (catches misuse early); can be relaxed later.
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

/*====================
   WIDGETS -- Screen 1 only needs LABEL and BUTTON; everything else is
   deliberately left UNSET rather than force-disabled.
 *====================*/
// Explicitly force-disabling individual widgets here turned out to be the
// wrong kind of "minimal": LVGL 9's widgets have internal dependencies on
// each other (e.g. lv_scale depends on lv_line even though nothing in
// this project uses either) that this project has no reason to hand-
// verify. Forcing some off while others default on produced a broken,
// inconsistent build (a real error hit during Phase 1 bring-up -- see
// docs/DISPLAY_HARDWARE.md's "LVGL configuration" section). Leaving
// unused widgets at LVGL's own internally-consistent defaults costs some
// flash (this project has 4MB, plenty of headroom -- see the memory-plan
// section) in exchange for a build that doesn't require re-deriving
// LVGL's own dependency graph by hand. Only the two widgets this screen
// actually uses are explicitly turned on below; nothing is explicitly
// forced off at the widget level.
#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1

/*====================
   THEME -- default theme only, no fancy extras
 *====================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 0
#define LV_THEME_DEFAULT_GROW 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80

/*====================
   FONTS -- one default size, enough for status text/buttons
 *====================*/
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   OTHERS -- keep off unless a specific screen needs them
 *====================*/
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0
#define LV_USE_PNG 0
#define LV_USE_BMP 0
#define LV_USE_JPG 0
#define LV_USE_GIF 0
#define LV_USE_QRCODE 0
#define LV_USE_BARCODE 0
#define LV_USE_FREETYPE 0
#define LV_USE_LOTTIE 0
#define LV_USE_RLOTTIE 0
#define LV_USE_VECTOR_GRAPHIC 0
#define LV_USE_SNAPSHOT 0
#define LV_USE_SYSMON 0
#define LV_USE_PROFILER 0
#define LV_USE_MONKEY 0
#define LV_USE_GRIDNAV 0
#define LV_USE_FRAGMENT 0
#define LV_USE_IMGFONT 0
#define LV_USE_OBSERVER 1  // lightweight, useful later for UI-state -> widget binding (Phase 10's "UI STATE" layer)

#endif  // 1 (LVGL_CONF include guard convention)
