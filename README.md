# keyb
Keyb is a feature‑rich on‑screen virtual keyboard designed for Linux desktop OS. It provides a full QWERTY layout with advanced input methods, customizable appearance, and system integration. Built with X11, Cairo, and FreeType, it offers a smooth, responsive experience for touch‑screen users, accessibility needs, or as a handy input tool.

# ✨ Features



# Core Keyboard
Full QWERTY layout with modifier keys (Shift, Ctrl, Alt, Caps Lock)

Multiple window modes:

Normal – full keyboard

Auto‑shrink – reduces size when mouse leaves the window

Ribbon – shows only the top row of function and utility keys

Numeric Keypad – compact numeric input

Sticky modifiers – double‑click Shift/Ctrl/Alt to latch them

Key repeat with configurable delay and rate

XKB layout monitoring – automatically adapts to system keyboard layout (supports Arabic with custom mapping)

<img width="1699" height="956" alt="screen_20260801_171944" src="https://github.com/user-attachments/assets/5534efe9-9908-4344-a00a-a94eb1a2e182" />

# Appearance
8 colour themes – Dark, Light, Blue, Green, Purple, Brown, Marine, Red

4 button shapes – Rectangle, Rounded Rectangle, Curve, Cup

Adjustable opacity (0.2 – 0.99)

Scale factor – resize the entire keyboard (0.2× – 4.0×)

Custom TrueType font support (place myfont.ttf in the working directory)

<img width="850" height="678" alt="keyb" src="https://github.com/user-attachments/assets/6472e5fc-7506-4300-a7cf-563d6b548e88" />

# Swipe Typing (Swype)
Draw continuous paths across letter keys to form words

Built‑in dictionary of 500+ common words for prediction

Real‑time path visualisation with colour and thickness

Suggestion engine – outputs the best match after swipe

Works alongside normal key input

# System Integration
Volume control – up/down/mute (via pactl or amixer)

Brightness control – uses xrandr, light, or xbacklight

Screenshot – captures full screen, saves as PNG with timestamp

Open web browser – launches xdg‑open with a default URL

Speak selected text – copies primary selection and uses edge-tts to read aloud

Mouse button simulation – right‑click at current cursor position

Dock to left edge – hides most of the window, only a thin strip remains

# Configuration
All settings persist across sessions in keyb_config.txt (plain text)

Remembers window position, scale, theme, mode, opacity, and more

# Window Management
Always on top – stays above other windows

Drag to move – click and drag the window

Auto‑relocate – if the window goes off‑screen, it is repositioned to the top‑left quadrant

# Dependencies
To build and run Keyb, you need the following development libraries:

Library	Purpose
libX11	X11 display and events
libXext	X11 extensions
libXtst	XTest – keyboard/mouse simulation
libXkb	Keyboard layout monitoring
Cairo	2D rendering (cairo‑xlib)
librsvg‑2	SVG support (optional)
FreeType2	Font rendering
libpng	Screenshot PNG output
GLib	Data structures (GArray, GHashTable)
Cairo‑FT	Cairo + FreeType integration
libxcb	(indirect, via X11)
Runtime tools (optional but recommended):

pactl or amixer – audio control

xrandr, light, or xbacklight – brightness

xdg‑open – web browser launcher

xclip or xsel – text selection clipboard

edge-tts + play – text‑to‑speech (from sox)

# Installation
1. Clone the repository
bash
git clone https://github.com/yourusername/keyb.git
cd keyb
2. Install dependencies (Debian/Ubuntu example)
bash
sudo apt install build-essential libx11-dev libxext-dev libxtst-dev libxkbcommon-dev \
                 libcairo2-dev librsvg2-dev libfreetype6-dev libpng-dev libglib2.0-dev
3. Compile
bash
gcc -o keyb keyb.c -lX11 -lXtst -lcairo -lrsvg-2 -lfreetype -lpng -lglib-2.0 -lm -lXext -lXkb -lcairo-xlib -lfontconfig -lX11-xcb -lxcb
(You may need to adjust flags for your distribution.)

4. (Optional) Place a custom font
Put a TrueType font file named myfont.ttf in the same directory as the binary, or in usr/share/fonts/truetype/ relative to an AppDir.

5. Run
bash
./keyb
Or use the provided launcher script:

bash
./keyb.sh

# Configuration
Settings are stored in keyb_config.txt in the current directory. You can edit it manually while the program is not running, or use the keyboard’s interface to change options (they are saved automatically).

Example configuration:

text
scale_factor=1.20
reverse_colors=0
window_x=100
window_y=100
current_mode=0
button_shape=0
current_theme=0
window_opacity=0.92
is_docked_left=0

# Usage Tips
Drag window – click on any empty area and drag.

Double‑click modifiers – toggles sticky mode (Shift, Ctrl, Alt).

Right‑click (mouse button 3) – simulated via the 🖰 key.

Swipe typing – enable with the ✍ button, then drag over letters; release to insert the predicted word.

Change modes – click the 🖮 button to cycle through Normal → Auto‑shrink → Ribbon → Numeric keypad.

Change themes – click the ◐ button to cycle through 8 colour schemes.

Change button shape – click 🕹 to cycle through four shapes.

Dock left – click ⮀ to attach the window to the left edge (only a sliver remains).

Opacity cycle – click 🟫 to decrease opacity; wrap‑around to full opacity.

Screenshot – press 📷 to capture the entire screen (saves as screen_YYYYMMDD_HHMMSS.png).

# Keyboard Layouts
Keyb monitors the active XKB group and automatically adjusts the displayed labels.
It includes built‑in Arabic mapping for both letter and most punctuation keys when the layout is set to Arabic (e.g., setxkbmap ar).
To switch between US and Arabic layouts, use the Ar button (top row) – it runs setxkbmap us or setxkbmap ar as appropriate.

# Screenshots
Placeholder – insert your own screenshots here:

Normal mode (Dark theme)

Ribbon mode

Numeric keypad

Swipe in action

# Contributing
Contributions are welcome! If you find a bug or have an idea for a new feature, please open an issue or submit a pull request.
Please follow the existing coding style and ensure your changes compile without warnings.

# License
This project is licensed under the MIT License – see the LICENSE file for details.

# Acknowledgements
Built with Cairo and FreeType

Inspired by various on‑screen keyboards and swipe input methods

Thanks to the X11 and open‑source community for the essential libraries

Enjoy typing with Keyb!
For questions or support, please open an issue on GitHub.
