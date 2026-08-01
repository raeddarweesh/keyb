#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XTest.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <librsvg-2.0/librsvg/rsvg.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <sys/wait.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <sys/time.h>
#include <time.h>
#include <png.h>
#include <glib.h>
#include <float.h>

// About window variables
#define POPUP_WIDTH 200 
#define POPUP_HEIGHT 100

// Keyboard variables
#define BTN_WIDTH 15
#define BTN_HEIGHT 15
#define BTN_PADDING 2
#define ROWS 7
#define COLS 18
#define WIN_WIDTH (COLS * (BTN_WIDTH + BTN_PADDING) + BTN_PADDING)
#define WIN_HEIGHT (ROWS * (BTN_HEIGHT + BTN_PADDING) + BTN_PADDING)

// Numeric keypad dimensions
#define NUM_COLS 4
#define NUM_ROWS 5

// Swipe variables
#define SWYPE_PATH_COLOR_R 0.0
#define SWYPE_PATH_COLOR_G 0.83
#define SWYPE_PATH_COLOR_B 0.67
#define SWIPE_PATH_THICKNESS 0.15
#define SWIPE_PATH_OPACITY 0.05
#define HIGH_SPEED_THRESHOLD 0.08
#define MAX_SWIPE_TIME 2000
#define MIN_SWIPE_DISTANCE 50.0
#define MIN_WORD_LENGTH 2

// Dictionary for word prediction
#define MAX_DICTIONARY_WORDS 500
#define MAX_WORD_LENGTH 50
#define MAX_SUGGESTIONS 4

// Throttling for flicker reduction
#define REDRAW_INTERVAL 16  // ~60 FPS

typedef enum {
    MODE_NORMAL = 0,
    MODE_AUTO_SHRINK = 1,
    MODE_RIBBON = 2,
    MODE_NUMERIC = 3
} WindowMode;

typedef enum {
    SHAPE_RECTANGLE = 0,
    SHAPE_ROUNDED_RECTANGLE = 1,
    SHAPE_CURVE_RECTANGLE = 2,
    SHAPE_CUP_RECTANGLE = 3
} ButtonShape;

ButtonShape current_button_shape = SHAPE_RECTANGLE;

typedef struct {
    char word[MAX_WORD_LENGTH];
    int length;
} DictionaryEntry;

typedef struct {
    char word[MAX_WORD_LENGTH];
    float score;
} WordSuggestion;

typedef struct {
    int x, y;
    gint64 timestamp;
    char nearest_key;
    double distance;
} SwipePoint;

typedef struct {
    char current_word[100];
    int current_word_len;
    Bool swype_active;
    char last_key;
    char start_key;
    int last_x, last_y;
    GArray *swype_path;
    gint64 last_time;
    Bool is_drawing;
    GArray *swipe_points;
    GHashTable *activated_keys;
    GArray *key_sequence;
    DictionaryEntry dictionary[MAX_DICTIONARY_WORDS];
    int dictionary_size;
    WordSuggestion suggestions[MAX_SUGGESTIONS];
    int suggestion_count;
    char last_inserted_word[MAX_WORD_LENGTH];
    Bool show_suggestions;
} SwypeState;

// XKB Monitoring 
static int xkb_event_base = 0;
static int current_xkb_group = 0;
static char current_layout_name[256] = "us";

// Theme definitions
#define NUM_THEMES 8
typedef enum {
    THEME_DARK = 0,
    THEME_LIGHT = 1,
    THEME_BLUE = 2,
    THEME_GREEN = 3,
    THEME_PURPLE = 4,
    THEME_BROWN = 5,
    THEME_MARINE = 6,
    THEME_RED = 7
} ColorTheme;

ColorTheme current_theme = THEME_DARK;

typedef struct {
    float bg_r, bg_g, bg_b;
    float btn_r, btn_g, btn_b;
    float btn_border_r, btn_border_g, btn_border_b;
    float text_r, text_g, text_b;
    float hover_r, hover_g, hover_b;
    float pressed_r, pressed_g, pressed_b;
} ThemeColors;

ThemeColors themes[NUM_THEMES] = {
    // THEME_DARK
    {0.13, 0.13, 0.13, 0.02, 0.02, 0.02, 0.01, 0.01, 0.01, 1.0, 1.0, 1.0, 0.3, 0.3, 0.7, 0.6, 0.6, 0.6},
    // THEME_LIGHT
    {0.90, 0.90, 0.90, 1.0, 1.0, 1.0, 0.909, 0.909, 0.909, 0.0, 0.0, 0.0, 0.8, 0.8, 0.7, 0.4, 0.4, 0.4},
    // THEME_BLUE
    {0.1, 0.1, 0.3, 0.15, 0.15, 0.4, 0.17, 0.17, 0.42, 0.8, 0.9, 1.0, 0.3, 0.4, 0.8, 0.5, 0.6, 0.9},
    // THEME_GREEN
    {0.88, 0.92, 0.91, 0.78, 0.82, 0.81, 0.73, 0.77, 0.76, 0.09, 0.10, 0.04, 0.58, 0.62, 0.61, 0.70, 0.75, 0.75},
    // THEME_PURPLE
    {1.00, 0.83, 0.64, 0.90, 0.73, 0.54, 0.95, 0.78, 0.59, 0.10, 0.13, 0.04, 0.85, 0.68, 0.49, 0.7, 0.5, 0.9},
    // THEME_BROWN
    {0.95, 0.67, 0.71, 0.99, 0.77, 0.81, 0.97, 0.72, 0.76, 0.25, 0.07, 0.01, 0.85, 0.47, 0.51, 0.85, 0.57, 0.61},
    // THEME_MARINE
    {0.02, 0.18, 0.26, 0.06, 0.26, 0.36, 0.06, 0.32, 0.40, 0.72, 0.91, 1.00, 0.8, 0.5, 0.3, 0.9, 0.7, 0.5},
    // THEME_RED
    {0.47, 0.55, 0.66, 0.57, 0.65, 0.76, 0.52, 0.60, 0.71, 0.0, 0.0, 0.0, 0.8, 0.3, 0.3, 0.9, 0.5, 0.5}
};

// Global variables
WindowMode current_mode = MODE_NORMAL;
Bool is_shrunk = False;
int normal_width = 0;
int normal_height = 0;
int shrunk_width = 0;
int shrunk_height = 0;
Bool is_docked_left = False;

float brightness = 1.0f;
 

int pressed_row = -1;
int pressed_col = -1;
Bool shrink_mode = False;  // For backward compatibility

Bool sticky_shift = False;
Bool sticky_ctrl = False;
Bool sticky_alt = False;
struct timeval last_shift_click_time = {0};
struct timeval last_ctrl_click_time = {0};
struct timeval last_alt_click_time = {0};
#define DOUBLE_CLICK_DELAY 300

int repeat_delay = 600;
int repeat_rate = 25;
int repeat_row = -1;
int repeat_col = -1;
Bool repeat_active = False;
struct timeval repeat_start_time;
struct timeval repeat_last_time;

Bool mouse_in_window = False;
int last_mouse_x = -1;
int last_mouse_y = -1;
int hover_row = -1;
int hover_col = -1;

// Font variables
FT_Library ft_library;
FT_Face ft_face;
cairo_font_face_t *custom_font_face = NULL;

float window_opacity = 0.92f;
Atom net_wm_window_opacity;

#define CONFIG_FILE "keyb_config.txt"

float scale_factor = 1.0f;
float scale_factor_min = 0.2f;
float scale_factor_max = 4.0f;
#define BASE_BTN_WIDTH 15
#define BASE_BTN_HEIGHT 15
#define BASE_BTN_PADDING 2
#define BASE_FONT_SIZE 12

Display *dpy;
Window win;
GC gc;
int screen;
Atom wm_delete_window;
int drag_start_x, drag_start_y;
Bool is_dragging = False;
Bool reverse_colors = True;
Bool caps_lock = False;
Bool shift_active = False;
Bool ctrl_active = False;
Bool alt_active = False;

int window_x = 100;
int window_y = 100;

Window popup_window;

// Cairo surfaces for double buffering
cairo_surface_t *main_surface = NULL;
cairo_t *main_cr = NULL;
cairo_surface_t *back_surface = NULL;
cairo_t *back_cr = NULL;

// Throttling variables
struct timeval last_redraw_time = {0};

// SVG variables
RsvgHandle *svg_handle = NULL;
cairo_surface_t *svg_surface = NULL;
cairo_t *svg_cr = NULL;

SwypeState swipe_state = {0};

typedef struct {
    const char *label;
    const char *shift_label;
    KeySym *keys;
    KeySym *shift_keys;
    int key_count;
    int width;
    Bool is_letter;
    Bool is_caps_indicator;
    Bool is_shift_indicator;
    Bool is_ctrl_indicator;  
    Bool is_alt_indicator;   
    int fontsize;
    Bool is_swipe_indicator;
    int font_option;
    int yshft;
} ButtonDef;

// Key definitions
KeySym KS_Ctrl_L = XK_Control_L;
KeySym KS_Shift_L = XK_Shift_L;
KeySym KS_Shift_R = XK_Shift_R;
KeySym KS_Alt_L = XK_Alt_L;
KeySym KS_Alt_R = XK_Alt_R;
KeySym KS_BackSpace = XK_BackSpace;
KeySym KS_Delete = XK_Delete;
KeySym KS_space = XK_space;
KeySym KS_Return = XK_Return;
KeySym KS_Home = XK_Home;
KeySym KS_End = XK_End;
KeySym KS_Page_Up = XK_Page_Up;
KeySym KS_Page_Down = XK_Page_Down;
KeySym KS_Caps_Lock = XK_Caps_Lock;
KeySym KS_Up = XK_Up;
KeySym KS_Down = XK_Down;
KeySym KS_Left = XK_Left;
KeySym KS_Right = XK_Right;
KeySym KS_Tab = XK_Tab;
KeySym KS_comma = XK_comma;
KeySym KS_period = XK_period;
KeySym KS_slash = XK_slash;
KeySym KS_backslash = XK_backslash;
KeySym KS_apostrophe = XK_apostrophe;
KeySym KS_semicolon = XK_semicolon;
KeySym KS_bracketleft = XK_bracketleft;
KeySym KS_bracketright = XK_bracketright;
KeySym KS_exclam = XK_exclam;
KeySym KS_at = XK_at;
KeySym KS_numbersign = XK_numbersign;
KeySym KS_dollar = XK_dollar;
KeySym KS_percent = XK_percent;
KeySym KS_asciicircum = XK_asciicircum;
KeySym KS_ampersand = XK_ampersand;
KeySym KS_asterisk = XK_asterisk;
KeySym KS_parenleft = XK_parenleft;
KeySym KS_parenright = XK_parenright;
KeySym KS_underscore = XK_underscore;
KeySym KS_plus = XK_plus;
KeySym KS_less = XK_less;
KeySym KS_greater = XK_greater;
KeySym KS_question = XK_question;
KeySym KS_colon = XK_colon;
KeySym KS_quotedbl = XK_quotedbl;
KeySym KS_bar = XK_bar;
KeySym KS_braceleft = XK_braceleft;
KeySym KS_braceright = XK_braceright;
KeySym KS_Super_R = XK_Super_R;
KeySym KS_Hyper_R = XK_Hyper_R;
KeySym KS_Meta_R = XK_Meta_R;
KeySym KS_Arabic_sheen = XK_Arabic_sheen;
KeySym KS_Escape = XK_Escape;
KeySym KS_F1 = XK_F1;
KeySym KS_F2 = XK_F2;
KeySym KS_F3 = XK_F3;
KeySym KS_F4 = XK_F4;
KeySym KS_F5 = XK_F5;
KeySym KS_F6 = XK_F6;
KeySym KS_F7 = XK_F7;
KeySym KS_F8 = XK_F8;
KeySym KS_F9 = XK_F9;
KeySym KS_F10 = XK_F10;
KeySym KS_F11 = XK_F11;
KeySym KS_F12 = XK_F12;

// Keypad keysyms
KeySym KS_KP_7 = XK_KP_7;
KeySym KS_KP_8 = XK_KP_8;
KeySym KS_KP_9 = XK_KP_9;
KeySym KS_KP_Divide = XK_KP_Divide;
KeySym KS_KP_4 = XK_KP_4;
KeySym KS_KP_5 = XK_KP_5;
KeySym KS_KP_6 = XK_KP_6;
KeySym KS_KP_Multiply = XK_KP_Multiply;
KeySym KS_KP_1 = XK_KP_1;
KeySym KS_KP_2 = XK_KP_2;
KeySym KS_KP_3 = XK_KP_3;
KeySym KS_KP_Subtract = XK_KP_Subtract;
KeySym KS_KP_0 = XK_KP_0;
KeySym KS_KP_Decimal = XK_KP_Decimal;
KeySym KS_KP_Add = XK_KP_Add;
KeySym KS_KP_Enter = XK_KP_Enter;

KeySym CtrlS[] = {XK_Control_L, XK_s};
KeySym CtrlX[] = {XK_Control_L, XK_x};
KeySym CtrlC[] = {XK_Control_L, XK_c};
KeySym CtrlV[] = {XK_Control_L, XK_v};
KeySym CtrlZ[] = {XK_Control_L, XK_z};
KeySym CtrlA[] = {XK_Control_L, XK_a};
KeySym CtrlN[] = {XK_Control_L, XK_n};
KeySym CtrlF[] = {XK_Control_L, XK_f};
KeySym CtrlD[] = {XK_Control_L, XK_d};
KeySym CtrlShiftZ[] = {XK_Control_L, XK_Shift_L, XK_z};
KeySym Numbers[] = {XK_1, XK_2, XK_3, XK_4, XK_5, XK_6, XK_7, XK_8, XK_9, XK_0};
KeySym ShiftNumbers[] = {XK_exclam, XK_at, XK_numbersign, XK_dollar, XK_percent, 
                        XK_asciicircum, XK_ampersand, XK_asterisk, XK_parenleft, 
                        XK_parenright, XK_asciitilde};
KeySym Symbols[] = {XK_minus, XK_plus, XK_equal, XK_apostrophe};
KeySym ShiftSymbols[] = {XK_underscore, XK_plus, XK_plus};
KeySym Qwerty1[] = {XK_q, XK_w, XK_e, XK_r, XK_t, XK_y, XK_u, XK_i, XK_o, XK_p};
KeySym Qwerty2[] = {XK_a, XK_s, XK_d, XK_f, XK_g, XK_h, XK_j, XK_k, XK_l};
KeySym Qwerty3[] = {XK_z, XK_x, XK_c, XK_v, XK_b, XK_n, XK_m};
KeySym Qwerty1U[] = {XK_Q, XK_W, XK_E, XK_R, XK_T, XK_Y, XK_U, XK_I, XK_O, XK_P};
KeySym Qwerty2U[] = {XK_A, XK_S, XK_D, XK_F, XK_G, XK_H, XK_J, XK_K, XK_L};
KeySym Qwerty3U[] = {XK_Z, XK_X, XK_C, XK_V, XK_B, XK_N, XK_M};
KeySym Symbols2[] = {XK_semicolon, XK_apostrophe, XK_bracketleft, XK_bracketright, XK_backslash};
KeySym ShiftSymbols2[] = {XK_colon, XK_quotedbl, XK_braceleft, XK_braceright, XK_bar};
KeySym ShiftEnter[] = {XK_Shift_L, XK_Return};
KeySym ShiftCommaKeys[] = {XK_less};

// Arabic mapping for letter keys (UTF-8 strings)
static const char *arabic_labels[ROWS][COLS] = { {NULL} };

ButtonDef keyboard[ROWS][COLS] = { 
    // Row 1
    {{"◐", NULL, NULL, NULL, 0, 1, False, False, False, False, False,11, False,2,0}, 
     {"🖫", NULL, CtrlS, NULL, 2, 1, False, False, False, False, False,12, False,2,0},
     {"✂", NULL, CtrlX, NULL, 2, 1, False, False, False, False, False,15, False,2,0},
     {"🗊", NULL, CtrlC, NULL, 2, 1, False, False, False, False, False,14, False,2,0},
     {"🗒", NULL, CtrlV, NULL, 2, 1, False, False, False, False, False,12, False,2,0}, 
     {"⮌", NULL, CtrlZ, NULL, 2, 1, False, False, False, False, False,12, False,2,0},
     {"⮎", NULL, CtrlShiftZ, NULL, 3, 1, False, False, False, False, False,12, False,2,0},
     {"⏮", NULL, &KS_Home, NULL, 1, 1, False, False, False, False, False,11, False,2,0},
     {"⏭", NULL, &KS_End, NULL, 1, 1, False, False, False, False, False,11, False,2,0},
     {"⇧", NULL, &KS_Page_Up, NULL, 1, 1, False, False, False, False, False,12, False,2,0},
     {"⇩", NULL, &KS_Page_Down, NULL, 1, 1, False, False, False, False, False,12, False,2,0},
     {"🟙", NULL, NULL, NULL, 1, 1, False, False, False, False, False,12, False,2,0},
     {"⏾", NULL, NULL, NULL, 1, 1, False, False, False, False, False,13, False,2,0},
     {"🖮", NULL, NULL, NULL, 1, 1, False, False, False, False, False,15, False,2,0},
     {"🖰", NULL, NULL, NULL, 0, 1, False, False, False, False, False,11, False,2,0}, // RMB
     {"🌐", NULL, NULL, NULL, 0, 1, False, False, False, False, False,12, False,2,0},
     {"✖", NULL, NULL, NULL, 0, 1, False, False, False, False, False,12, False,2,0}
     },
    // Row 2
    {{"Es", NULL, &KS_Escape, NULL , 1, 1, False, False, False, False, False,10, False,1,0}, 
     {"F1", NULL, &KS_F1, NULL, 1, 1, False, False, False, False, False,10, False,1,0}, 
     {"F2", NULL, &KS_F2, NULL, 1, 1, False, False, False, False, False,10, False,1,0}, 
     {"F3", NULL, &KS_F3, NULL, 1, 1, False, False, False, False, False,10, False,1,0}, 
     {"F4", NULL, &KS_F4, NULL, 1, 1, False, False, False, False, False,10, False,1,0}, 
     {"F5", NULL, &KS_F5, NULL, 1, 1, False, False, False, False, False,10, False,1,0}, 
     {"F6", NULL, &KS_F6, NULL, 1, 1, False, False, False, False, False,10, False,1,0},
     {"F7", NULL, &KS_F7, NULL, 1, 1, False, False, False, False, False,10, False,1,0}, 
     {"F8", NULL, &KS_F8, NULL,1, 1, False, False, False, False, False,10, False,1,0}, 
     {"F9", NULL, &KS_F9, NULL, 1, 1, False, False, False, False, False,10, False,1,0}, 
     {"F10", NULL, &KS_F10, NULL, 1, 1, False, False, False, False, False,7, False,1,0}, 
     {"F11", NULL, &KS_F11, NULL, 1, 1, False, False, False, False, False,7, False,1,0}, 
     {"F12", NULL, &KS_F12, NULL, 1, 1, False, False, False, False, False,7, False,1,0}, 
     {"🔍", NULL, CtrlF, NULL, 2, 1, False, False, False, False, False,12, False,2,0},
     {"🗗", NULL, CtrlD, NULL, 2, 1, False, False, False, False, False,12, False,2,0},
     {"🟣", NULL, CtrlA, NULL, 2, 1, False, False, False, False, False,11, False,2,0},
     {"🕯", NULL, CtrlN, NULL, 2, 1, False, False, False, False, False,15, False,2,0},
     {"⮀", NULL, NULL, NULL, 1, 1, False, False, False, False, False,15, False,2,0}
     },

// Row 3 (Qwerty 1) 
    {{"`", "~", &Symbols2[1], &ShiftNumbers[10], 1, 1, False, False, False, False, False,12, False,1,6}, 
     {"1", "!", &Numbers[0], &ShiftNumbers[0], 1, 1, False, False, False, False, False,12, False,1,0}, 
     {"2", "@", &Numbers[1], &ShiftNumbers[1], 1, 1, False, False, False, False, False,12, False,1,0}, 
     {"3", "#", &Numbers[2], &ShiftNumbers[2], 1, 1, False, False, False, False, False,12, False,1,0}, 
     {"4", "$", &Numbers[3], &ShiftNumbers[3], 1, 1, False, False, False, False, False,12, False,1,0}, 
     {"5", "%", &Numbers[4], &ShiftNumbers[4], 1, 1, False, False, False, False, False,12, False,1,0}, 
     {"6", "^", &Numbers[5], &ShiftNumbers[5], 1, 1, False, False, False, False, False,12, False,1,0},
     {"7", "&", &Numbers[6], &ShiftNumbers[6], 1, 1, False, False, False, False, False,12, False,1,0}, 
     {"8", "*", &Numbers[7], &ShiftNumbers[7], 1, 1, False, False, False, False, False,12, False,1,0}, 
     {"9", "(", &Numbers[8], &ShiftNumbers[8], 1, 1, False, False, False, False, False,12, False,1,0}, 
     {"0", ")", &Numbers[9], &ShiftNumbers[9], 1, 1, False, False, False, False, False,12, False,1,0}, 
     {"-", "_", &Symbols[0], &ShiftSymbols[0], 1, 1, False, False, False, False, False,12, False,1,2}, 
     {"=", "+", &Symbols[2], &ShiftSymbols[2], 1, 1, False, False, False, False, False,12, False,1,2}, 
     {"⌫", NULL, &KS_BackSpace, NULL, 1, 2, False, False, False, False, False,12, 11,2,-1}, // ⟵🠰🢤
     {"⌧", NULL, &KS_Delete, NULL, 1, 1, False, False, False, False, False,12, False,2,-1}, //del
     {"✍", NULL, NULL, NULL, 1, 1, False, False, False, False, False,10, False,2,0}, // swipe 
     {"🔊", NULL, NULL, NULL, 0, 1, False, False, False, False, False, 12, False,2,0}  // VOLUME UP
     },
    // Row 4 (Qwerty 1) 
    {{"Tab", NULL, &KS_Tab, NULL, 1, 2, False, False, False, False, False,9, False,1,0},
     {"q", "Q", &Qwerty1[0], &Qwerty1U[0], 1, 1, True, False, False, False, False,12, False,1,-5},
     {"w", "W", &Qwerty1[1], &Qwerty1U[1], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {"e", "E", &Qwerty1[2], &Qwerty1U[2], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {"r", "R", &Qwerty1[3], &Qwerty1U[3], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {"t", "T", &Qwerty1[4], &Qwerty1U[4], 1, 1, True, False, False, False, False,12, False,1,0},
     {"y", "Y", &Qwerty1[5], &Qwerty1U[5], 1, 1, True, False, False, False, False,12, False,1,-5}, 
     {"u", "U", &Qwerty1[6], &Qwerty1U[6], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {"i", "I", &Qwerty1[7], &Qwerty1U[7], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {"o", "O", &Qwerty1[8], &Qwerty1U[8], 1, 1, True, False, False, False, False,12, False,1,0},
     {"p", "P", &Qwerty1[9], &Qwerty1U[9], 1, 1, True, False, False, False, False,12, False,1,-5}, 
     {"[", "{", &Symbols2[2], &Symbols2[2], 1, 1, False, False, False, False, False,12, False,1,-5},
     {"]", "}", &Symbols2[3], &Symbols2[3], 1, 1, False, False, False, False, False,12, False,1,-5},
     {"\\", "|", &Symbols2[4], &ShiftSymbols2[4], 1, 1, False, False, False, False, False,12, False,1,0},
     {"🗚", NULL, NULL, NULL, 0, 1, False, False, False, False, False, 11, False,2,0},
     {"🗛", NULL, NULL, NULL, 0, 1, False, False, False, False, False, 11, False,2,0},
     {"🔉", NULL, NULL, NULL, 0, 1, False, False, False, False, False, 12, False,2,0}  // VOLUME DOWN
     },
    // Row 5 (Qwerty 2) 
    {{"Caps", NULL, &KS_Caps_Lock, NULL, 1, 2, False, True, False,False,False,9, False,1,0},
     {"a", "A", &Qwerty2[0], &Qwerty2U[0], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {"s", "S", &Qwerty2[1], &Qwerty2U[1], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {"d", "D", &Qwerty2[2], &Qwerty2U[2], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {"f", "F", &Qwerty2[3], &Qwerty2U[3], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {"g", "G", &Qwerty2[4], &Qwerty2U[4], 1, 1, True, False, False, False, False,12, False,1,-5}, 
     {"h", "H", &Qwerty2[5], &Qwerty2U[5], 1, 1, True, False, False, False, False,12, False,1,0},
     {"j", "J", &Qwerty2[6], &Qwerty2U[6], 1, 1, True, False, False, False, False,12, False,1,-8}, 
     {"k", "K", &Qwerty2[7], &Qwerty2U[7], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {"l", "L", &Qwerty2[8], &Qwerty2U[8], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {";", ":", &Symbols2[0], &ShiftSymbols2[0], 1, 1, False, False, False, False, False,12, False,1,3}, 
     {"'", "\"", &Symbols2[1], &ShiftSymbols2[1], 1, 1, False, False, False, False, False,12, False,1,5}, 
     {"Enter", NULL, &KS_Return, NULL, 1, 3, False, False, False, False, False,9, False,1,0},
     {"🕹", NULL, NULL, NULL, 1, 1, False, False, False, False, False,12, False,2,0}, // Button shape ◻
     {"🔇", NULL, NULL, NULL, 0, 1, False, False, False, False, False, 11, False,2,-3} //🔇
     },
    // Row 6 (Qwerty 3) 
    { {"Shift", NULL, &KS_Shift_L, NULL, 1, 2, False, False, True,False,False,9, False,1,0},
     {"z", "Z", &Qwerty3[0], &Qwerty3U[0], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {"x", "X", &Qwerty3[1], &Qwerty3U[1], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {"c", "C", &Qwerty3[2], &Qwerty3U[2], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {"v", "V", &Qwerty3[3], &Qwerty3U[3], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {"b", "B", &Qwerty3[4], &Qwerty3U[4], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {"n", "N", &Qwerty3[5], &Qwerty3U[5], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {"m", "M", &Qwerty3[6], &Qwerty3U[6], 1, 1, True, False, False, False, False,12, False,1,0}, 
     {",", "<",  &KS_comma, &KS_less, 1, 1, False, False, False, False, False,14, False,1,1}, 
     {".", ">", &KS_period,  &KS_greater,  1, 1, False, False, False, False, False,14, False,1,1}, 
     {"/", "?", &KS_slash,  &KS_question,  1, 1, False, False, False, False, False,12, False,1,0}, 
     {"🠹", NULL, &KS_Up, NULL, 1, 1, False, False, False, False, False,17, False,2,5}, // ▲
     {"⏎", NULL, ShiftEnter, NULL, 2, 1, False, False, False, False, False,14, False,2,0}, //↲ shift+enter
     {"Shift", NULL, &KS_Shift_R, NULL, 1, 2, False, False, True,False,False,9, False,1,0},
     {"🟫", NULL, NULL, NULL, 0, 1, False, False, False, False, False, 12, False,2,0}, // Fade 🌫
     {"📷", NULL, NULL, NULL, 0, 1, False, False, False, False, False,12, False,2,0},
     },  
    // Row 7 (Modifiers) 
    {
     {"Ctrl", NULL, &KS_Ctrl_L, NULL, 1, 2, False, False, False, True, False,8, False,1,0},
     {"Alt", NULL, &KS_Alt_L, NULL, 1, 2, False, False, False, False, True,8, False,1,0}, 
     {" ", NULL, &KS_space, NULL, 1, 5, False, False, False, False, False,8, False,1,0},
     {"Alt", NULL, &KS_Alt_R, NULL, 1, 1, False, False, False, False, True,8, False,1,0}, 
     {"❖", NULL, &KS_Super_R, NULL, 1, 1, False, False, False, False, False,6, False,2,-2}, // swipe 
     {"🠸", NULL, &KS_Left, NULL, 1, 1, False, False, False, False, False,17, False,2,3}, // ⯇
     {"🠻", NULL, &KS_Down, NULL, 1, 1, False, False, False, False, False,17, False,2,6}, // ▼
     {"🠺", NULL, &KS_Right, NULL, 1, 1, False, False, False, False, False,17, False,2,3}, // ⯈
     {"Ctrl", NULL, &KS_Ctrl_L, NULL, 1, 2, False, False, False, True, False,8, False,1,0},
     } 
};

// Numeric keypad layout (5 rows, 4 columns)
ButtonDef numeric_keyboard[NUM_ROWS][NUM_COLS] = {
    // Row 0: last 4 buttons from main first row: 🖮, 🖰, 🌐, ✖
    {{"🖮", NULL, NULL, NULL, 1, 1, False, False, False, False, False, 15, False, 2, 0},
     {"🖰", NULL, NULL, NULL, 0, 1, False, False, False, False, False, 11, False, 2, 0},
     {"🌐", NULL, NULL, NULL, 0, 1, False, False, False, False, False, 12, False, 2, 0},
     {"✖", NULL, NULL, NULL, 0, 1, False, False, False, False, False, 12, False, 2, 0}},
    // Row 1: 7 8 9 /
    {{"7", NULL, &KS_KP_7, NULL, 1, 1, False, False, False, False, False, 14, False, 1, 0},
     {"8", NULL, &KS_KP_8, NULL, 1, 1, False, False, False, False, False, 14, False, 1, 0},
     {"9", NULL, &KS_KP_9, NULL, 1, 1, False, False, False, False, False, 14, False, 1, 0},
     {"/", NULL, &KS_KP_Divide, NULL, 1, 1, False, False, False, False, False, 14, False, 1, 0}},
    // Row 2: 4 5 6 *
    {{"4", NULL, &KS_KP_4, NULL, 1, 1, False, False, False, False, False, 14, False, 1, 0},
     {"5", NULL, &KS_KP_5, NULL, 1, 1, False, False, False, False, False, 14, False, 1, 0},
     {"6", NULL, &KS_KP_6, NULL, 1, 1, False, False, False, False, False, 14, False, 1, 0},
     {"*", NULL, &KS_KP_Multiply, NULL, 1, 1, False, False, False, False, False, 14, False, 1, 0}},
    // Row 3: 1 2 3 -
    {{"1", NULL, &KS_KP_1, NULL, 1, 1, False, False, False, False, False, 14, False, 1, 0},
     {"2", NULL, &KS_KP_2, NULL, 1, 1, False, False, False, False, False, 14, False, 1, 0},
     {"3", NULL, &KS_KP_3, NULL, 1, 1, False, False, False, False, False, 14, False, 1, 0},
     {"-", NULL, &KS_KP_Subtract, NULL, 1, 1, False, False, False, False, False, 14, False, 1, 0}},
    // Row 4: 0 . + Enter
    {{"0", NULL, &KS_KP_0, NULL, 1, 1, False, False, False, False, False, 14, False, 1, 0},
     {".", NULL, &KS_KP_Decimal, NULL, 1, 1, False, False, False, False, False, 14, False, 1, 0},
     {"+", NULL, &KS_KP_Add, NULL, 1, 1, False, False, False, False, False, 14, False, 1, 0},
     {"⏎", NULL, &KS_KP_Enter, NULL, 1, 1, False, False, False, False, False, 12, False, 1, 0}}
};

// Arabic mapping for non‑letter keys (normal and shifted)
static const char *get_arabic_label(const char *english_label, Bool shift) {
    // Normal (non‑shift) mapping
    
    if (!shift) {
        if (strcmp(english_label, "`") == 0) return "`";
        if (strcmp(english_label, "-") == 0) return "-";
        if (strcmp(english_label, "=") == 0) return "=";
        if (strcmp(english_label, "[") == 0) return "ج";
        if (strcmp(english_label, "]") == 0) return "د";
        if (strcmp(english_label, "\\") == 0) return "\\";
        if (strcmp(english_label, ";") == 0) return "ك";
        if (strcmp(english_label, "'") == 0) return "ط";
        if (strcmp(english_label, ",") == 0) return "و";
        if (strcmp(english_label, ".") == 0) return "ز";
        if (strcmp(english_label, "/") == 0) return "ظ";
        if (strcmp(english_label, "<") == 0) return "<";
        if (strcmp(english_label, ">") == 0) return "¦";
        if (strcmp(english_label, "?") == 0) return ".";
        if (strcmp(english_label, ":") == 0) return ":";
        if (strcmp(english_label, "\"") == 0) return "\"";
        if (strcmp(english_label, "{") == 0) return "{";
        if (strcmp(english_label, "}") == 0) return "}";
        if (strcmp(english_label, "|") == 0) return "|";
    } else {
        // Shifted mapping (when Shift key is active)
        if (strcmp(english_label, "<") == 0) return ">";
        if (strcmp(english_label, ">") == 0) return "?";
        if (strcmp(english_label, "?") == 0) return "؟";
        if (strcmp(english_label, ":") == 0) return ":";
        if (strcmp(english_label, "\"") == 0) return "\"";
        if (strcmp(english_label, "{") == 0) return "<";
        if (strcmp(english_label, "}") == 0) return ">";
        if (strcmp(english_label, "|") == 0) return "…";
        if (strcmp(english_label, "_") == 0) return "_";
        if (strcmp(english_label, "+") == 0) return "+";
        if (strcmp(english_label, "~") == 0) return "~";
    }
    return NULL; // No Arabic mapping, keep English
}

// Initialize Arabic labels for letter keys
void init_arabic_labels(void) {
    // Row 4 (index 3) Qwerty1 letters
    static const char *arabic_row4[] = {
        NULL,   // col0 Tab
        "ض", "ص", "ث", "ق", "ف", "غ", "ع", "ه", "خ", "ح",  // q,w,e,r,t,y,u,i,o,p
        "ج", "د", "ذ"                                      // [, ], \''
    };
    // Row 5 (index 4) Qwerty2 letters
    static const char *arabic_row5[] = {
        NULL,   // Caps
        "ش", "س", "ي", "ب", "ل", "ا", "ت", "ن", "م",   // a,s,d,f,g,h,j,k,l
        "ك", "ط"                                         // ; '
    };
    // Row 6 (index 5) Qwerty3 letters
    static const char *arabic_row6[] = {
        NULL,   // Shift
        "ئ", "ء", "ؤ", "ر", "ﻻ","ى", "ة", "و",   // z,x,c,v,b,n,m
        "ز", "ظ"                          // comma, period, slash (Arabic versions)
    };
    
    for (int row = 0; row < ROWS; row++)
        for (int col = 0; col < COLS; col++)
            arabic_labels[row][col] = NULL;
            
    // Row 3 (index 3)
    for (int col = 1; col <= 13; col++)
        if (col <= 13) arabic_labels[3][col] = arabic_row4[col];
    // Row 4 (index 4)
    for (int col = 1; col <= 11; col++)
        arabic_labels[4][col] = arabic_row5[col];
    // Row 5 (index 5)
    for (int col = 1; col <= 10; col++)
        arabic_labels[5][col] = arabic_row6[col];
}

// Check if current layout is Arabic (any variant)
static Bool is_arabic_layout(void) {
    return (strstr(current_layout_name, "Arabic") != NULL);
}

// Function declarations
char* brightness_command(float b);
void create_popup_window();
void draw_popup_contents();
void open_web_browser(const char *url);
void save_config();
void load_config();
char* get_config_path(void);
int scaled_btn_width();
int scaled_btn_height();
int scaled_btn_padding();
int scaled_font_size(int base_size);
int scaled_win_width();
int scaled_win_height();
void adjust_scale(float factor);
void draw_keyboard();
void draw_numeric_keyboard();
Bool is_window_hidden_left();
void update_modifier_state();
void finish_swipe();
void handle_swipe_motion(int x, int y);
void toggle_swipe_mode();
void draw_swipe_path();
void update_swipe_display();
void update_hover_display();
void init_enhanced_swipe();
void cleanup_enhanced_swipe();
void enhanced_handle_swipe_motion(int x, int y);
void enhanced_finish_swipe();
void enhanced_toggle_swipe_mode();
void enhanced_draw_swipe_path();
void process_swipe_gesture();
char find_nearest_key(int x, int y, double *min_distance);
double distance_between_points(int x1, int y1, int x2, int y2);
void init_dictionary();
void get_suggestions(const char *input, WordSuggestion *suggestions, int *count);
void save_screenshot_png(const char *filename, int x, int y, int width, int height);
char* generate_screenshot_filename();
void take_screenshot();
int load_custom_font(const char *font_path);
void cleanup_font();
char* get_font_path(void);
void debug_key_mapping();
void volume_up();
void volume_down();
void toggle_mute();
void decrease_opacity();
void toggle_dock_left();
void toggle_button_shape();
void draw_rounded_rectangle(cairo_t *cr, double x, double y, double width, double height, double radius);
void draw_cup_rectangle(cairo_t *cr, double x, double y, double width, double height, double radius);
void draw_curve_rectangle(cairo_t *cr, double x, double y, double width, double height, double radius);
void send_keys(KeySym *keys, int count);
void send_mouse_button(int button);
void check_repeat();
Bool is_double_click(struct timeval *last_click_time);
void simulate_context_menu_at_caret(Display *display);
void draw_button(int x, int y, int width, int height, const char *text, 
                Bool is_caps_indicator, Bool is_shift_indicator, 
                Bool is_swipe_indicator, int font_size, int font_opt, int yshft);
void toggle_colors();
void toggle_theme();
void increase_brightness();
void decrease_brightness();
void speak_selected_text();
void change_kbd_layout();
void toggle_caps_lock();
void toggle_shrink_mode();
void toggle_window_mode();
void release_all_modifiers();
void enhanced_handle_swipe_button_press(int x, int y, int row, int col);
void cleanup_svg();
void handle_button_press(int x, int y, int button);
void handle_motion_notify(int x, int y);
void handle_mouse_enter();
void handle_mouse_exit();
void handle_button_release();
void make_window_always_on_top();
void set_window_icon();
void send_key(KeySym key);  // helper for single key events
void handle_numeric_button_press(int x, int y, int button);

// ====================================================================
// Core Functions
// ====================================================================

static const char* get_layout_name(int group) {
    static char name[256];
    name[0] = '\0';
    if (!dpy) return "unknown";

    XkbDescRec *xkb = XkbGetMap(dpy, XkbAllComponentsMask, XkbUseCoreKbd);
    if (!xkb) return "unknown";

    XkbGetNames(dpy, XkbSymbolsNameMask | XkbGroupNamesMask, xkb);
    if (xkb->names && xkb->names->groups[group] != None) {
        char *atom_name = XGetAtomName(dpy, xkb->names->groups[group]);
        if (atom_name) {
            strncpy(name, atom_name, sizeof(name) - 1);
            XFree(atom_name);
        } else {
            snprintf(name, sizeof(name), "group%d", group);
        }
    } else {
        snprintf(name, sizeof(name), "group%d", group);
    }
    XkbFreeKeyboard(xkb, 0, True);
    return name;
}

static void init_xkb_monitor() {
    int xkb_opcode, xkb_error_base;
    if (!XkbQueryExtension(dpy, &xkb_opcode, &xkb_event_base, &xkb_error_base, NULL, NULL)) {
        fprintf(stderr, "XKB extension not available, layout monitoring disabled.\n");
        return;
    }

    // Select XKB state notify events for the core keyboard
    XkbSelectEvents(dpy, XkbUseCoreKbd, XkbStateNotifyMask, XkbStateNotifyMask);

    // Get the initial group
    XkbStateRec state;
    if (XkbGetState(dpy, XkbUseCoreKbd, &state) == Success) {
        current_xkb_group = state.group;
    } else {
        current_xkb_group = 0;
    }
    strncpy(current_layout_name, get_layout_name(current_xkb_group), sizeof(current_layout_name) - 1);
}

double distance_between_points(int x1, int y1, int x2, int y2) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

char find_nearest_key(int x, int y, double *min_distance) {
    char nearest_key = '\0';
    *min_distance = DBL_MAX;
    
    for (int row = 0; row < ROWS; row++) {
        int current_x = scaled_btn_padding();
        
        if (row == 0) {
            current_x += scaled_btn_width() + scaled_btn_padding();
        }

        for (int col = 0; col < COLS; col++) {
            if (keyboard[row][col].label == NULL) continue;
            
            if (!keyboard[row][col].is_letter) {
                int btn_width = (keyboard[row][col].width ? keyboard[row][col].width : 1) * 
                              (scaled_btn_width() + scaled_btn_padding()) - scaled_btn_padding();
                current_x += btn_width + scaled_btn_padding();
                continue;
            }
            
            int btn_width = (keyboard[row][col].width ? keyboard[row][col].width : 1) * 
                          (scaled_btn_width() + scaled_btn_padding()) - scaled_btn_padding();
            int btn_y = scaled_btn_padding() + row * (scaled_btn_height() + scaled_btn_padding());
            
            int center_x = current_x + btn_width / 2;
            int center_y = btn_y + scaled_btn_height() / 2;
            
            double distance = distance_between_points(x, y, center_x, center_y);
            
            if (distance < *min_distance) {
                *min_distance = distance;
                // For swipe we still use Latin base key (label[0]) because it's internal identity
                if (keyboard[row][col].label && strlen(keyboard[row][col].label) >= 1) {
                    nearest_key = tolower(keyboard[row][col].label[0]);
                }
            }
            
            current_x += btn_width + scaled_btn_padding();
        }
    }
    
    return nearest_key;
}

void init_enhanced_swipe() {
    if (!swipe_state.swipe_points) {
        swipe_state.swipe_points = g_array_new(FALSE, FALSE, sizeof(SwipePoint));
    } else {
        g_array_set_size(swipe_state.swipe_points, 0);
    }
    if (!swipe_state.activated_keys) {
        swipe_state.activated_keys = g_hash_table_new(g_direct_hash, g_direct_equal);
    } else {
        g_hash_table_remove_all(swipe_state.activated_keys);
    }
    if (!swipe_state.key_sequence) {
        swipe_state.key_sequence = g_array_new(FALSE, FALSE, sizeof(char));
    } else {
        g_array_set_size(swipe_state.key_sequence, 0);
    }
}

void cleanup_enhanced_swipe() {
    if (swipe_state.swipe_points) {
        g_array_free(swipe_state.swipe_points, TRUE);
        swipe_state.swipe_points = NULL;
    }
    if (swipe_state.activated_keys) {
        g_hash_table_destroy(swipe_state.activated_keys);
        swipe_state.activated_keys = NULL;
    }
    if (swipe_state.key_sequence) {
        g_array_free(swipe_state.key_sequence, TRUE);
        swipe_state.key_sequence = NULL;
    }
}

void process_swipe_gesture() {
    if (!swipe_state.swipe_points || swipe_state.swipe_points->len < 2) {
        printf("Swipe too short: %d points\n", 
               swipe_state.swipe_points ? swipe_state.swipe_points->len : 0);
        return;
    }
    
    SwipePoint *points = (SwipePoint *)swipe_state.swipe_points->data;
    gint64 duration = (points[swipe_state.swipe_points->len - 1].timestamp - points[0].timestamp) / 1000;
    
    if (duration > MAX_SWIPE_TIME) {
        printf("Swipe too slow: %ld ms\n", duration);
        return;
    }
    
    double total_distance = 0;
    for (guint i = 1; i < swipe_state.swipe_points->len; i++) {
        total_distance += distance_between_points(
            points[i-1].x, points[i-1].y,
            points[i].x, points[i].y
        );
    }
    
    if (total_distance < MIN_SWIPE_DISTANCE) {
        printf("Swipe too short: %.2f pixels\n", total_distance);
        return;
    }
    
    if (swipe_state.key_sequence && swipe_state.key_sequence->len >= MIN_WORD_LENGTH) {
        swipe_state.current_word_len = 0;
        for (guint i = 0; i < swipe_state.key_sequence->len && swipe_state.current_word_len < sizeof(swipe_state.current_word)-1; i++) {
            char key = g_array_index(swipe_state.key_sequence, char, i);
            
            if (swipe_state.current_word_len == 0 || 
                key != swipe_state.current_word[swipe_state.current_word_len - 1]) {
                swipe_state.current_word[swipe_state.current_word_len++] = key;
            }
        }
        swipe_state.current_word[swipe_state.current_word_len] = '\0';
        printf("Processed swipe word: %s from %d keys\n", swipe_state.current_word, swipe_state.key_sequence->len);
    } else {
        printf("Not enough keys in sequence: %d\n", 
               swipe_state.key_sequence ? swipe_state.key_sequence->len : 0);
    }
}

void init_dictionary() {
    const char *word_list[] = {
        "the", "be", "to", "of", "and", "a", "in", "that", "have", "i", "it", "for", "not", 
        "on", "with", "he", "as", "you", "do", "at", "this", "but", "his", "by", "from", 
        "they", "we", "say", "her", "she", "or", "an", "will", "my", "one", "all", "would", 
        "there", "their", "what", "so", "up", "out", "if", "about", "who", "get", "which", 
        "go", "me", "when", "make", "can", "like", "time", "no", "just", "him", "know", "take", 
        "people", "into", "year", "your", "good", "some", "could", "them", "see", "other", 
        "than", "then", "now", "look", "only", "come", "its", "over", "think", "also", "back", 
        "after", "use", "two", "how", "our", "work", "first", "well", "way", "even", "new", 
        "want", "because", "any", "give", "day", "most", "us", "generate","create",
        "hello", "world", "great", "awesome", "phone", "computer", "keyboard", 
        "screen", "swipe", "text", "type", "word", "letter", "app", "web", "site", 
        "internet", "email", "message", "chat", "friend", "family", "love", "life", 
        "home", "house", "place", "city", "state", "country", "school", "work", 
        "office", "job", "money", "car", "food", "water", "coffee", "music", 
        "movie", "book", "game", "sport", "play", "run", "walk", "jump", "read",
        "write", "learn", "teach", "grow", "move", "stop", "start", "end", "finish", 
        "build", "design", "develop", "code", "program", "system", "data", 
        "file", "save", "delete", "edit", "search", "find", "watch", "listen", "speak", 
        "talk", "call", "number", "contact", "address", "date", "calendar", "event", "plan", 
        "schedule", "morning", "afternoon", "evening", "night", "today", "tomorrow", 
        "yesterday", "week", "month", "hour", "minute", "fast", "slow", "big", "small", 
        "long", "short", "high", "low", "hard", "easy", "simple", "clear", "clean", "old", 
        "new", "young", "right", "wrong", "true", "false", "yes", "no", "maybe", "please", 
        "thanks", "sorry", "welcome", "goodbye", "name", "user", "password", "account", 
        "settings", "help", "info", "error", "bug", "fix", "update", "version", "test", 
        "check", "try", "buy", "sell", "pay", "cost", "price", "free", "open", "close", 
        "lock", "unlock", "start", "stop", "pause", "send", "receive", "download", "upload", 
        "copy", "paste", "print", "scan", "view", "show", "hide", "zoom", "drag", "drop", 
        "click", "touch", "press", "hold", "release", "scroll", "pinch", "rotate", "shake", 
        "tilt", "turn", "left", "right", "up", "down", "top", "bottom", "center", "middle", 
        "side", "front", "back", "inside", "outside", "near", "far", "here", "there", "where", 
        "when", "why", "how", "what", "who", "which", "that", "this", "these", "those",
        "milk","board","green","blue","red","purple","white","while","brown","rectangle",
        "orange", "triangle","upgrade","kit","hit", "install", "java","python"
    };
    
    int word_count = sizeof(word_list) / sizeof(word_list[0]);
    swipe_state.dictionary_size = 0;
    
    for (int i = 0; i < word_count && swipe_state.dictionary_size < MAX_DICTIONARY_WORDS; i++) {
        strncpy(swipe_state.dictionary[swipe_state.dictionary_size].word, word_list[i], MAX_WORD_LENGTH - 1);
        swipe_state.dictionary[swipe_state.dictionary_size].word[MAX_WORD_LENGTH - 1] = '\0';
        swipe_state.dictionary[swipe_state.dictionary_size].length = strlen(word_list[i]);
        swipe_state.dictionary_size++;
    }
}

void get_suggestions(const char *input, WordSuggestion *suggestions, int *count) {
    *count = 0;
    if (!input || strlen(input) < 1) return;
    
    char first_char = input[0];
    char last_char = input[strlen(input) - 1];
    int input_len = strlen(input);
    
    WordSuggestion candidates[MAX_DICTIONARY_WORDS];
    int candidate_count = 0;
    
    for (int i = 0; i < swipe_state.dictionary_size; i++) {
        const char *word = swipe_state.dictionary[i].word;
        int word_len = swipe_state.dictionary[i].length;
        
        if (word[0] != first_char) continue;
        
        float score = 20.0f;
        
        if (word[word_len - 1] == last_char) score += 50.0f;
        
        int matched_count = 0;
        int search_idx = 0;
        
        for (int j = 0; j < input_len; j++) {
            char c = input[j];
            for (int k = search_idx; k < word_len; k++) {
                if (word[k] == c) {
                    matched_count++;
                    search_idx = k + 1;
                    break;
                }
            }
        }
        
        score += ((float)matched_count / input_len) * 30.0f;
        
        int len_diff = abs(word_len - input_len);
        score -= len_diff * 2.0f;
        
        if (word_len <= 4) score += 5.0f;
        
        if (score > 30.0f && candidate_count < MAX_DICTIONARY_WORDS) {
            strncpy(candidates[candidate_count].word, word, MAX_WORD_LENGTH - 1);
            candidates[candidate_count].word[MAX_WORD_LENGTH - 1] = '\0';
            candidates[candidate_count].score = score;
            candidate_count++;
        }
    }
    
    for (int i = 0; i < candidate_count - 1; i++) {
        for (int j = 0; j < candidate_count - i - 1; j++) {
            if (candidates[j].score < candidates[j + 1].score) {
                WordSuggestion temp = candidates[j];
                candidates[j] = candidates[j + 1];
                candidates[j + 1] = temp;
            }
        }
    }
    
    *count = (candidate_count < MAX_SUGGESTIONS) ? candidate_count : MAX_SUGGESTIONS;
    for (int i = 0; i < *count; i++) {
        strncpy(suggestions[i].word, candidates[i].word, MAX_WORD_LENGTH - 1);
        suggestions[i].word[MAX_WORD_LENGTH - 1] = '\0';
        suggestions[i].score = candidates[i].score;
    }
}

void enhanced_finish_swipe() {
    if (swipe_state.swype_active && swipe_state.is_drawing) {
        process_swipe_gesture();
        
        if (swipe_state.current_word_len >= MIN_WORD_LENGTH) {
            WordSuggestion suggestions[MAX_SUGGESTIONS];
            int suggestion_count = 0;
            
            get_suggestions(swipe_state.current_word, suggestions, &suggestion_count);
            
            if (suggestion_count > 0) {
                strncpy(swipe_state.last_inserted_word, suggestions[0].word, MAX_WORD_LENGTH - 1);
                swipe_state.last_inserted_word[MAX_WORD_LENGTH - 1] = '\0';
                printf("Sending swipe word: %s (from suggestions)\n", suggestions[0].word);
                
                for (int i = 0; i < strlen(suggestions[0].word); i++) {
                    char c = suggestions[0].word[i];
                    KeySym key = XK_a + (c - 'a');
                    KeyCode keycode = XKeysymToKeycode(dpy, key);
                    
                    if (keycode != 0) {
                        XTestFakeKeyEvent(dpy, keycode, True, CurrentTime);
                        XFlush(dpy);
                        usleep(20000);
                        XTestFakeKeyEvent(dpy, keycode, False, CurrentTime);
                        XFlush(dpy);
                        usleep(20000);
                    }
                }
                
                KeyCode space_code = XKeysymToKeycode(dpy, XK_space);
                if (space_code != 0) {
                    XTestFakeKeyEvent(dpy, space_code, True, CurrentTime);
                    XFlush(dpy);
                    usleep(20000);
                    XTestFakeKeyEvent(dpy, space_code, False, CurrentTime);
                    XFlush(dpy);
                }
            } else {
                printf("Sending raw swipe: %s\n", swipe_state.current_word);
                
                for (int i = 0; i < swipe_state.current_word_len; i++) {
                    char c = swipe_state.current_word[i];
                    KeySym key = XK_a + (c - 'a');
                    KeyCode keycode = XKeysymToKeycode(dpy, key);
                    
                    if (keycode != 0) {
                        XTestFakeKeyEvent(dpy, keycode, True, CurrentTime);
                        XFlush(dpy);
                        usleep(20000);
                        XTestFakeKeyEvent(dpy, keycode, False, CurrentTime);
                        XFlush(dpy);
                        usleep(20000);
                    }
                }
                
                KeyCode space_code = XKeysymToKeycode(dpy, XK_space);
                if (space_code != 0) {
                    XTestFakeKeyEvent(dpy, space_code, True, CurrentTime);
                    XFlush(dpy);
                    usleep(20000);
                    XTestFakeKeyEvent(dpy, space_code, False, CurrentTime);
                    XFlush(dpy);
                }
            }
        }
    }
    
    memset(swipe_state.current_word, 0, sizeof(swipe_state.current_word));
    swipe_state.current_word_len = 0;
    swipe_state.is_drawing = False;
    swipe_state.last_x = -1;
    swipe_state.last_y = -1;
    
    if (swipe_state.swipe_points) {
        g_array_set_size(swipe_state.swipe_points, 0);
    }
    if (swipe_state.activated_keys) {
        g_hash_table_remove_all(swipe_state.activated_keys);
    }
    if (swipe_state.key_sequence) {
        g_array_set_size(swipe_state.key_sequence, 0);
    }
    
    draw_keyboard();
}

void enhanced_draw_swipe_path() {
    if (!swipe_state.swype_active || !swipe_state.swipe_points || swipe_state.swipe_points->len < 2) {
        return;
    }

    SwipePoint *points = (SwipePoint *)swipe_state.swipe_points->data;
    
    cairo_set_source_rgba(back_cr, 0.0, 0.35, 1.0, 0.2);
    cairo_set_line_width(back_cr, SWIPE_PATH_THICKNESS * 9 *scale_factor);
    cairo_set_line_cap(back_cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(back_cr, CAIRO_LINE_JOIN_ROUND);
    
    cairo_move_to(back_cr, points[0].x, points[0].y);
    for (guint i = 1; i < swipe_state.swipe_points->len; i++) {
        cairo_line_to(back_cr, points[i].x, points[i].y);
    }
    cairo_stroke(back_cr);
    
    cairo_set_source_rgba(back_cr, 0.0, 0.35, 1.0, 0.18);
    cairo_set_line_width(back_cr, SWIPE_PATH_THICKNESS * scale_factor);
    
    cairo_move_to(back_cr, points[0].x, points[0].y);
    for (guint i = 1; i < swipe_state.swipe_points->len; i++) {
        cairo_line_to(back_cr, points[i].x, points[i].y);
    }
    cairo_stroke(back_cr);
    
    if (swipe_state.activated_keys && swipe_state.key_sequence) {
        for (guint i = 0; i < swipe_state.key_sequence->len; i++) {
            char key_char = g_array_index(swipe_state.key_sequence, char, i);
            
            for (int row = 0; row < ROWS; row++) {
                int current_x = scaled_btn_padding();
                
                if (row == 0) {
                    current_x += scaled_btn_width() + scaled_btn_padding();
                }

                for (int col = 0; col < COLS; col++) {
                    if (keyboard[row][col].label == NULL) {
                        int skip_width = (keyboard[row][col].width ? keyboard[row][col].width : 1) * 
                                        (scaled_btn_width() + scaled_btn_padding()) - scaled_btn_padding();
                        current_x += skip_width + scaled_btn_padding();
                        continue;
                    }
                    
                    if (!keyboard[row][col].is_letter) {
                        int skip_width = (keyboard[row][col].width ? keyboard[row][col].width : 1) * 
                                        (scaled_btn_width() + scaled_btn_padding()) - scaled_btn_padding();
                        current_x += skip_width + scaled_btn_padding();
                        continue;
                    }
                    
                    char label_char = tolower(keyboard[row][col].label[0]);
                    if (label_char == key_char) {
                        int btn_width = (keyboard[row][col].width ? keyboard[row][col].width : 1) * 
                                      (scaled_btn_width() + scaled_btn_padding()) - scaled_btn_padding();
                        int btn_y = scaled_btn_padding() + row * (scaled_btn_height() + scaled_btn_padding());
                        
                        int center_x = current_x + btn_width / 2;
                        int center_y = btn_y + scaled_btn_height() / 2;
                        
                        current_x += btn_width + scaled_btn_padding();
                        break;
                    }
                    
                    int btn_width = (keyboard[row][col].width ? keyboard[row][col].width : 1) * 
                                  (scaled_btn_width() + scaled_btn_padding()) - scaled_btn_padding();
                    current_x += btn_width + scaled_btn_padding();
                }
            }
        }
    }
}

void enhanced_toggle_swipe_mode() {
    swipe_state.swype_active = !swipe_state.swype_active;
    
    if (swipe_state.swype_active) {
        if (swipe_state.dictionary_size == 0) {
            init_dictionary();
        }
        
        init_enhanced_swipe();
        
        memset(swipe_state.current_word, 0, sizeof(swipe_state.current_word));
        swipe_state.current_word_len = 0;
        swipe_state.is_drawing = False;
        swipe_state.last_x = -1;
        swipe_state.last_y = -1;
        swipe_state.last_time = g_get_monotonic_time();
        memset(swipe_state.last_inserted_word, 0, sizeof(swipe_state.last_inserted_word));
        
        if (swipe_state.swipe_points) {
            g_array_set_size(swipe_state.swipe_points, 0);
        }
        if (swipe_state.activated_keys) {
            g_hash_table_remove_all(swipe_state.activated_keys);
        }
        if (swipe_state.key_sequence) {
            g_array_set_size(swipe_state.key_sequence, 0);
        }
        
        if (shift_active) {
            shift_active = False;
            sticky_shift = False;
        }
        if (ctrl_active) {
            ctrl_active = False;
            sticky_ctrl = False;
        }
        if (alt_active) {
            alt_active = False;
            sticky_alt = False;
        }
        update_modifier_state();
    } else {
        cleanup_enhanced_swipe();
    }
    
    draw_keyboard();
}

// ====================================================================
// Helper for single key events (used by mouse wheel)
// ====================================================================
void send_key(KeySym key) {
    KeyCode code = XKeysymToKeycode(dpy, key);
    if (code) {
        XTestFakeKeyEvent(dpy, code, True, CurrentTime);
        XFlush(dpy);
        usleep(10000);
        XTestFakeKeyEvent(dpy, code, False, CurrentTime);
        XFlush(dpy);
    }
}

// ====================================================================
// Drawing Functions with Double Buffering
// ====================================================================

void draw_swipe_path() {
    if (!swipe_state.swype_active || !swipe_state.swype_path || swipe_state.swype_path->len < 2) {
        return;
    }
    
    cairo_set_source_rgba(back_cr, SWYPE_PATH_COLOR_R, SWYPE_PATH_COLOR_G, SWYPE_PATH_COLOR_B, SWIPE_PATH_OPACITY);
    cairo_set_line_width(back_cr, SWIPE_PATH_THICKNESS * scale_factor);
    cairo_set_line_cap(back_cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(back_cr, CAIRO_LINE_JOIN_ROUND);
    
    XPoint *points = (XPoint *)swipe_state.swype_path->data;
    cairo_move_to(back_cr, points[0].x, points[0].y);
    
    for (guint i = 1; i < swipe_state.swype_path->len; i++) {
        cairo_line_to(back_cr, points[i].x, points[i].y);
    }
    cairo_stroke(back_cr);
}

void draw_button(int x, int y, int width, int height, const char *text, 
                Bool is_caps_indicator, Bool is_shift_indicator, 
                Bool is_swipe_indicator, int font_size, int font_opt, int yshft) {
    
    Bool is_pressed = False;
    Bool is_hovered = False;
    Bool is_letter_button = False;
    
    // For non-numeric modes, we need to find if this button is pressed/hovered
    // Since numeric mode uses its own layout, this function is only called for normal/ribbon/auto-shrink
    for (int row = 0; row < ROWS; row++) {
        int current_x = scaled_btn_padding();
        
        if (row == 0) {
            current_x += scaled_btn_width() + scaled_btn_padding();
        }

        for (int col = 0; col < COLS; col++) {
            if (keyboard[row][col].label == NULL) continue;

            int btn_width = (keyboard[row][col].width ? keyboard[row][col].width : 1) * 
                      (scaled_btn_width() + scaled_btn_padding()) - scaled_btn_padding();
            int btn_y = scaled_btn_padding() + row * (scaled_btn_height() + scaled_btn_padding());
            
            if (x == current_x && y == btn_y && width == btn_width && height == scaled_btn_height()) {
                is_pressed = (row == pressed_row && col == pressed_col);
                is_hovered = (row == hover_row && col == hover_col);
                is_letter_button = keyboard[row][col].is_letter;
                break;
            }
            
            current_x += btn_width + scaled_btn_padding();
        }
        if (is_pressed || is_hovered) break;
    }
    
    Bool arabic = is_arabic_layout();

    if (is_letter_button && (caps_lock || shift_active || arabic)) {
        yshft = 0;
    }

    cairo_set_line_width(back_cr, 1.0 * scale_factor);
    
    ThemeColors *theme = &themes[current_theme];
        
    if (is_pressed) {
        cairo_set_source_rgb(back_cr, theme->pressed_r, theme->pressed_g, theme->pressed_b);
    } else if (is_hovered) {
        cairo_set_source_rgb(back_cr, theme->hover_r, theme->hover_g, theme->hover_b);
    } else {
        cairo_set_source_rgb(back_cr, theme->btn_r, theme->btn_g, theme->btn_b);
    }
    
    double radius = fmin(width, height) * 0.3;
    
    switch (current_button_shape) {
        case SHAPE_RECTANGLE:
            cairo_rectangle(back_cr, x, y, width, height);
            break;
        case SHAPE_ROUNDED_RECTANGLE:
            draw_rounded_rectangle(back_cr, x, y, width, height, radius);
            break;
        case SHAPE_CURVE_RECTANGLE:
            draw_curve_rectangle(back_cr, x, y, width, height, radius);
            break;
        case SHAPE_CUP_RECTANGLE:
            draw_cup_rectangle(back_cr, x, y, width, height, radius);
            break;
    }
    cairo_fill(back_cr);
    
    if (is_pressed) {
        cairo_set_source_rgb(back_cr, 1.0, 0.0, 0.0);
    } else if (is_hovered) {
        cairo_set_source_rgb(back_cr, 0.0, 0.7, 1.0);
    } else {
        cairo_set_source_rgb(back_cr, theme->btn_border_r, theme->btn_border_g, theme->btn_border_b);
    }
    
    switch (current_button_shape) {
        case SHAPE_RECTANGLE:
            cairo_rectangle(back_cr, x, y, width, height);
            break;
        case SHAPE_ROUNDED_RECTANGLE:
            draw_rounded_rectangle(back_cr, x, y, width, height, radius);
            break;
        case SHAPE_CURVE_RECTANGLE:
            draw_curve_rectangle(back_cr, x, y, width, height, radius);
            break;
        case SHAPE_CUP_RECTANGLE:
            draw_cup_rectangle(back_cr, x, y, width, height, radius);
            break;
    }
    cairo_stroke(back_cr);
    
    if (font_opt == 2) {
        cairo_set_font_face(back_cr, custom_font_face);
    } else {
        cairo_select_font_face(back_cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    }

    cairo_set_font_size(back_cr, font_size);
    
    if (is_pressed || is_hovered) {
        if (current_theme == THEME_LIGHT) {
            cairo_set_source_rgb(back_cr, 0.0, 0.0, 0.0);
        } else {
            cairo_set_source_rgb(back_cr, 1.0, 1.0, 1.0);
        }
    } else if (is_caps_indicator && caps_lock) {
        cairo_set_source_rgb(back_cr, 1.0, 0.0, 0.0);
    } else if (is_shift_indicator && (shift_active || sticky_shift)) {
        if (sticky_shift) {
            cairo_set_source_rgb(back_cr, 0.8, 0.2, 0.8);
        } else {
            cairo_set_source_rgb(back_cr, 0.0, 0.8, 0.0);
        }
    } else if (strcmp(text, "Ctrl") == 0 && (ctrl_active || sticky_ctrl)) {
        if (sticky_ctrl) {
            cairo_set_source_rgb(back_cr, 0.8, 0.2, 0.8);
        } else {
            cairo_set_source_rgb(back_cr, 0.0, 0.8, 0.0);
        }
    } else if (strcmp(text, "Alt") == 0 && (alt_active || sticky_alt)) {
        if (sticky_alt) {
            cairo_set_source_rgb(back_cr, 0.8, 0.2, 0.8);
        } else {
            cairo_set_source_rgb(back_cr, 0.0, 0.8, 0.0);
        }
    } else if (is_swipe_indicator && swipe_state.swype_active) {
        cairo_set_source_rgb(back_cr, 0.0, 0.8, 0.0);
    } else if (strcmp(text, "🖮") == 0) {
        switch (current_mode) {
            case MODE_NORMAL:
                cairo_set_source_rgb(back_cr, 0.0, 0.8, 0.0);
                break;
            case MODE_AUTO_SHRINK:
                cairo_set_source_rgb(back_cr, 1.0, 0.5, 0.0);
                break;
            case MODE_RIBBON:
                cairo_set_source_rgb(back_cr, 0.0, 0.5, 1.0);
                break;
            case MODE_NUMERIC:
                cairo_set_source_rgb(back_cr, 0.8, 0.0, 0.8);
                break;
        }
    } else if (strcmp(text, "🎨") == 0) {
        switch (current_theme) {
            case THEME_DARK: cairo_set_source_rgb(back_cr, 0.5, 0.5, 0.5); break;
            case THEME_LIGHT: cairo_set_source_rgb(back_cr, 1.0, 1.0, 1.0); break;
            case THEME_BLUE: cairo_set_source_rgb(back_cr, 0.3, 0.4, 0.8); break;
            case THEME_GREEN: cairo_set_source_rgb(back_cr, 0.3, 0.7, 0.3); break;
            case THEME_PURPLE: cairo_set_source_rgb(back_cr, 0.5, 0.3, 0.8); break;
            case THEME_MARINE: cairo_set_source_rgb(back_cr, 0.8, 0.5, 0.3); break;
            case THEME_RED: cairo_set_source_rgb(back_cr, 0.8, 0.3, 0.3); break;
        }
    } else {
        cairo_set_source_rgb(back_cr, theme->text_r, theme->text_g, theme->text_b);
    }
    
    if (is_swipe_indicator && swipe_state.swype_active) {
        cairo_set_source_rgb(back_cr, 0.0, 0.8, 0.0);
    } else if (strcmp(text, "✍") == 0 && swipe_state.swype_active) {
        cairo_set_source_rgb(back_cr, 0.0, 0.8, 0.0);
    }

    cairo_text_extents_t extents;
    cairo_text_extents(back_cr, text, &extents);
    double text_x = x + (width - extents.width) / 2;
    double text_y = y + (height + extents.height) / 2 + yshft;
    
    cairo_move_to(back_cr, text_x, text_y);
    cairo_show_text(back_cr, text);

    if (strcmp(text, "🕹") == 0) {
        switch (current_button_shape) {
            case SHAPE_RECTANGLE:
                cairo_set_source_rgb(back_cr, 0.0, 0.8, 0.0);
                break;
            case SHAPE_ROUNDED_RECTANGLE:
                cairo_set_source_rgb(back_cr, 0.0, 0.5, 1.0);
                break;
            case SHAPE_CURVE_RECTANGLE:
                cairo_set_source_rgb(back_cr, 1.0, 0.5, 0.0);
                break;
            case SHAPE_CUP_RECTANGLE:
                cairo_set_source_rgb(back_cr, 0.5, 0.5, 0.5);
                break;
        }
    }
}

// ====================================================================
// Numeric Keypad Drawing
// ====================================================================
void draw_numeric_keyboard() {
    if (!back_cr) return;
    
    ThemeColors *theme = &themes[current_theme];
    cairo_set_source_rgb(back_cr, theme->bg_r, theme->bg_g, theme->bg_b);
    cairo_paint(back_cr);
    
    // Draw swipe path if active (though not very useful on keypad)
    enhanced_draw_swipe_path();
    
    int btn_w = scaled_btn_width();
    int btn_h = scaled_btn_height();
    int pad = scaled_btn_padding();
    
    for (int row = 0; row < NUM_ROWS; row++) {
        int current_x = pad;
        int y = pad + row * (btn_h + pad);
        
        for (int col = 0; col < NUM_COLS; col++) {
            ButtonDef *btn = &numeric_keyboard[row][col];
            int btn_width = (btn->width ? btn->width : 1) * (btn_w + pad) - pad;
            
            // Determine if this button is pressed or hovered
            // We need to maintain pressed_row, pressed_col for numeric mode? For simplicity, we can reuse global variables.
            // But since numeric mode has its own grid, we'll map row/col to separate variables if needed.
            // For now, we'll store pressed_row/col as -1 when not in numeric mode, and when in numeric mode we'll set them to row/col of numeric grid.
            // However, the existing pressed_row/col are used for normal mode and may conflict. We'll add separate variables for numeric mode.
            // But to keep changes minimal, we'll assume that when current_mode == MODE_NUMERIC, pressed_row/col are interpreted as numeric indices.
            // The drawing of button uses pressed_row/col to determine pressed state. So we need to treat them as numeric grid indices.
            // We'll also need a way to know which grid we are in. We'll use a separate flag `is_numeric_mode`? Actually we have current_mode.
            // So in draw_numeric_keyboard, we'll interpret pressed_row and pressed_col as numeric grid indices (row in 0..NUM_ROWS-1, col 0..NUM_COLS-1).
            // We'll also need to store hover_row/col similarly. We'll reuse the same variables but they will be set differently in numeric mode.
            Bool is_pressed = (pressed_row == row && pressed_col == col);
            Bool is_hovered = (hover_row == row && hover_col == col);
            
            // Draw button using a helper that doesn't rely on keyboard array
            // We'll create a simplified draw for numeric buttons
            cairo_set_line_width(back_cr, 1.0 * scale_factor);
            
            if (is_pressed) {
                cairo_set_source_rgb(back_cr, theme->pressed_r, theme->pressed_g, theme->pressed_b);
            } else if (is_hovered) {
                cairo_set_source_rgb(back_cr, theme->hover_r, theme->hover_g, theme->hover_b);
            } else {
                cairo_set_source_rgb(back_cr, theme->btn_r, theme->btn_g, theme->btn_b);
            }
            
            double radius = fmin(btn_width, btn_h) * 0.3;
            switch (current_button_shape) {
                case SHAPE_RECTANGLE:
                    cairo_rectangle(back_cr, current_x, y, btn_width, btn_h);
                    break;
                case SHAPE_ROUNDED_RECTANGLE:
                    draw_rounded_rectangle(back_cr, current_x, y, btn_width, btn_h, radius);
                    break;
                case SHAPE_CURVE_RECTANGLE:
                    draw_curve_rectangle(back_cr, current_x, y, btn_width, btn_h, radius);
                    break;
                case SHAPE_CUP_RECTANGLE:
                    draw_cup_rectangle(back_cr, current_x, y, btn_width, btn_h, radius);
                    break;
            }
            cairo_fill(back_cr);
            
            // Border
            if (is_pressed) {
                cairo_set_source_rgb(back_cr, 1.0, 0.0, 0.0);
            } else if (is_hovered) {
                cairo_set_source_rgb(back_cr, 0.0, 0.7, 1.0);
            } else {
                cairo_set_source_rgb(back_cr, theme->btn_border_r, theme->btn_border_g, theme->btn_border_b);
            }
            switch (current_button_shape) {
                case SHAPE_RECTANGLE:
                    cairo_rectangle(back_cr, current_x, y, btn_width, btn_h);
                    break;
                case SHAPE_ROUNDED_RECTANGLE:
                    draw_rounded_rectangle(back_cr, current_x, y, btn_width, btn_h, radius);
                    break;
                case SHAPE_CURVE_RECTANGLE:
                    draw_curve_rectangle(back_cr, current_x, y, btn_width, btn_h, radius);
                    break;
                case SHAPE_CUP_RECTANGLE:
                    draw_cup_rectangle(back_cr, current_x, y, btn_width, btn_h, radius);
                    break;
            }
            cairo_stroke(back_cr);
            
            // Text
            if (btn->font_option == 2) {
                cairo_set_font_face(back_cr, custom_font_face);
            } else {
                cairo_select_font_face(back_cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            }
            int font_size = scaled_font_size(btn->fontsize);
            cairo_set_font_size(back_cr, font_size);
            
            // Special highlight for mode button in numeric mode
            if (strcmp(btn->label, "🖮") == 0) {
                switch (current_mode) {
                    case MODE_NORMAL: cairo_set_source_rgb(back_cr, 0.0, 0.8, 0.0); break;
                    case MODE_AUTO_SHRINK: cairo_set_source_rgb(back_cr, 1.0, 0.5, 0.0); break;
                    case MODE_RIBBON: cairo_set_source_rgb(back_cr, 0.0, 0.5, 1.0); break;
                    case MODE_NUMERIC: cairo_set_source_rgb(back_cr, 0.8, 0.0, 0.8); break;
                }
            } else {
                cairo_set_source_rgb(back_cr, theme->text_r, theme->text_g, theme->text_b);
            }
            
            cairo_text_extents_t extents;
            cairo_text_extents(back_cr, btn->label, &extents);
            double text_x = current_x + (btn_width - extents.width) / 2;
            double text_y = y + (btn_h + extents.height) / 2 + btn->yshft;
            cairo_move_to(back_cr, text_x, text_y);
            cairo_show_text(back_cr, btn->label);
            
            current_x += btn_width + pad;
        }
    }
    
    cairo_set_source_surface(main_cr, back_surface, 0, 0);
    cairo_paint(main_cr);
    cairo_surface_flush(main_surface);
    XFlush(dpy);
}

// ====================================================================
// Drawing Functions (modified)
// ====================================================================

void draw_keyboard() {
    if (current_mode == MODE_NUMERIC) {
        draw_numeric_keyboard();
        return;
    }
    
    if (!back_cr) return;
    
    ThemeColors *theme = &themes[current_theme];
    cairo_set_source_rgb(back_cr, theme->bg_r, theme->bg_g, theme->bg_b);
    cairo_paint(back_cr);
    
    enhanced_draw_swipe_path();

    int start_row = 0;
    int end_row = ROWS;
    
    if (current_mode == MODE_RIBBON) {
        start_row = 0;
        end_row = 1;
    }
    
    Bool arabic = is_arabic_layout();
    
    for (int row = start_row; row < end_row; row++) {
        int current_x = scaled_btn_padding();
        if (row == 0) current_x += scaled_btn_width() + scaled_btn_padding();

        for (int col = 0; col < COLS; col++) {
            if (keyboard[row][col].label == NULL) continue;
            
            int btn_width = (keyboard[row][col].width ? keyboard[row][col].width : 1) * 
                          (scaled_btn_width() + scaled_btn_padding()) - scaled_btn_padding();
            int y = scaled_btn_padding() + row * (scaled_btn_height() + scaled_btn_padding());
            
            char display_text[10] = "";
            const char *label = keyboard[row][col].label;
            
            // For Arabic layout, try to get Arabic label
            if (arabic) {
                const char *arabic_label = NULL;
                // First check if it's a letter key with predefined mapping
                if (keyboard[row][col].is_letter && arabic_labels[row][col] != NULL) {
                    arabic_label = arabic_labels[row][col];
                } else {
                    // Non‑letter key: try mapping based on English label
                    const char *eng_label = (shift_active && keyboard[row][col].shift_label) ? 
                                             keyboard[row][col].shift_label : keyboard[row][col].label;
                    arabic_label = get_arabic_label(eng_label, shift_active);
                }
                if (arabic_label) {
                    strncpy(display_text, arabic_label, sizeof(display_text)-1);
                    display_text[sizeof(display_text)-1] = '\0';
                } else {
                    // Fallback to English behaviour
                    if (shift_active && keyboard[row][col].shift_label)
                        label = keyboard[row][col].shift_label;
                    strncpy(display_text, label, sizeof(display_text)-1);
                    if (keyboard[row][col].is_letter && caps_lock && !shift_active && !arabic)
                        display_text[0] = toupper(display_text[0]);
                }
            } else {
                // English layout
                if (shift_active && keyboard[row][col].shift_label)
                    label = keyboard[row][col].shift_label;
                strncpy(display_text, label, sizeof(display_text)-1);
                if (keyboard[row][col].is_letter && caps_lock && !shift_active)
                    display_text[0] = toupper(display_text[0]);
            }
            
            draw_button(current_x, y, btn_width, scaled_btn_height(), 
               display_text, keyboard[row][col].is_caps_indicator, 
               keyboard[row][col].is_shift_indicator,
               keyboard[row][col].is_swipe_indicator,
               scaled_font_size(keyboard[row][col].fontsize),
               keyboard[row][col].font_option,
               keyboard[row][col].yshft);
            
            current_x += btn_width + scaled_btn_padding();
        }
    }
    
    cairo_set_source_surface(main_cr, back_surface, 0, 0);
    cairo_paint(main_cr);
    cairo_surface_flush(main_surface);
    XFlush(dpy);
}

// ====================================================================
// Throttled Update Functions
// ====================================================================

void update_swipe_display() {
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    
    long elapsed_ms = (current_time.tv_sec - last_redraw_time.tv_sec) * 1000 +
                     (current_time.tv_usec - last_redraw_time.tv_usec) / 1000;
    
    if (elapsed_ms < REDRAW_INTERVAL && last_redraw_time.tv_sec != 0) {
        return;
    }
    
    last_redraw_time = current_time;
    
    if (swipe_state.swype_active && current_mode != MODE_NUMERIC) {
        enhanced_draw_swipe_path();
        
        cairo_set_source_surface(main_cr, back_surface, 0, 0);
        cairo_paint(main_cr);
        cairo_surface_flush(main_surface);
        XFlush(dpy);
    }
}

void update_hover_display() {
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    
    long elapsed_ms = (current_time.tv_sec - last_redraw_time.tv_sec) * 1000 +
                     (current_time.tv_usec - last_redraw_time.tv_usec) / 1000;
    
    if (swipe_state.swype_active) {
        return;
    }

    if (elapsed_ms >= REDRAW_INTERVAL || last_redraw_time.tv_sec == 0) {
        last_redraw_time = current_time;
        draw_keyboard();
    }
}

// ====================================================================
// Numeric Keypad Event Handling
// ====================================================================
void handle_numeric_button_press(int x, int y, int button) {
    int btn_w = scaled_btn_width();
    int btn_h = scaled_btn_height();
    int pad = scaled_btn_padding();
    
    for (int row = 0; row < NUM_ROWS; row++) {
        int current_x = pad;
        int btn_y = pad + row * (btn_h + pad);
        for (int col = 0; col < NUM_COLS; col++) {
            ButtonDef *btn = &numeric_keyboard[row][col];
            int btn_width = (btn->width ? btn->width : 1) * (btn_w + pad) - pad;
            if (x >= current_x && x <= current_x + btn_width && y >= btn_y && y <= btn_y + btn_h) {
                // Button clicked
                pressed_row = row;
                pressed_col = col;
                draw_keyboard();
                XFlush(dpy);
                
                // Handle special buttons
                if (strcmp(btn->label, "✖") == 0) {
                    save_config();
                    XFreeGC(dpy, gc);
                    XDestroyWindow(dpy, win);
                    XCloseDisplay(dpy);
                    exit(0);
                } else if (strcmp(btn->label, "🖰") == 0) {
                    send_mouse_button(3);
                } else if (strcmp(btn->label, "🌐") == 0) {
                    open_web_browser("https://www.example.com");
                } else if (strcmp(btn->label, "🖮") == 0) {
                    // Cycle modes
                    if (button == 1) { // left click toggles as before
                        toggle_window_mode();
                    }
                } else if (btn->keys != NULL) {
                    send_keys(btn->keys, btn->key_count);
                }
                return;
            }
            current_x += btn_width + pad;
        }
    }
}

// ====================================================================
// Event Handlers
// ====================================================================

void enhanced_handle_swipe_button_press(int x, int y, int row, int col) {
    if (!swipe_state.swype_active) return;
    
    if (keyboard[row][col].is_letter && keyboard[row][col].label && 
        strlen(keyboard[row][col].label) == 1) {
        
        char key_char = tolower(keyboard[row][col].label[0]);
        if (isalpha(key_char)) {
            swipe_state.is_drawing = True;
            swipe_state.start_key = key_char;
            swipe_state.last_key = key_char;
            
            swipe_state.current_word_len = 0;
            if (swipe_state.current_word_len < sizeof(swipe_state.current_word)-1) {
                swipe_state.current_word[swipe_state.current_word_len++] = key_char;
                swipe_state.current_word[swipe_state.current_word_len] = '\0';
            }
            
            init_enhanced_swipe();
            
            SwipePoint start_point;
            start_point.x = x;
            start_point.y = y;
            start_point.timestamp = g_get_monotonic_time();
            start_point.nearest_key = key_char;
            start_point.distance = 0.0;
            
            if (swipe_state.swipe_points) {
                g_array_append_val(swipe_state.swipe_points, start_point);
            }
            
            if (swipe_state.activated_keys) {
                g_hash_table_add(swipe_state.activated_keys, GINT_TO_POINTER((int)key_char));
            }
            if (swipe_state.key_sequence) {
                g_array_append_val(swipe_state.key_sequence, key_char);
            }
            
            swipe_state.last_time = start_point.timestamp;
        }
    }
    
    update_swipe_display();
}

void enhanced_handle_swipe_motion(int x, int y) {
    if (!swipe_state.swype_active || !swipe_state.is_drawing) return;
    
    swipe_state.last_x = x;
    swipe_state.last_y = y;
    
    double min_distance;
    char nearest_key = find_nearest_key(x, y, &min_distance);
    
    SwipePoint point;
    point.x = x;
    point.y = y;
    point.timestamp = g_get_monotonic_time();
    point.nearest_key = nearest_key;
    point.distance = min_distance;
    
    if (swipe_state.swipe_points) {
        g_array_append_val(swipe_state.swipe_points, point);
    }
    
    if (nearest_key != '\0' && min_distance < 30.0 && swipe_state.activated_keys) {
        gpointer key_ptr = GINT_TO_POINTER((int)nearest_key);
        
        if (!g_hash_table_contains(swipe_state.activated_keys, key_ptr)) {
            g_hash_table_add(swipe_state.activated_keys, key_ptr);
            
            if (swipe_state.key_sequence) {
                if (swipe_state.key_sequence->len == 0 || 
                    g_array_index(swipe_state.key_sequence, char, swipe_state.key_sequence->len - 1) != nearest_key) {
                    char key_char = nearest_key;
                    g_array_append_val(swipe_state.key_sequence, key_char);
                }
            }
        }
    }
    
    update_swipe_display();
}

void handle_button_press(int x, int y, int button) {
    // Throttle redraws
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    long elapsed_ms = (current_time.tv_sec - last_redraw_time.tv_sec) * 1000 +
                     (current_time.tv_usec - last_redraw_time.tv_usec) / 1000;

    // Mouse wheel handling
    if (button == 4 || button == 5) {
        // Find which button is under the cursor
        int over_row = -1, over_col = -1;
        const char *over_label = NULL;
        
        if (current_mode == MODE_NUMERIC) {
            int btn_w = scaled_btn_width();
            int btn_h = scaled_btn_height();
            int pad = scaled_btn_padding();
            for (int row = 0; row < NUM_ROWS; row++) {
                int current_x = pad;
                int btn_y = pad + row * (btn_h + pad);
                for (int col = 0; col < NUM_COLS; col++) {
                    ButtonDef *btn = &numeric_keyboard[row][col];
                    int btn_width = (btn->width ? btn->width : 1) * (btn_w + pad) - pad;
                    if (x >= current_x && x <= current_x + btn_width && y >= btn_y && y <= btn_y + btn_h) {
                        over_row = row;
                        over_col = col;
                        over_label = btn->label;
                        break;
                    }
                    current_x += btn_width + pad;
                }
                if (over_row != -1) break;
            }
        } else {
            for (int row = 0; row < ROWS; row++) {
                int current_x = scaled_btn_padding();
                if (row == 0) current_x += scaled_btn_width() + scaled_btn_padding();
                for (int col = 0; col < COLS; col++) {
                    if (keyboard[row][col].label == NULL) continue;
                    int btn_width = (keyboard[row][col].width ? keyboard[row][col].width : 1) *
                                    (scaled_btn_width() + scaled_btn_padding()) - scaled_btn_padding();
                    int btn_y = scaled_btn_padding() + row * (scaled_btn_height() + scaled_btn_padding());
                    if (x >= current_x && x <= current_x + btn_width &&
                        y >= btn_y && y <= btn_y + scaled_btn_height()) {
                        over_row = row;
                        over_col = col;
                        over_label = keyboard[row][col].label;
                        break;
                    }
                    current_x += btn_width + scaled_btn_padding();
                }
                if (over_row != -1) break;
            }
        }
        
        if (over_label) {
            // Mode change on 🖮 button
            if (strcmp(over_label, "🖮") == 0) {
                if (button == 4) { // scroll up
                    current_mode = (current_mode + 1) % 4;
                } else { // scroll down
                    current_mode = (current_mode + 3) % 4;
                }
                // Resize window according to new mode
                if (current_mode == MODE_RIBBON) {
                    int ribbon_height = scaled_btn_height() + 2 * scaled_btn_padding();
                    XResizeWindow(dpy, win, scaled_win_width(), ribbon_height);
                    if (main_cr) cairo_destroy(main_cr);
                    if (main_surface) cairo_surface_destroy(main_surface);
                    if (back_cr) cairo_destroy(back_cr);
                    if (back_surface) cairo_surface_destroy(back_surface);
                    main_surface = cairo_xlib_surface_create(dpy, win, DefaultVisual(dpy, screen),
                                                           scaled_win_width(), ribbon_height);
                    main_cr = cairo_create(main_surface);
                    back_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, scaled_win_width(), ribbon_height);
                    back_cr = cairo_create(back_surface);
                } else if (current_mode == MODE_NUMERIC) {
                    int num_width = NUM_COLS * (scaled_btn_width() + scaled_btn_padding()) + scaled_btn_padding();
                    int num_height = NUM_ROWS * (scaled_btn_height() + scaled_btn_padding()) + scaled_btn_padding();
                    XResizeWindow(dpy, win, num_width, num_height);
                    if (main_cr) cairo_destroy(main_cr);
                    if (main_surface) cairo_surface_destroy(main_surface);
                    if (back_cr) cairo_destroy(back_cr);
                    if (back_surface) cairo_surface_destroy(back_surface);
                    main_surface = cairo_xlib_surface_create(dpy, win, DefaultVisual(dpy, screen),
                                                           num_width, num_height);
                    main_cr = cairo_create(main_surface);
                    back_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, num_width, num_height);
                    back_cr = cairo_create(back_surface);
                } else {
                    adjust_scale(scale_factor);
                }
                draw_keyboard();
                save_config();
                return;
            }
            // NEW: change color theme when scrolling over F2 button (row 1, col 2)
            else if (strcmp(over_label, "◐") == 0) {
                toggle_theme();
                draw_keyboard();
                save_config();
                return;
            }
            // Opacity button
            else if (strcmp(over_label, "🟫") == 0) {
                float delta = (button == 4) ? 0.05f : -0.05f;
                window_opacity += delta;
                if (window_opacity > 0.99f) window_opacity = 0.99f;
                if (window_opacity < 0.2f) window_opacity = 0.2f;
                if (net_wm_window_opacity != None) {
                    unsigned long opacity = (unsigned long)(0xFFFFFFFF * window_opacity);
                    XChangeProperty(dpy, win, net_wm_window_opacity, XA_CARDINAL, 32,
                                    PropModeReplace, (unsigned char *)&opacity, 1);
                }
                draw_keyboard();
                save_config();
                return;
            }
            // Brightness buttons
            else if (strcmp(over_label, "🟙") == 0 || strcmp(over_label, "⏾") == 0) {
                if (button == 4) increase_brightness();
                else decrease_brightness();
                return;
            }
            // Volume buttons
            else if (strcmp(over_label, "🔊") == 0 || strcmp(over_label, "🔉") == 0) {
                if (button == 4) volume_up();
                else volume_down();
                return;
            }
            // Page Up / Page Down buttons
            else if (strcmp(over_label, "⇧") == 0 || strcmp(over_label, "⇩") == 0) {
                if (button == 4) send_key(XK_Page_Up);
                else send_key(XK_Page_Down);
                return;
            }
            // Arrow Up / Down buttons
            else if (strcmp(over_label, "🠹") == 0 || strcmp(over_label, "🠻") == 0) {
                if (button == 4) send_key(XK_Up);
                else send_key(XK_Down);
                return;
            }
            // Window size buttons
            else if (strcmp(over_label, "🗚") == 0 || strcmp(over_label, "🗛") == 0) {
                if (button == 4) adjust_scale(scale_factor + 0.1f);
                else adjust_scale(scale_factor - 0.1f);
                save_config();
                return;
            }
            // Button shape toggle
            else if (strcmp(over_label, "🕹") == 0) {
                toggle_button_shape();
                return;
            }
        }
        return;
    }

    // Only left button (button 1) triggers normal key actions
    if (button != 1) return;
    
    // Handle numeric mode separately
    if (current_mode == MODE_NUMERIC) {
        handle_numeric_button_press(x, y, button);
        return;
    }

    if (swipe_state.swype_active) {
        for (int row = 0; row < ROWS; row++) {
            int current_x = scaled_btn_padding();
            
            if (row == 0) {
                current_x += scaled_btn_width() + scaled_btn_padding();
            }

            for (int col = 0; col < COLS; col++) {
                if (keyboard[row][col].label == NULL) continue;
                
                int btn_width = (keyboard[row][col].width ? keyboard[row][col].width : 1) * 
                              (scaled_btn_width() + scaled_btn_padding()) - scaled_btn_padding();
                int btn_y = scaled_btn_padding() + row * (scaled_btn_height() + scaled_btn_padding());
                
                if (x >= current_x && x <= current_x + btn_width &&
                    y >= btn_y && y <= btn_y + scaled_btn_height()) {
                    
                    if (strcmp(keyboard[row][col].label, "✖") == 0) { 
                        save_config();
                        XFreeGC(dpy, gc);
                        XDestroyWindow(dpy, win);
                        XCloseDisplay(dpy);
                        exit(0);
                    }
                    
                    if (strcmp(keyboard[row][col].label, "✍") == 0) {
                        enhanced_toggle_swipe_mode();
                        return;
                    }
                    
                    if (keyboard[row][col].is_letter) {
                        enhanced_handle_swipe_button_press(x, y, row, col);
                        return;
                    } else {
                        pressed_row = row;
                        pressed_col = col;
                        
                        if (elapsed_ms >= REDRAW_INTERVAL || last_redraw_time.tv_sec == 0) {
                            last_redraw_time = current_time;
                            draw_keyboard();
                        }
                        XFlush(dpy);
                        
                        repeat_row = row;
                        repeat_col = col;
                        gettimeofday(&repeat_start_time, NULL);
                        repeat_last_time = repeat_start_time;
                        repeat_active = True;
                        
                        // Handle all non-letter keys (same as normal mode)
                        if (row == 0 && col == 0 && keyboard[row][col].keys == NULL) {
                            toggle_theme();
                        } 
                        else if (strcmp(keyboard[row][col].label, "🟙") == 0) {
                            increase_brightness();
                        } 
                        else if (strcmp(keyboard[row][col].label, "⏾") == 0) {
                            decrease_brightness();
                        } 
                        else if (strcmp(keyboard[row][col].label, "Ar") == 0) {
                            change_kbd_layout();
                        } 
                        else if (strcmp(keyboard[row][col].label, "🕹") == 0) {
                            toggle_button_shape();
                        }
                        else if (strcmp(keyboard[row][col].label, "Caps") == 0) {
                            toggle_caps_lock();
                            send_keys(keyboard[row][col].keys, keyboard[row][col].key_count);
                        }
                        else if (strcmp(keyboard[row][col].label, "🔊") == 0) {
                            volume_up();
                        }
                        else if (strcmp(keyboard[row][col].label, "🔉") == 0) {
                            volume_down();
                        }
                        else if (strcmp(keyboard[row][col].label, "🔇") == 0) {
                            toggle_mute();
                        }
                        else if (strcmp(keyboard[row][col].label, "⮀") == 0) { 
                            toggle_dock_left();
                        }
                        else if (strcmp(keyboard[row][col].label, "Shift") == 0) {
                            struct timeval shift_time;
                            gettimeofday(&shift_time, NULL);
                            
                            if (is_double_click(&last_shift_click_time)) {
                                sticky_shift = !sticky_shift;
                                if (sticky_shift) {
                                    shift_active = True;
                                } else {
                                    shift_active = False;
                                }
                            } else {
                                shift_active = !shift_active;
                                sticky_shift = False;
                            }
                            
                            last_shift_click_time = shift_time;
                            update_modifier_state();
                            draw_keyboard();
                        }
                        else if (strcmp(keyboard[row][col].label, "Ctrl") == 0) {
                            struct timeval ctrl_time;
                            gettimeofday(&ctrl_time, NULL);
                            
                            if (is_double_click(&last_ctrl_click_time)) {
                                sticky_ctrl = !sticky_ctrl;
                                if (sticky_ctrl) {
                                    ctrl_active = True;
                                } else {
                                    ctrl_active = False;
                                }
                            } else {
                                ctrl_active = !ctrl_active;
                                sticky_ctrl = False;
                            }
                            
                            last_ctrl_click_time = ctrl_time;
                            update_modifier_state();
                            draw_keyboard();
                        }
                        else if (strcmp(keyboard[row][col].label, "Alt") == 0) {
                            struct timeval alt_time;
                            gettimeofday(&alt_time, NULL);
                            
                            if (is_double_click(&last_alt_click_time)) {
                                sticky_alt = !sticky_alt;
                                if (sticky_alt) {
                                    alt_active = True;
                                } else {
                                    alt_active = False;
                                }
                            } else {
                                alt_active = !alt_active;
                                sticky_alt = False;
                            }
                            
                            last_alt_click_time = alt_time;
                            update_modifier_state();
                            draw_keyboard();
                        }
                        else if (strcmp(keyboard[row][col].label, "📷") == 0) { 
                            take_screenshot();
                        }
                        else if (strcmp(keyboard[row][col].label, "🌐") == 0) { 
                            open_web_browser("https://www.example.com");
                        }
                        else if (strcmp(keyboard[row][col].label, "⚘") == 0) { 
                            speak_selected_text();
                        }
                        else if (strcmp(keyboard[row][col].label, "🗚") == 0) {
                            adjust_scale(scale_factor + 0.1f);
                            save_config();
                        } 
                        else if (strcmp(keyboard[row][col].label, "🗛") == 0) {
                            adjust_scale(scale_factor - 0.1f);
                            save_config();
                        }
                        else if (strcmp(keyboard[row][col].label, "🖰") == 0) {
                            send_mouse_button(3);
                        }
                        else if (strcmp(keyboard[row][col].label, "🖮") == 0) {
                            toggle_window_mode();
                            return;
                        }
                        else if (strcmp(keyboard[row][col].label, "🟫") == 0) {
                            decrease_opacity();
                        }
                        else if (strcmp(keyboard[row][col].label, ",") == 0) {
                            KeySym key_to_send;
                            if (shift_active) {
                                key_to_send = XK_less;
                            } else {
                                key_to_send = XK_comma;
                            }
                            XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, key_to_send), True, CurrentTime);
                            XFlush(dpy);
                            usleep(10000);
                            XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, key_to_send), False, CurrentTime);
                            XFlush(dpy);
                            
                            if (shift_active && !sticky_shift) {
                                shift_active = False;
                                update_modifier_state();
                            }
                            return;
                        }
                        else if (keyboard[row][col].keys != NULL) {
                            KeySym *keys_to_send = keyboard[row][col].keys;
                            int count = keyboard[row][col].key_count;
                            
                            if (shift_active) {
                                if (keyboard[row][col].shift_keys) {
                                    keys_to_send = keyboard[row][col].shift_keys;
                                    count = keyboard[row][col].key_count;
                                }
                            }
                            
                            send_keys(keys_to_send, count);
                            
                            if ((shift_active && !keyboard[row][col].is_shift_indicator) ||
                                (ctrl_active && !keyboard[row][col].is_ctrl_indicator) ||
                                (alt_active && !keyboard[row][col].is_alt_indicator)) {
                                release_all_modifiers();
                            }
                        }
                        return;
                    }
                }
                
                current_x += btn_width + scaled_btn_padding();
            }
        }
        
        // No button clicked - start dragging
        is_dragging = True;
        drag_start_x = x;
        drag_start_y = y;
        
        if (swipe_state.is_drawing) {
            swipe_state.is_drawing = False;
            if (swipe_state.swipe_points) {
                g_array_set_size(swipe_state.swipe_points, 0);
            }
            draw_keyboard();
        }
        return;
    }

    // Original non-swipe mode handling (normal, auto-shrink, ribbon)
    for (int row = 0; row < ROWS; row++) {
        int current_x = scaled_btn_padding();

        if (row == 0) {
            current_x += scaled_btn_width() + scaled_btn_padding();
        }

        for (int col = 0; col < COLS; col++) {
            if (keyboard[row][col].label == NULL) continue;
            
            int btn_width = (keyboard[row][col].width ? keyboard[row][col].width : 1) * 
                          (scaled_btn_width() + scaled_btn_padding()) - scaled_btn_padding();
            int btn_y = scaled_btn_padding() + row * (scaled_btn_height() + scaled_btn_padding());
            
            if (x >= current_x && x <= current_x + btn_width &&
                y >= btn_y && y <= btn_y + scaled_btn_height()) {
                
                pressed_row = row;
                pressed_col = col;

                if (elapsed_ms >= REDRAW_INTERVAL || last_redraw_time.tv_sec == 0) {
                    last_redraw_time = current_time;
                    draw_keyboard();
                }
                XFlush(dpy);
                
                repeat_row = row;
                repeat_col = col;
                gettimeofday(&repeat_start_time, NULL);
                repeat_last_time = repeat_start_time;
                repeat_active = True;

                // Handle special buttons
                if (row == 0 && col == 0 && keyboard[row][col].keys == NULL) {
                    toggle_theme();
                } 
                else if (strcmp(keyboard[row][col].label, "🟙") == 0) {
                    increase_brightness();
                } 
                else if (strcmp(keyboard[row][col].label, "⏾") == 0) {
                    decrease_brightness();
                } 
                else if (strcmp(keyboard[row][col].label, "Ar") == 0) {
                    change_kbd_layout();
                } 
                else if (strcmp(keyboard[row][col].label, "🕹") == 0) {
                    toggle_button_shape();
                }
                else if (strcmp(keyboard[row][col].label, "Caps") == 0) {
                    toggle_caps_lock();
                    send_keys(keyboard[row][col].keys, keyboard[row][col].key_count);
                }
                else if (strcmp(keyboard[row][col].label, "🔊") == 0) {
                    volume_up();
                }
                else if (strcmp(keyboard[row][col].label, "🔉") == 0) {
                    volume_down();
                }
                else if (strcmp(keyboard[row][col].label, "🔇") == 0) {
                    toggle_mute();
                }
                else if (strcmp(keyboard[row][col].label, "⮀") == 0) { 
                    toggle_dock_left();
                }
                else if (strcmp(keyboard[row][col].label, "Shift") == 0) {
                    struct timeval current_time;
                    gettimeofday(&current_time, NULL);
                    
                    if (is_double_click(&last_shift_click_time)) {
                        sticky_shift = !sticky_shift;
                        if (sticky_shift) {
                            shift_active = True;
                        } else {
                            shift_active = False;
                        }
                    } else {
                        shift_active = !shift_active;
                        sticky_shift = False;
                    }
                    
                    last_shift_click_time = current_time;
                    update_modifier_state();
                    draw_keyboard();
                }
                else if (strcmp(keyboard[row][col].label, "Ctrl") == 0) {
                    struct timeval current_time;
                    gettimeofday(&current_time, NULL);
                    
                    if (is_double_click(&last_ctrl_click_time)) {
                        sticky_ctrl = !sticky_ctrl;
                        if (sticky_ctrl) {
                            ctrl_active = True;
                        } else {
                            ctrl_active = False;
                        }
                    } else {
                        ctrl_active = !ctrl_active;
                        sticky_ctrl = False;
                    }
                    
                    last_ctrl_click_time = current_time;
                    update_modifier_state();
                    draw_keyboard();
                }
                else if (strcmp(keyboard[row][col].label, "Alt") == 0) {
                    struct timeval current_time;
                    gettimeofday(&current_time, NULL);
                    
                    if (is_double_click(&last_alt_click_time)) {
                        sticky_alt = !sticky_alt;
                        if (sticky_alt) {
                            alt_active = True;
                        } else {
                            alt_active = False;
                        }
                    } else {
                        alt_active = !alt_active;
                        sticky_alt = False;
                    }
                    
                    last_alt_click_time = current_time;
                    update_modifier_state();
                    draw_keyboard();
                }
                else if (strcmp(keyboard[row][col].label, "✖") == 0) { 
                    save_config();
                    XFreeGC(dpy, gc);
                    XDestroyWindow(dpy, win);
                    XCloseDisplay(dpy);
                    exit(0);
                }
                else if (strcmp(keyboard[row][col].label, "📷") == 0) { 
                    take_screenshot();
                }
                else if (strcmp(keyboard[row][col].label, "🌐") == 0) { 
                    open_web_browser("https://www.example.com");
                }
                else if (strcmp(keyboard[row][col].label, "⚘") == 0) { 
                    speak_selected_text();
                }
                else if (strcmp(keyboard[row][col].label, "🗚") == 0) {
                    adjust_scale(scale_factor + 0.1f);
                    save_config();
                } 
                else if (strcmp(keyboard[row][col].label, "🗛") == 0) {
                    adjust_scale(scale_factor - 0.1f);
                    save_config();
                }
                else if (strcmp(keyboard[row][col].label, "🖰") == 0) {
                    send_mouse_button(3);
                }
                else if (strcmp(keyboard[row][col].label, "✍") == 0) {
                    enhanced_toggle_swipe_mode();
                }
                else if (strcmp(keyboard[row][col].label, "🖮") == 0) {
                    toggle_window_mode();
                    return;
                }
                else if (strcmp(keyboard[row][col].label, "🟫") == 0) {
                    decrease_opacity();
                }
                else if (keyboard[row][col].keys != NULL) {
                    KeySym *keys_to_send = keyboard[row][col].keys;
                    int count = keyboard[row][col].key_count;
                    
                    if (shift_active) {
                        if (keyboard[row][col].shift_keys) {
                            keys_to_send = keyboard[row][col].shift_keys;
                            count = keyboard[row][col].key_count;
                        }
                    }
                    
                    send_keys(keys_to_send, count);
                    
                    if ((shift_active && !keyboard[row][col].is_shift_indicator) ||
                        (ctrl_active && !keyboard[row][col].is_ctrl_indicator) ||
                        (alt_active && !keyboard[row][col].is_alt_indicator)) {
                        release_all_modifiers();
                    }
                }
                return;
            }
            
            current_x += btn_width + scaled_btn_padding();
        }
    }
    
    is_dragging = True;
    drag_start_x = x;
    drag_start_y = y;
}

void handle_motion_notify(int x, int y) {
    last_mouse_x = x;
    last_mouse_y = y;

    if (current_mode == MODE_NUMERIC) {
        // Update hover for numeric mode
        int btn_w = scaled_btn_width();
        int btn_h = scaled_btn_height();
        int pad = scaled_btn_padding();
        int new_hover_row = -1, new_hover_col = -1;
        for (int row = 0; row < NUM_ROWS; row++) {
            int current_x = pad;
            int btn_y = pad + row * (btn_h + pad);
            for (int col = 0; col < NUM_COLS; col++) {
                ButtonDef *btn = &numeric_keyboard[row][col];
                int btn_width = (btn->width ? btn->width : 1) * (btn_w + pad) - pad;
                if (x >= current_x && x <= current_x + btn_width && y >= btn_y && y <= btn_y + btn_h) {
                    new_hover_row = row;
                    new_hover_col = col;
                    break;
                }
                current_x += btn_width + pad;
            }
            if (new_hover_row != -1) break;
        }
        if (new_hover_row != hover_row || new_hover_col != hover_col) {
            hover_row = new_hover_row;
            hover_col = new_hover_col;
            draw_keyboard();
        }
        return;
    }
    
    if (swipe_state.swype_active) {
        enhanced_handle_swipe_motion(x, y);
    } else if (is_dragging) {
        Window root;
        int new_x, new_y;
        unsigned int mask;
        
        if (XQueryPointer(dpy, win, &root, &root, &new_x, &new_y, &new_x, &new_y, &mask)) {
            int dx = x - drag_start_x;
            int dy = y - drag_start_y;
            
            XWindowAttributes attrs;
            XGetWindowAttributes(dpy, win, &attrs);
            
            XMoveWindow(dpy, win, attrs.x + dx, attrs.y + dy);
            
            window_x = attrs.x + dx;
            window_y = attrs.y + dy;
        }
        
        if (hover_row != -1 || hover_col != -1) {
            hover_row = -1;
            hover_col = -1;
        }
    } else {
        struct timeval current_time;
        gettimeofday(&current_time, NULL);
        
        long elapsed_ms = (current_time.tv_sec - last_redraw_time.tv_sec) * 1000 +
                         (current_time.tv_usec - last_redraw_time.tv_usec) / 1000;
        
        int btn_w = scaled_btn_width();
        int btn_h = scaled_btn_height();
        int pad = scaled_btn_padding();
        
        int new_hover_col = -1;
        int new_hover_row = -1;
        
        for (int row = 0; row < ROWS; row++) {
            int row_y = row * (btn_h + pad) + pad;
            if (y >= row_y && y <= row_y + btn_h) {
                int current_x = pad;
                
                if (row == 0) {
                    current_x += btn_w + pad;
                }

                for (int col = 0; col < COLS; col++) {
                    if (keyboard[row][col].label == NULL) {
                        current_x += btn_w + pad;
                        continue;
                    }
                    
                    int btn_width = (keyboard[row][col].width ? keyboard[row][col].width : 1) * 
                                  (btn_w + pad) - pad;
                    
                    if (x >= current_x && x <= current_x + btn_width) {
                        new_hover_row = row;
                        new_hover_col = col;
                        break;
                    }
                    
                    current_x += btn_width + pad;
                }
                break;
            }
        }
        
        if ((new_hover_row != hover_row || new_hover_col != hover_col) && 
            (elapsed_ms >= REDRAW_INTERVAL || last_redraw_time.tv_sec == 0)) {
            hover_row = new_hover_row;
            hover_col = new_hover_col;
            last_redraw_time = current_time;
            
            if (!swipe_state.swype_active) {
                draw_keyboard();
            }
        }
        
        if (repeat_active) {
            check_repeat();
        }
    }
}

void handle_button_release() {
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    
    long elapsed_ms = (current_time.tv_sec - last_redraw_time.tv_sec) * 1000 +
                     (current_time.tv_usec - last_redraw_time.tv_usec) / 1000;
    
    if (swipe_state.swype_active && swipe_state.is_drawing) {
        enhanced_finish_swipe();
    } else if (is_dragging) {
        is_dragging = False;
        save_config();
        if (elapsed_ms >= REDRAW_INTERVAL || last_redraw_time.tv_sec == 0) {
            last_redraw_time = current_time;
            update_hover_display();
        }
    } else if (pressed_row != -1 && pressed_col != -1) {
        pressed_row = -1;
        pressed_col = -1;
        repeat_row = -1;
        repeat_col = -1;
        repeat_active = False;
        
        if (elapsed_ms >= REDRAW_INTERVAL || last_redraw_time.tv_sec == 0) {
            last_redraw_time = current_time;
            update_hover_display();
        }
        XFlush(dpy);
    }
}

// ====================================================================
// Scaling Functions
// ====================================================================

int scaled_btn_width() {
    return (int)(BASE_BTN_WIDTH * scale_factor);
}

int scaled_btn_height() {
    return (int)(BASE_BTN_HEIGHT * scale_factor);
}

int scaled_btn_padding() {
    return (int)(BASE_BTN_PADDING * scale_factor);
}

int scaled_font_size(int base_size) {
    return (int)(base_size * scale_factor);
}

int scaled_win_width() {
    return (COLS * (scaled_btn_width() + scaled_btn_padding()) + scaled_btn_padding());
}

int scaled_win_height() {
    return (ROWS * (scaled_btn_height() + scaled_btn_padding()) + scaled_btn_padding());
}

void adjust_scale(float factor) {
    if (factor < scale_factor_min) factor = scale_factor_min;
    if (factor > scale_factor_max) factor = scale_factor_max;
    
    scale_factor = factor;
    
    int new_width, new_height;
    
    if (current_mode == MODE_RIBBON) {
        new_width = scaled_win_width();
        new_height = scaled_btn_height() + 2 * scaled_btn_padding();
    } else if (current_mode == MODE_NUMERIC) {
        new_width = NUM_COLS * (scaled_btn_width() + scaled_btn_padding()) + scaled_btn_padding();
        new_height = NUM_ROWS * (scaled_btn_height() + scaled_btn_padding()) + scaled_btn_padding();
    } else {
        new_width = scaled_win_width();
        new_height = scaled_win_height();
    }
    
    XResizeWindow(dpy, win, new_width, new_height);
    
    if (back_cr) {
        cairo_destroy(back_cr);
        back_cr = NULL;
    }
    if (back_surface) {
        cairo_surface_destroy(back_surface);
        back_surface = NULL;
    }
    if (main_cr) {
        cairo_destroy(main_cr);
        main_cr = NULL;
    }
    if (main_surface) {
        cairo_surface_destroy(main_surface);
        main_surface = NULL;
    }
    
    main_surface = cairo_xlib_surface_create(dpy, win, DefaultVisual(dpy, screen),
                                           new_width, new_height);
    main_cr = cairo_create(main_surface);
    
    back_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_width, new_height);
    back_cr = cairo_create(back_surface);
    
    XClearWindow(dpy, win);
    draw_keyboard();
    XFlush(dpy);
}

// ====================================================================
// Configuration Functions
// ====================================================================

void save_config() {
    FILE *file = fopen(CONFIG_FILE, "w");
    if (file) {
        fprintf(file, "scale_factor=%.2f\n", scale_factor);
        fprintf(file, "reverse_colors=%d\n", reverse_colors);
        fprintf(file, "window_x=%d\n", window_x);
        fprintf(file, "window_y=%d\n", window_y);
        fprintf(file, "current_mode=%d\n", current_mode);
        fprintf(file, "button_shape=%d\n", current_button_shape); 
        fprintf(file, "current_theme=%d\n", current_theme);
        fprintf(file, "window_opacity=%.2f\n", window_opacity);
        fprintf(file, "is_docked_left=%d\n", is_docked_left);
        fclose(file);
    } else {
        fprintf(stderr, "Error: Could not save configuration to %s\n", CONFIG_FILE);
    }
}

void load_config() {
    char* config_file_path = get_config_path();

    FILE *file = fopen(CONFIG_FILE, "r");
    if (file) {
        char line[256];
        while (fgets(line, sizeof(line), file)) {
            if (strncmp(line, "scale_factor=", 13) == 0) {
                scale_factor = atof(line + 13);
            } else if (strncmp(line, "reverse_colors=", 15) == 0) {
                reverse_colors = atoi(line + 15);
            } else if (strncmp(line, "window_x=", 9) == 0) {
                window_x = atoi(line + 9);
            } else if (strncmp(line, "window_y=", 9) == 0) {
                window_y = atoi(line + 9);
            } else if (strncmp(line, "current_mode=", 13) == 0) {
                current_mode = atoi(line + 13);
            } else if (strncmp(line, "button_shape=", 13) == 0) {
                current_button_shape = atoi(line + 13);
            } else if (strncmp(line, "current_theme=", 14) == 0) {
                current_theme = atoi(line + 14);
            } else if (strncmp(line, "window_opacity=", 15) == 0) {
                window_opacity = atof(line + 15);
            } else if (strncmp(line, "is_docked_left=", 15) == 0) {
                is_docked_left = atoi(line + 15);
            }
        }
        fclose(file);
        
        if (scale_factor < scale_factor_min) scale_factor = scale_factor_min;
        if (scale_factor > scale_factor_max) scale_factor = scale_factor_max;
        if (current_theme >= NUM_THEMES) current_theme = THEME_DARK;
    } 
}

char* get_config_path(void) {
    static char config_path[1024];
    
    char* appdir = getenv("APPDIR");
    if (appdir != NULL) {
        snprintf(config_path, sizeof(config_path), "%s/usr/share/virtual-keyboard/keyb_config.txt", appdir);
        if (access(config_path, F_OK) == 0) {
            return config_path;
        }
    }
    
    return CONFIG_FILE;
}

// ====================================================================
// Brightness Control Functions
// ====================================================================

static int command_exists(const char *cmd) {
    char path[1024];
    snprintf(path, sizeof(path), "/usr/bin/%s", cmd);
    if (access(path, X_OK) == 0) return 1;
    snprintf(path, sizeof(path), "/bin/%s", cmd);
    if (access(path, X_OK) == 0) return 1;
    return 0;
}

void increase_brightness() {
    // Increase by 10%, respecting max 100%
    if (brightness >= 0.2f && brightness <= 1.1f) {
        brightness += 0.1;
    }
    if (command_exists("xrandr")) {
        // fallback: software gamma increase (clamped to 1.2)
        //system("sudo xrandr --output HDMI-1 --brightness %.1f", brightness);
        char* result = NULL;
        result = (char*)malloc(50 * sizeof(char));
        if (result) {
            snprintf(result, 50, "xrandr --output HDMI-1 --brightness %.1f", brightness);
            system(result);
            free(result);
        }
    } else if (command_exists("light")) {
        system("sudo light -A 10");
    } else if (command_exists("xbacklight")) {
        system("sudo xbacklight +10");

    }
}

void decrease_brightness() {
    // Decrease by 10%, respecting min 5% (or 0.2 for xrandr)
    if (brightness >= 0.3f && brightness <= 1.2f) {
        brightness -= 0.1;
    }
    if (command_exists("xrandr")) {
        //system("sudo xrandr --output HDMI-1 --brightness %.1f", brightness);
        char* result = NULL;
        result = (char*)malloc(50 * sizeof(char));
        if (result) {
            snprintf(result, 50, "xrandr --output HDMI-1 --brightness %.1f", brightness);
            system(result);
            free(result);
        }
    } else if (command_exists("light")) {
        system("sudo light -U 10");
    } else if (command_exists("xbacklight")) {
        system("sudo xbacklight -10");
    }
}

// ====================================================================
// Utility Functions
// ====================================================================

void volume_up() {
    system("pactl set-sink-volume @DEFAULT_SINK@ +5% 2>/dev/null || amixer set Master 5%+ 2>/dev/null");
}

void volume_down() {
    system("pactl set-sink-volume @DEFAULT_SINK@ -5% 2>/dev/null || amixer set Master 5%- 2>/dev/null");
}

void toggle_mute() {
    system("pactl set-sink-mute @DEFAULT_SINK@ toggle 2>/dev/null || amixer set Master toggle 2>/dev/null");
}

void decrease_opacity() {
    window_opacity -= 0.1f;
    
    if (window_opacity <= 0.4f) {
        window_opacity = 0.99f;
    }
    
    if (window_opacity < 0.2f) window_opacity = 0.2f;
    if (window_opacity > 0.99f) window_opacity = 0.99f;
    
    unsigned long opacity = (unsigned long)(0xFFFFFFFF * window_opacity);
    
    XChangeProperty(dpy, win, net_wm_window_opacity, XA_CARDINAL, 32, 
                   PropModeReplace, (unsigned char *)&opacity, 1);
    
    XFlush(dpy);
    draw_keyboard();
}

void toggle_dock_left() {
    is_docked_left = !is_docked_left;
    
    if (is_docked_left) {
        XWindowAttributes attrs;
        XGetWindowAttributes(dpy, win, &attrs);
        
        int last_col_width = scaled_btn_width() + 2 * scaled_btn_padding();
        
        XMoveWindow(dpy, win, 0-attrs.width+last_col_width, attrs.y);
    } else {
        adjust_scale(scale_factor);
        XMoveWindow(dpy, win, window_x, window_y);
    }
    
    draw_keyboard();
    save_config();
}

void toggle_button_shape() {
    current_button_shape = (current_button_shape + 1) % 4;
    draw_keyboard();
    save_config();
}

void draw_rounded_rectangle(cairo_t *cr, double x, double y, double width, double height, double radius) {
    double degrees = M_PI / 180.0;
    
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees);
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees);
    cairo_arc(cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
    cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
    cairo_close_path(cr);
}

void draw_cup_rectangle(cairo_t *cr, double x, double y, double width, double height, double radius) {
    double x0 = x, y0 = y;
    double x1 = x + width, y1 = y + height;
    
    if (width/2 < radius) {
        if (height/2 < radius) {
            cairo_move_to(cr, x0, (y0 + y1)/2);
            cairo_curve_to(cr, x0, y0, x0, y0, (x0 + x1)/2, y0);
            cairo_curve_to(cr, x1, y0, x1, y0, x1, (y0 + y1)/2);
            cairo_curve_to(cr, x1, y1, x1, y1, (x1 + x0)/2, y1);
            cairo_curve_to(cr, x0, y1, x0, y1, x0, (y0 + y1)/2);
        } else {
            cairo_move_to(cr, x0, y0 + radius);
            cairo_curve_to(cr, x0, y0, x0, y0, (x0 + x1)/2, y0);
            cairo_curve_to(cr, x1, y0, x1, y0, x1, y0 + radius);
            cairo_line_to(cr, x1, y1 - radius);
            cairo_curve_to(cr, x1, y1, x1, y1, (x1 + x0)/2, y1);
            cairo_curve_to(cr, x0, y1, x0, y1, x0, y1 - radius);
        }
    } else {
        if (height/2 < radius) {
            cairo_move_to(cr, x0, (y0 + y1)/2);
            cairo_curve_to(cr, x0, y0, x0, y0, x0 + radius, y0);
            cairo_line_to(cr, x1 - radius, y0);
            cairo_curve_to(cr, x1, y0, x1, y0, x1, (y0 + y1)/2);
            cairo_curve_to(cr, x1, y1, x1, y1, x1 - radius, y1);
            cairo_line_to(cr, x0 + radius, y1);
            cairo_curve_to(cr, x0, y1, x0, y1, x0, (y0 + y1)/2);
        } else {
            cairo_move_to(cr, x0, y0 + radius);
            cairo_curve_to(cr, x0, y0, x0, y0, x0 + radius, y0);
            cairo_line_to(cr, x1 - radius, y0);
            cairo_curve_to(cr, x1, y0, x1, y0, x1, y0 + radius);
            cairo_line_to(cr, x1, y1 - radius);
            cairo_curve_to(cr, x1, y1, x1, y1, x1 - radius, y1);
            cairo_line_to(cr, x0 + radius, y1);
            cairo_curve_to(cr, x0, y1, x0, y1, x0, y1 - radius);
        }
    }
    cairo_close_path(cr);
}

void draw_curve_rectangle(cairo_t *cr, double x, double y, double width, double height, double radius) {
    double x0 = x, y0 = y;
    double x1 = x + width, y1 = y + height;
    
    if (width/2 < radius) {
        if (height/2 < radius) {
            cairo_move_to(cr, x0, (y0 + y1)/2);
            cairo_curve_to(cr, x0, y0, x0, y0, (x0 + x1)/2, y0);
            cairo_curve_to(cr, x1, y0, x1, y0, x1, (y0 + y1)/2);
            cairo_curve_to(cr, x1, y1, x1, y1, (x1 + x0)/2, y1);
            cairo_curve_to(cr, x0, y1, x0, y1, x0, (y0 + y1)/2);
        } else {
            cairo_move_to(cr, x0, y0 + radius);
            cairo_curve_to(cr, x0, y0, x0, y0, (x0 + x1)/2, y0);
            cairo_curve_to(cr, x1, y0, x1, y0, x1, y0 + radius);
            cairo_line_to(cr, x1, y1 - radius);
            cairo_curve_to(cr, x1, y1, x1, y1, (x1 + x0)/2, y1);
            cairo_curve_to(cr, x0, y1, x0, y1, x0, y1 - radius);
        }
    } else {
        if (height/2 < radius) {
            cairo_move_to(cr, x0, (y0 + y1)/2);
            cairo_curve_to(cr, x0, y0, x0, y0, x0 + radius, y0);
            cairo_line_to(cr, x1 - radius, y0);
            cairo_curve_to(cr, x1, y0, x1, y0, x1, (y0 + y1)/2);
            cairo_curve_to(cr, x1, y1, x1, y1, x1 - radius, y1);
            cairo_line_to(cr, x0 + radius, y1);
            cairo_curve_to(cr, x0, y1, x0, y1, x0, (y0 + y1)/2);
        } else {
            cairo_move_to(cr, x0, y0 + radius);
            cairo_curve_to(cr, x0, y0, x0, y0, x0 + radius, y0);
            cairo_line_to(cr, x1 - radius, y0);
            cairo_curve_to(cr, x1, y0, x1, y0, x1, y0 + radius);
            cairo_line_to(cr, x1, y1 - radius);
            cairo_curve_to(cr, x1, y1, x1, y1, x1 - radius, y1);
            cairo_line_to(cr, x0 + radius, y1);
            cairo_curve_to(cr, x0, y1, x0, y1, x0, y1 - radius);
        }
    }
    cairo_close_path(cr);
}

void send_keys(KeySym *keys, int count) {
    if (shift_active || ctrl_active || alt_active) {
        for (int i = 0; i < count; i++) {
            if (keys[i] == XK_Shift_L || keys[i] == XK_Control_L || keys[i] == XK_Alt_L) 
                continue;
            XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, keys[i]), False, CurrentTime);
        }
        XFlush(dpy);
        usleep(10000);
    }

    if (shift_active) {
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), True, CurrentTime);
        XFlush(dpy);
        usleep(10000);
    }
    if (ctrl_active) {
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Control_L), True, CurrentTime);
        XFlush(dpy);
        usleep(10000);
    }
    if (alt_active) {
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Alt_L), True, CurrentTime);
        XFlush(dpy);
        usleep(10000);
    }

    for (int i = 0; i < count; i++) {
        KeySym key_to_send = keys[i];
        
        if (caps_lock && !shift_active && key_to_send >= XK_a && key_to_send <= XK_z) {
            key_to_send = key_to_send - XK_a + XK_A;
        } 

        if (shift_active && key_to_send == XK_comma) {
            key_to_send = XK_less;
        }

        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, key_to_send), True, CurrentTime);
        XFlush(dpy);
        usleep(10000);
    }
    
    for (int i = count-1; i >= 0; i--) {
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, keys[i]), False, CurrentTime);
        XFlush(dpy);
        usleep(10000);
    }
    
    if (alt_active) {
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Alt_L), False, CurrentTime);
        XFlush(dpy);
        usleep(10000);
    }
    if (ctrl_active) {
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Control_L), False, CurrentTime);
        XFlush(dpy);
        usleep(10000);
    }
    if (shift_active) {
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), False, CurrentTime);
        XFlush(dpy);
        usleep(10000);
    }
}

void send_mouse_button(int button) {
    usleep(1000000);
    Window root_window = XDefaultRootWindow(dpy);
    
    Window root, child;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    
    if (XQueryPointer(dpy, root_window, &root, &child, &root_x, &root_y, &win_x, &win_y, &mask)) {
        XTestFakeMotionEvent(dpy, screen, root_x, root_y, CurrentTime);
        XFlush(dpy);
        usleep(10000);
        
        XTestFakeButtonEvent(dpy, button, True, CurrentTime);
        XFlush(dpy);
        usleep(10000);
        XTestFakeButtonEvent(dpy, button, False, CurrentTime);
        XFlush(dpy);
    }
}

void check_repeat() {
    if (repeat_row == -1 || repeat_col == -1 || !mouse_in_window) {
        repeat_active = False;
        return;
    }

    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    
    long elapsed_ms = (current_time.tv_sec - repeat_start_time.tv_sec) * 1000 +
                     (current_time.tv_usec - repeat_start_time.tv_usec) / 1000;
    
    long since_last_ms = (current_time.tv_sec - repeat_last_time.tv_sec) * 1000 +
                        (current_time.tv_usec - repeat_last_time.tv_usec) / 1000;
    
    if (elapsed_ms >= repeat_delay && since_last_ms >= repeat_rate) {
        Bool still_over_button = False;
        if (last_mouse_x != -1 && last_mouse_y != -1) {
            int current_x = scaled_btn_padding();
            
            for (int col = 0; col < COLS; col++) {
                if (keyboard[repeat_row][col].label == NULL) continue;
                
                int btn_width = (keyboard[repeat_row][col].width ? keyboard[repeat_row][col].width : 1) * 
                              (scaled_btn_width() + scaled_btn_padding()) - scaled_btn_padding();
                int btn_y = scaled_btn_padding() + repeat_row * (scaled_btn_height() + scaled_btn_padding());
                
                if (col == repeat_col && 
                    last_mouse_x >= current_x && last_mouse_x <= current_x + btn_width &&
                    last_mouse_y >= btn_y && last_mouse_y <= btn_y + scaled_btn_height()) {
                    still_over_button = True;
                    break;
                }
                
                current_x += btn_width + scaled_btn_padding();
            }
        }
        
        if (still_over_button) {
            ButtonDef *button = &keyboard[repeat_row][repeat_col];
            
            if (button->keys != NULL) {
                KeySym *keys_to_send = button->keys;
                int count = button->key_count;
                
                if (shift_active && button->shift_keys) {
                    keys_to_send = button->shift_keys;
                    count = button->key_count;
                }
                
                send_keys(keys_to_send, count);
            }
            
            repeat_last_time = current_time;
        } else {
            repeat_row = -1;
            repeat_col = -1;
            repeat_active = False;
        }
    }
}

Bool is_double_click(struct timeval *last_click_time) {
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    
    long elapsed_ms = (current_time.tv_sec - last_click_time->tv_sec) * 1000 +
                     (current_time.tv_usec - last_click_time->tv_usec) / 1000;
    
    return (elapsed_ms <= DOUBLE_CLICK_DELAY);
}

void toggle_colors() {
    reverse_colors = !reverse_colors;
    draw_keyboard();
    save_config();
}

void toggle_theme() {
    current_theme = (current_theme + 1) % NUM_THEMES;
    draw_keyboard();
    save_config();
}

void open_web_browser(const char *url) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        execlp("xdg-open", "xdg-open", url, NULL);
        // If xdg-open fails, try some common browsers
        execlp("firefox", "firefox", url, NULL);
        execlp("google-chrome", "google-chrome", url, NULL);
        execlp("chromium", "chromium", url, NULL);
        exit(1);
    } else if (pid > 0) {
        // Parent process - wait for child to complete
        waitpid(pid, NULL, 0);
    }
}

void create_popup_window() {
    if (popup_window) {
        XDestroyWindow(dpy, popup_window);
    }
    
    // Create as a child of the root window (makes it independent)
    popup_window = XCreateSimpleWindow(dpy, RootWindow(dpy, screen),
                                 100, 100,  // Position on screen
                                 POPUP_WIDTH, POPUP_HEIGHT, 1,
                                 BlackPixel(dpy, screen),
                                 WhitePixel(dpy, screen));
    XSelectInput(dpy, popup_window, ExposureMask | ButtonPressMask);
    XStoreName(dpy, popup_window, "About Virtual Keyboard");
    
    // Set WM_DELETE_WINDOW protocol
    XSetWMProtocols(dpy, popup_window, &wm_delete_window, 1);
    
    XMapWindow(dpy, popup_window);
    draw_popup_contents();
}

void draw_popup_contents() {
    if (!popup_window) return;
    
    // Create a temporary Cairo context for the popup
    cairo_surface_t *popup_surface = cairo_xlib_surface_create(dpy, popup_window,
                                                              DefaultVisual(dpy, screen),
                                                              POPUP_WIDTH, POPUP_HEIGHT);
    cairo_t *popup_cr = cairo_create(popup_surface);
    
    // Clear background
    cairo_set_source_rgb(popup_cr, 0.95, 0.95, 0.95);
    cairo_paint(popup_cr);
    
    // Draw title
    cairo_set_source_rgb(popup_cr, 0.2, 0.2, 0.2);
    cairo_select_font_face(popup_cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(popup_cr, 14);
    
    const char *title = "Virtual Keyboard";
    cairo_text_extents_t extents;
    cairo_text_extents(popup_cr, title, &extents);
    cairo_move_to(popup_cr, (POPUP_WIDTH - extents.width) / 2, 30);
    cairo_show_text(popup_cr, title);
    
    // Draw version
    cairo_set_font_size(popup_cr, 10);
    cairo_set_source_rgb(popup_cr, 0.4, 0.4, 0.4);
    const char *version = "Version 1.0";
    cairo_text_extents(popup_cr, version, &extents);
    cairo_move_to(popup_cr, (POPUP_WIDTH - extents.width) / 2, 50);
    cairo_show_text(popup_cr, version);
    
    // Draw website link
    cairo_set_source_rgb(popup_cr, 0.2, 0.4, 0.8);
    const char *url = "https://www.example.com";
    cairo_text_extents(popup_cr, url, &extents);
    cairo_move_to(popup_cr, (POPUP_WIDTH - extents.width) / 2, 75);
    cairo_show_text(popup_cr, url);
    
    // Underline the link
    cairo_set_line_width(popup_cr, 1);
    cairo_move_to(popup_cr, (POPUP_WIDTH - extents.width) / 2, 78);
    cairo_line_to(popup_cr, (POPUP_WIDTH + extents.width) / 2, 78);
    cairo_stroke(popup_cr);
    
    // Clean up
    cairo_destroy(popup_cr);
    cairo_surface_destroy(popup_surface);
    
    XFlush(dpy);
}

void speak_selected_text() {
    system("xclip -out -selection primary | xclip -in -selection clipboard 2>/dev/null");
    
    FILE *fp = popen("xsel --clipboard 2>/dev/null", "r");
    if (fp == NULL) {
        return;
    }
    
    char buffer[4096];
    size_t len = 0;
    char *text_to_speak = malloc(1);
    if (text_to_speak == NULL) {
        pclose(fp);
        return;
    }
    text_to_speak[0] = '\0';
    
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        char *pos;
        if ((pos = strchr(buffer, '\n')) != NULL) {
            *pos = ' ';
        }
        
        char *temp = realloc(text_to_speak, len + strlen(buffer) + 1);
        if (temp == NULL) {
            free(text_to_speak);
            pclose(fp);
            return;
        }
        text_to_speak = temp;
        strcpy(text_to_speak + len, buffer);
        len += strlen(buffer);
    }
    pclose(fp);
    
    if (len > 0) {
        char command[4096 + 256];
        snprintf(command, sizeof(command), 
                 "/home/r/myenv/bin/edge-tts --voice en-CA-LiamNeural --text \"%s\" --write-media hello.mp3 && play hello.mp3", 
                 text_to_speak);
        
        system(command);
    }
    
    free(text_to_speak);
}

void change_kbd_layout() {
    Display *display;
    char command[256];
    char *current_layout;

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Unable to open X display\n");
        return;
    }

    FILE *fp = popen("setxkbmap -query | grep layout | awk '{print $2}'", "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to get current keyboard layout\n");
        XCloseDisplay(display);
        return;
    }

    current_layout = fgets(command, sizeof(command), fp);
    pclose(fp);

    if (current_layout != NULL) {
        current_layout[strcspn(current_layout, "\n")] = 0;
    }

    if (strcmp(current_layout, "us") == 0) {
        snprintf(command, sizeof(command), "setxkbmap ar");
    } else {
        snprintf(command, sizeof(command), "setxkbmap us");
    }

    if (system(command) == -1) {
        fprintf(stderr, "Failed to execute command: %s\n", command);
    }

    XCloseDisplay(display);
}

void toggle_caps_lock() {
    caps_lock = !caps_lock;
    draw_keyboard();
}

void toggle_shrink_mode() { 
    shrink_mode = !shrink_mode;
    draw_keyboard();
    save_config();
}

void toggle_window_mode() {
    current_mode = (current_mode + 1) % 4;
    
    if (current_mode == MODE_RIBBON) {
        int ribbon_height = scaled_btn_height() + 2 * scaled_btn_padding();
        XResizeWindow(dpy, win, scaled_win_width(), ribbon_height);
        
        if (main_cr) cairo_destroy(main_cr);
        if (main_surface) cairo_surface_destroy(main_surface);
        if (back_cr) cairo_destroy(back_cr);
        if (back_surface) cairo_surface_destroy(back_surface);
        
        main_surface = cairo_xlib_surface_create(dpy, win, DefaultVisual(dpy, screen),
                                               scaled_win_width(), ribbon_height);
        main_cr = cairo_create(main_surface);
        
        back_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, scaled_win_width(), ribbon_height);
        back_cr = cairo_create(back_surface);
    } else if (current_mode == MODE_NUMERIC) {
        int num_width = NUM_COLS * (scaled_btn_width() + scaled_btn_padding()) + scaled_btn_padding();
        int num_height = NUM_ROWS * (scaled_btn_height() + scaled_btn_padding()) + scaled_btn_padding();
        XResizeWindow(dpy, win, num_width, num_height);
        if (main_cr) cairo_destroy(main_cr);
        if (main_surface) cairo_surface_destroy(main_surface);
        if (back_cr) cairo_destroy(back_cr);
        if (back_surface) cairo_surface_destroy(back_surface);
        main_surface = cairo_xlib_surface_create(dpy, win, DefaultVisual(dpy, screen),
                                               num_width, num_height);
        main_cr = cairo_create(main_surface);
        back_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, num_width, num_height);
        back_cr = cairo_create(back_surface);
    } else {
        adjust_scale(scale_factor);
    }
    
    draw_keyboard();
    save_config();
}

void update_modifier_state() {
    if (shift_active) {
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), True, CurrentTime);
    } else {
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), False, CurrentTime);
    }
    
    if (ctrl_active) {
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Control_L), True, CurrentTime);
    } else {
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Control_L), False, CurrentTime);
    }
    
    if (alt_active) {
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Alt_L), True, CurrentTime);
    } else {
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Alt_L), False, CurrentTime);
    }
    
    XFlush(dpy);
}

void release_all_modifiers() {
    if (shift_active && !sticky_shift) {
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), False, CurrentTime);
        shift_active = False;
    }
    if (ctrl_active && !sticky_ctrl) {
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Control_L), False, CurrentTime);
        ctrl_active = False;
    }
    if (alt_active && !sticky_alt) {
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Alt_L), False, CurrentTime);
        alt_active = False;
    }
    XFlush(dpy);
    draw_keyboard();
}

void handle_mouse_enter() {
    mouse_in_window = True;
    load_config();
    
    if (current_mode == MODE_AUTO_SHRINK) {
        adjust_scale(scale_factor);
    }
}

void handle_mouse_exit() {
    mouse_in_window = False;
    
    if (hover_row != -1 || hover_col != -1) {
        hover_row = -1;
        hover_col = -1;
        draw_keyboard();
    }
    
    repeat_row = -1;
    repeat_col = -1;
    repeat_active = False;
    
    if (pressed_row != -1 || pressed_col != -1) {
        pressed_row = -1;
        pressed_col = -1;
        draw_keyboard();
    }

    if (current_mode == MODE_AUTO_SHRINK && scale_factor > scale_factor_min && !is_dragging) {
        if (!is_window_hidden_left()) {
            adjust_scale(scale_factor_min);
        }
    }
}

Bool is_window_hidden_left() {
    XWindowAttributes attrs;
    XGetWindowAttributes(dpy, win, &attrs);
    
    return (attrs.x + scaled_btn_width() < 27) || (attrs.y + scaled_btn_height() < 27); 
}

void make_window_always_on_top() {
    Atom net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom net_wm_state_above = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
    
    XChangeProperty(dpy, win, net_wm_state, XA_ATOM, 32, PropModeReplace,
                   (unsigned char *)&net_wm_state_above, 1);
    
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    XChangeWindowAttributes(dpy, win, CWOverrideRedirect, &attrs);
    
    XSetTransientForHint(dpy, win, RootWindow(dpy, screen));
    
    XRaiseWindow(dpy, win);
    XFlush(dpy);
}

void set_window_icon() {
    XChangeProperty(dpy, win, XInternAtom(dpy, "_NET_WM_ICON_NAME", False),
                   XA_STRING, 8, PropModeReplace, (unsigned char *)"⌨", 3);
    XStoreName(dpy, win, "⌨ Virtual Keyboard");
}

void save_screenshot_png(const char *filename, int x, int y, int width, int height) {
    Display *screenshot_display = XOpenDisplay(NULL);
    if (!screenshot_display) {
        fprintf(stderr, "Cannot open display for screenshot\n");
        return;
    }
    
    int screenshot_screen = DefaultScreen(screenshot_display);
    Window root_window = RootWindow(screenshot_display, screenshot_screen);
    
    XImage *image = XGetImage(screenshot_display, root_window, x, y, width, height, AllPlanes, ZPixmap);
    
    if (!image) {
        fprintf(stderr, "Failed to capture screen area\n");
        XCloseDisplay(screenshot_display);
        return;
    }
    
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Could not open file for writing: %s\n", filename);
        XDestroyImage(image);
        XCloseDisplay(screenshot_display);
        return;
    }
    
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fclose(fp);
        XDestroyImage(image);
        XCloseDisplay(screenshot_display);
        return;
    }
    
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(fp);
        XDestroyImage(image);
        XCloseDisplay(screenshot_display);
        return;
    }
    
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        XDestroyImage(image);
        XCloseDisplay(screenshot_display);
        return;
    }
    
    png_init_io(png_ptr, fp);
    
    png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    
    png_write_info(png_ptr, info_ptr);
    
    png_bytep row = (png_bytep) malloc(3 * width * sizeof(png_byte));
    
    for (int y_pos = 0; y_pos < height; y_pos++) {
        for (int x_pos = 0; x_pos < width; x_pos++) {
            unsigned long pixel = XGetPixel(image, x_pos, y_pos);
            
            unsigned int blue = (pixel & image->blue_mask) >> 0;
            unsigned int green = (pixel & image->green_mask) >> 8;
            unsigned int red = (pixel & image->red_mask) >> 16;
            
            red = (red * 255) / ((image->red_mask >> 16) & 0xFF);
            green = (green * 255) / ((image->green_mask >> 8) & 0xFF);
            blue = (blue * 255) / (image->blue_mask & 0xFF);
            
            red = red > 255 ? 255 : red;
            green = green > 255 ? 255 : green;
            blue = blue > 255 ? 255 : blue;
            
            row[x_pos*3 + 0] = red;
            row[x_pos*3 + 1] = green;
            row[x_pos*3 + 2] = blue;
        }
        png_write_row(png_ptr, row);
    }
    
    png_write_end(png_ptr, NULL);
    
    free(row);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    XDestroyImage(image);
    XCloseDisplay(screenshot_display);
}

char* generate_screenshot_filename() {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    
    char *filename = malloc(256);
    snprintf(filename, 256, "screen_%04d%02d%02d_%02d%02d%02d.png",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);
    
    return filename;
}

void take_screenshot() {
    XUnmapWindow(dpy, win);
    XFlush(dpy);
    
    usleep(100000);
    
    int screen_width = DisplayWidth(dpy, screen);
    int screen_height = DisplayHeight(dpy, screen);
    
    char *filename = generate_screenshot_filename();
    save_screenshot_png(filename, 0, 0, screen_width, screen_height);
    free(filename);
    
    XMapWindow(dpy, win);
    XFlush(dpy);
    
    draw_keyboard();
}

void cleanup_svg() {
    if (svg_cr) {
        cairo_destroy(svg_cr);
        svg_cr = NULL;
    }
    if (svg_surface) {
        cairo_surface_destroy(svg_surface);
        svg_surface = NULL;
    }
    if (svg_handle) {
        g_object_unref(svg_handle);
        svg_handle = NULL;
    }
}

int load_custom_font(const char *font_path) {
    if (font_path == NULL) {
        font_path = get_font_path();
        if (font_path == NULL) {
            fprintf(stderr, "No font path provided and could not find myfont.ttf\n");
            return 0;
        }
    }

    FT_Error error;
    
    error = FT_Init_FreeType(&ft_library);
    if (error) {
        fprintf(stderr, "FreeType initialization error\n");
        return 0;
    }
    
    error = FT_New_Face(ft_library, font_path, 0, &ft_face);
    if (error) {
        fprintf(stderr, "Failed to load font: %s\n", font_path);
        FT_Done_FreeType(ft_library);
        return 0;
    }
    
    custom_font_face = cairo_ft_font_face_create_for_ft_face(ft_face, 0);
    if (cairo_font_face_status(custom_font_face) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "Failed to create Cairo font face\n");
        FT_Done_Face(ft_face);
        FT_Done_FreeType(ft_library);
        return 0;
    }
    
    return 1;
}

void cleanup_font() {
    if (custom_font_face) {
        cairo_font_face_destroy(custom_font_face);
        custom_font_face = NULL;
    }
    if (ft_face) {
        FT_Done_Face(ft_face);
        ft_face = NULL;
    }
    if (ft_library) {
        FT_Done_FreeType(ft_library);
        ft_library = NULL;
    }
}

char* get_font_path() {
    static char font_path[1024];
    
    char* appdir = getenv("APPDIR");
    if (appdir != NULL) {
        snprintf(font_path, sizeof(font_path), "%s/usr/share/fonts/truetype/myfont.ttf", appdir);
        if (access(font_path, F_OK) == 0) {
            return font_path;
        }
    }
    
    if (access("myfont.ttf", F_OK) == 0) {
        strcpy(font_path, "myfont.ttf");
        return font_path;
    }
    
    return NULL;
}

// ====================================================================
// Window Visibility Check (New)
// ====================================================================
void check_window_visibility_and_relocate() {
    // Get screen dimensions
    int screen_width = DisplayWidth(dpy, screen);
    int screen_height = DisplayHeight(dpy, screen);

    // Get current window attributes (position and size)
    XWindowAttributes attrs;
    XGetWindowAttributes(dpy, win, &attrs);
    int win_x = attrs.x;
    int win_y = attrs.y;
    int win_w = attrs.width;
    int win_h = attrs.height;

    // Check if window is completely off-screen
    int off_left   = (win_x + win_w <= 0);
    int off_right  = (win_x >= screen_width);
    int off_top    = (win_y + win_h <= 0);
    int off_bottom = (win_y >= screen_height);

    if (off_left || off_right || off_top || off_bottom) {
        // Relocate to middle of first quarter (top-left quadrant)
        int quarter_x = screen_width / 4;
        int quarter_y = screen_height / 4;
        int new_x = quarter_x - win_w / 2;
        int new_y = quarter_y - win_h / 2;

        // Ensure window stays inside screen edges (optional, but safe)
        if (new_x < 0) new_x = 0;
        if (new_y < 0) new_y = 0;
        if (new_x + win_w > screen_width) new_x = screen_width - win_w;
        if (new_y + win_h > screen_height) new_y = screen_height - win_h;

        XMoveWindow(dpy, win, new_x, new_y);
        XFlush(dpy);

        // Update global position variables used for config saving
        window_x = new_x;
        window_y = new_y;

        // Force a redraw to reflect new position (not strictly needed but safe)
        draw_keyboard();
    }
}

void debug_key_mapping() {
    KeyCode comma_code = XKeysymToKeycode(dpy, XK_comma);
    KeyCode less_code = XKeysymToKeycode(dpy, XK_less);
    
    printf("Comma keycode: %d\n", comma_code);
    printf("Less keycode: %d\n", less_code);
    printf("Are they the same? %s\n", comma_code == less_code ? "YES" : "NO");
}

// ====================================================================
// Main Function
// ====================================================================

int main() {
    load_config();
    
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }

    screen = DefaultScreen(dpy);
    win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen),
                     window_x, window_y, WIN_WIDTH, WIN_HEIGHT, 1,
                     BlackPixel(dpy, screen), WhitePixel(dpy, screen));
    
    set_window_icon();
    
    wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete_window, 1);
    
    make_window_always_on_top();
    init_xkb_monitor();
    init_arabic_labels();   // Initialize Arabic mapping for letter keys
    
    net_wm_window_opacity = XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
    if (net_wm_window_opacity == None) {
        fprintf(stderr, "Warning: _NET_WM_WINDOW_OPACITY not available, opacity control disabled\n");
    }
    
    XSelectInput(dpy, win, ExposureMask | ButtonPressMask | 
            ButtonReleaseMask | ButtonMotionMask | 
            EnterWindowMask | LeaveWindowMask);
    
    main_surface = cairo_xlib_surface_create(dpy, win, 
                                           DefaultVisual(dpy, screen),
                                           WIN_WIDTH, WIN_HEIGHT);
    main_cr = cairo_create(main_surface);
    
    back_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, WIN_WIDTH, WIN_HEIGHT);
    back_cr = cairo_create(back_surface);
    
    XWindowAttributes attrs;
    XGetWindowAttributes(dpy, win, &attrs);
    normal_width = attrs.width;
    normal_height = attrs.height;
    shrunk_width = normal_width / 10;
    shrunk_height = normal_height / 10;

    if (!load_custom_font(get_font_path())) {
        fprintf(stderr, "Using fallback font (Sans)\n");
    }

    cairo_font_options_t *font_options = cairo_font_options_create();
    cairo_font_options_set_antialias(font_options, CAIRO_ANTIALIAS_SUBPIXEL);
    cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_FULL);
    cairo_font_options_set_hint_metrics(font_options, CAIRO_HINT_METRICS_ON);
    cairo_set_font_options(main_cr, font_options);
    cairo_font_options_destroy(font_options);
    
    adjust_scale(scale_factor);
    
    if (net_wm_window_opacity != None) {
        unsigned long opacity = (unsigned long)(0xFFFFFFFF * window_opacity);
        XChangeProperty(dpy, win, net_wm_window_opacity, XA_CARDINAL, 32, 
                       PropModeReplace, (unsigned char *)&opacity, 1);
    }
    
    save_config();

    XMapWindow(dpy, win);
    
    int event_base, error_base, major, minor;
    if (!XTestQueryExtension(dpy, &event_base, &error_base, &major, &minor)) {
        fprintf(stderr, "XTest extension not available\n");
        cleanup_svg();
        cairo_destroy(main_cr);
        cairo_surface_destroy(main_surface);
        cairo_destroy(back_cr);
        cairo_surface_destroy(back_surface);
        XCloseDisplay(dpy);
        return 1;
    }
    
    fd_set read_fds;
    struct timeval timeout;

    draw_keyboard();
    XFlush(dpy);

    XEvent ev;
    struct timeval last_vis_check = {0, 0};  // for periodic visibility check

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(ConnectionNumber(dpy), &read_fds);
        
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000;
        
        int ready = select(ConnectionNumber(dpy) + 1, &read_fds, NULL, NULL, &timeout);
        
        if (ready > 0) {
            while (XPending(dpy)) {
                XNextEvent(dpy, &ev);
                switch (ev.type) {
                    case Expose:
                        if (ev.xexpose.count == 0) {
                            draw_keyboard();
                            adjust_scale(scale_factor);
                            if (is_dragging) save_config();
                        } else if (ev.xexpose.window == popup_window) {
                            draw_popup_contents();
                        }
                        break;

                    case ButtonPress:
                        if (ev.xbutton.button == 1 || ev.xbutton.button == 4 || ev.xbutton.button == 5)
                            handle_button_press(ev.xbutton.x, ev.xbutton.y, ev.xbutton.button);
                        break;
                        
                    case ButtonRelease:
                        if (ev.xbutton.button == 1)
                            handle_button_release();
                        break;

                    case MotionNotify:
                        last_mouse_x = ev.xmotion.x; 
                        last_mouse_y = ev.xmotion.y;
                        mouse_in_window = True;
                        handle_motion_notify(ev.xmotion.x, ev.xmotion.y);
                        break;

                    case EnterNotify:
                        mouse_in_window = True;
                        last_mouse_x = ev.xcrossing.x;
                        last_mouse_y = ev.xcrossing.y;
                        handle_mouse_enter();
                        break;
                        
                    case LeaveNotify:
                        mouse_in_window = False;
                        repeat_row = -1;
                        repeat_col = -1;
                        repeat_active = False;
                        handle_mouse_exit();
                        break;
                    
                    case ClientMessage:
                        if (ev.xclient.data.l[0] == wm_delete_window) {
                            if (ev.xclient.window == win) {
                                release_all_modifiers();
                                save_config();
                                cleanup_enhanced_swipe();
                                if (popup_window) XDestroyWindow(dpy, popup_window);
                                cleanup_font(); 
                                cleanup_svg();
                                cairo_destroy(main_cr);
                                cairo_surface_destroy(main_surface);
                                cairo_destroy(back_cr);
                                cairo_surface_destroy(back_surface);
                                XFreeGC(dpy, gc);
                                XDestroyWindow(dpy, win);
                                XCloseDisplay(dpy);
                                exit(0);
                            } else if (ev.xclient.window == popup_window) {
                                XDestroyWindow(dpy, popup_window);
                                popup_window = 0;
                            }
                        }
                        break;

                    default:
                        if (ev.type == xkb_event_base) {
                            XkbEvent *xkbe = (XkbEvent*)&ev;
                            if (xkbe->any.xkb_type == XkbStateNotify) {
                                int new_group = xkbe->state.group;
                                if (new_group != current_xkb_group) {
                                    current_xkb_group = new_group;
                                    strncpy(current_layout_name, get_layout_name(current_xkb_group), sizeof(current_layout_name) - 1);
                                    draw_keyboard();  // Refresh keyboard with new layout
                                }
                            }
                        }
                        break;
                }
            }
        }
        
        // Periodic visibility check (every 1 second)
        struct timeval now;
        gettimeofday(&now, NULL);
        long elapsed_ms = (now.tv_sec - last_vis_check.tv_sec) * 1000 +
                          (now.tv_usec - last_vis_check.tv_usec) / 1000;
        if (elapsed_ms >= 1000) {
            check_window_visibility_and_relocate();
            last_vis_check = now;
        }
        
        if (repeat_active) check_repeat();
    }

    return 0;
}