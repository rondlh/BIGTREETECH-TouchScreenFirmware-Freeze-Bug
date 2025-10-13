#include "Terminal.h"
#include "includes.h"

#define MAX_GCODE_COUNT        5  // gcode history slots
#define MAX_PAGE_COUNT        64
#define MAX_TERMINAL_BUF_SIZE (NOBEYOND(600, RAM_SIZE * 45, 4800))

static uint16_t lineCounter = 0;   // count the lines drawn on a page
static uint32_t lastDataTime = 0;  // remember when we last received data
uint8_t oldPageIndex = 0;
uint16_t pageNumberColor = BAR_FONT_COLOR;

typedef struct
{
  CMD      gcodeTable[MAX_GCODE_COUNT];  // array of history gcodes
  uint8_t  gcodeIndex;                   // current history gcode index
} KEYBOARD_DATA;

typedef struct
{
  uint16_t pageTable[MAX_PAGE_COUNT];  // array to terminal page pointers within terminal buffer
  uint8_t  pageCount;                  // current page count
  uint8_t  pageHead;                   // index of first/oldest page
  uint8_t  pageTail;                   // index of last/newest page
  uint8_t  pageIndex;                  // current page index
  uint16_t bufTail;                    // last copy index for copying data in buffer
  char  lastSrc;
} TERMINAL_DATA;

typedef enum
{
  KEYBOARD_VIEW = 0,
  TERMINAL_VIEW
} TERMINAL_WINDOW;

typedef enum
{
  GKEY_PREV = 0,
  GKEY_NEXT,
  GKEY_CLEAR,
  GKEY_SEND,
  GKEY_ABC_123,
  GKEY_SPACE,
  GKEY_DEL,
  GKEY_BACK,
  // no need to declare key numbers if no special task is performed by the key
  GKEY_IDLE = IDLE_TOUCH,
} GKEY_VALUES;

typedef enum
{
  TERM_PAGE_UP = 0,
  TERM_PAGE_DOWN,
  TERM_TOGGLE_ACK,
  TERM_BACK,
  TERM_KEY_COUNT,  // number of keys
  TERM_IDLE = IDLE_TOUCH,
} TERMINAL_KEY_VALUES;

// keyboard layouts
#define LAYOUT_1_COL_COUNT 6
#define LAYOUT_1_ROW_COUNT 4

#define LAYOUT_2_COL_COUNT 7
#define LAYOUT_2_ROW_COUNT 4

#define LAYOUT_3_COL_COUNT 10
#define LAYOUT_3_ROW_COUNT  6

#if LCD_WIDTH < 480  // number of columns and rows is based on LCD display resolution
  #define KB_TYPE_STANDARD
  #define KB_COL_COUNT LAYOUT_1_COL_COUNT
  #define KB_ROW_COUNT LAYOUT_1_ROW_COUNT
#elif LCD_WIDTH < 800
  #define KB_TYPE_EXTENDED
  #define KB_COL_COUNT LAYOUT_2_COL_COUNT
  #define KB_ROW_COUNT LAYOUT_2_ROW_COUNT
#else
  #define KB_TYPE_QWERTY
  #define KB_COL_COUNT LAYOUT_3_COL_COUNT
  #define KB_ROW_COUNT LAYOUT_3_ROW_COUNT
#endif

#define LAYOUT_QWERTY 0
#define LAYOUT_QWERTZ 1
#define LAYOUT_AZERTY 2

#define GRID_ROW_COUNT (1 + KB_ROW_COUNT + 1)         // text box + keyboard rows + control bar
#define ROW_HEIGHT     (LCD_HEIGHT / GRID_ROW_COUNT)  // button height
#define CTRL_COL_COUNT 4                              // control button count for keyboard view

// keyboard key sizes
#define KEY_WIDTH  (LCD_WIDTH / KB_COL_COUNT + (0.5 * (LCD_WIDTH % KB_COL_COUNT > 0)))
#define KEY_HEIGHT ROW_HEIGHT
#define KEY_COUNT  (3 + 1 + (KB_COL_COUNT * KB_ROW_COUNT) + (CTRL_COL_COUNT))  // text box keys + send key + all keys + control bar keys

// control bar sizes
#define CTRL_WIDTH           (LCD_WIDTH / CTRL_COL_COUNT)        // control bar button width in keyboard view
#define TERMINAL_CTRL_WIDTH  (LCD_WIDTH / (TERM_KEY_COUNT + 1))  // control bar button width in terminal view + page text box
#define CTRL_HEIGHT          ROW_HEIGHT

// value text box inset padding
#define TEXTBOX_INSET 4

// text box button inset padding
#define TEXTBOX_BUTTON_INSET 2

#define COMMAND_START_ROW 0                              // row number for text box and send button
#define KB_START_ROW      1                              // row number for keyboard
#define CTRL_START_ROW    (KB_START_ROW + KB_ROW_COUNT)  // row number for control bar

// for text in terminal
#define CURSOR_H_OFFSET 2
#define CURSOR_END_Y    ((KB_START_ROW + KB_ROW_COUNT) * KEY_HEIGHT)
#define CURSOR_START_X  (terminalAreaRect[0].x0 + CURSOR_H_OFFSET)
#define CHARS_X ((terminalAreaRect[0].x1 - CURSOR_START_X) / BYTE_WIDTH)
#define LINES_Y (terminalAreaRect[0].y1 / BYTE_HEIGHT)

// gcode command draw area inside text box
static const GUI_RECT textBoxRect = {             0 + TEXTBOX_INSET, (COMMAND_START_ROW + 0) * CTRL_HEIGHT + TEXTBOX_INSET,
                                     3 * CTRL_WIDTH - TEXTBOX_INSET, (COMMAND_START_ROW + 1) * CTRL_HEIGHT - TEXTBOX_INSET};

// keyboard rectangles
static const GUI_RECT editorKeyRect[KEY_COUNT] = {
  // row text box + send button
  {0 * CTRL_WIDTH + TEXTBOX_INSET + TEXTBOX_BUTTON_INSET, (COMMAND_START_ROW + 0) * CTRL_HEIGHT + TEXTBOX_INSET + TEXTBOX_BUTTON_INSET,
   1 * CTRL_WIDTH +                 TEXTBOX_BUTTON_INSET, (COMMAND_START_ROW + 1) * CTRL_HEIGHT - TEXTBOX_INSET - TEXTBOX_BUTTON_INSET},  // Prev gcode (top row)
  {2 * CTRL_WIDTH -                 TEXTBOX_BUTTON_INSET, (COMMAND_START_ROW + 0) * CTRL_HEIGHT + TEXTBOX_INSET + TEXTBOX_BUTTON_INSET,
   3 * CTRL_WIDTH - TEXTBOX_INSET - TEXTBOX_BUTTON_INSET, (COMMAND_START_ROW + 1) * CTRL_HEIGHT - TEXTBOX_INSET - TEXTBOX_BUTTON_INSET},  // Clear gcode (top row)
  {1 * CTRL_WIDTH + (TEXTBOX_INSET / 2)                 , (COMMAND_START_ROW + 0) * CTRL_HEIGHT + TEXTBOX_INSET + TEXTBOX_BUTTON_INSET,
   2 * CTRL_WIDTH - (TEXTBOX_INSET / 2)                 , (COMMAND_START_ROW + 1) * CTRL_HEIGHT - TEXTBOX_INSET - TEXTBOX_BUTTON_INSET},  // Next gcode (top row)
  {3 * CTRL_WIDTH, COMMAND_START_ROW * CTRL_HEIGHT, 4 * CTRL_WIDTH, (COMMAND_START_ROW + 1) * CTRL_HEIGHT},  // Send (top row)

  // row control bar
  {0 * CTRL_WIDTH, (KB_START_ROW + KB_ROW_COUNT) * KEY_HEIGHT, 1 * CTRL_WIDTH, LCD_HEIGHT},  // ABC/123
  {1 * CTRL_WIDTH, (KB_START_ROW + KB_ROW_COUNT) * KEY_HEIGHT, 2 * CTRL_WIDTH, LCD_HEIGHT},  // Space
  {2 * CTRL_WIDTH, (KB_START_ROW + KB_ROW_COUNT) * KEY_HEIGHT, 3 * CTRL_WIDTH, LCD_HEIGHT},  // Del
  {3 * CTRL_WIDTH, (KB_START_ROW + KB_ROW_COUNT) * KEY_HEIGHT, 4 * CTRL_WIDTH, LCD_HEIGHT},  // Back

  // row 1
  {0 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 1 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
  {1 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 2 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
  {2 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 3 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
  {3 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 4 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
  {4 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 5 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
  {5 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 6 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
  #if KB_COL_COUNT > LAYOUT_1_COL_COUNT
    {6 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 7 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
  #endif
  #if KB_COL_COUNT > LAYOUT_2_COL_COUNT
    {7 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 8 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
    {8 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 9 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
    {9 * KEY_WIDTH, (KB_START_ROW + 0) * KEY_HEIGHT, 10 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT},
  #endif

  // row 2
  {0 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 1 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
  {1 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 2 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
  {2 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 3 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
  {3 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 4 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
  {4 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 5 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
  {5 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 6 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
  #if KB_COL_COUNT > LAYOUT_1_COL_COUNT
    {6 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 7 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
  #endif
  #if KB_COL_COUNT > LAYOUT_2_COL_COUNT
    {7 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 8 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
    {8 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 9 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
    {9 * KEY_WIDTH, (KB_START_ROW + 1) * KEY_HEIGHT, 10 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT},
  #endif

  // row 3
  {0 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 1 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
  {1 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 2 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
  {2 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 3 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
  {3 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 4 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
  {4 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 5 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
  {5 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 6 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
  #if KB_COL_COUNT > LAYOUT_1_COL_COUNT
    {6 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 7 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
  #endif
  #if KB_COL_COUNT > LAYOUT_2_COL_COUNT
    {7 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 8 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
    {8 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 9 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
    {9 * KEY_WIDTH, (KB_START_ROW + 2) * KEY_HEIGHT, 10 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT},
  #endif

  // row 4
  {0 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 1 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
  {1 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 2 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
  {2 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 3 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
  {3 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 4 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
  {4 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 5 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
  {5 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 6 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
  #if KB_COL_COUNT > LAYOUT_1_COL_COUNT
    {6 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 7 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
  #endif
  #if KB_COL_COUNT > LAYOUT_2_COL_COUNT
    {7 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 8 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
    {8 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 9 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
    {9 * KEY_WIDTH, (KB_START_ROW + 3) * KEY_HEIGHT, 10 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT},
  #endif

  #if KB_COL_COUNT > LAYOUT_2_COL_COUNT
    // row 5
    {0 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 1 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {1 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 2 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {2 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 3 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {3 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 4 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {4 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 5 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {5 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 6 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {6 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 7 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {7 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 8 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {8 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 9 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},
    {9 * KEY_WIDTH, (KB_START_ROW + 4) * KEY_HEIGHT, 10 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT},

    // row 6
    {0 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 1 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {1 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 2 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {2 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 3 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {3 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 4 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {4 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 5 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {5 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 6 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {6 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 7 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {7 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 8 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {8 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 9 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
    {9 * KEY_WIDTH, (KB_START_ROW + 5) * KEY_HEIGHT, 10 * KEY_WIDTH, (KB_START_ROW + 6) * KEY_HEIGHT},
  #endif
};

// area rectangles
static const GUI_RECT editorAreaRect[3] = {
  {0, COMMAND_START_ROW * CTRL_HEIGHT, LCD_WIDTH, ROW_HEIGHT},                // text box + send area
  {0, ROW_HEIGHT,                      LCD_WIDTH, LCD_HEIGHT - CTRL_HEIGHT},  // keyboard area
  {0, CTRL_START_ROW * CTRL_HEIGHT,    LCD_WIDTH, LCD_HEIGHT}                 // control bar area
};

static const GUI_RECT terminalKeyRect[TERM_KEY_COUNT] = {
  {1 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT - CTRL_HEIGHT, 2 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT},  // page down
  {2 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT - CTRL_HEIGHT, 3 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT},  // page up
  {3 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT - CTRL_HEIGHT, 4 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT},  // ACK
  {4 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT - CTRL_HEIGHT, 5 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT},  // Back
};

static const GUI_RECT terminalPageRect = {
  0 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT - CTRL_HEIGHT, 1 * TERMINAL_CTRL_WIDTH, LCD_HEIGHT};  // page number

static const GUI_RECT terminalAreaRect[2] = {
  {0,            0, LCD_WIDTH, CURSOR_END_Y},  // terminal area
  {0, CURSOR_END_Y, LCD_WIDTH,   LCD_HEIGHT},  // control area
};

// keyboard keys for first layout
static const char * const gcodeKey123[KEY_COUNT] = {
  "Prev", "Next", "Clear", "Send", "ABC", "Space", "Del", "Back",
  #if KB_COL_COUNT == LAYOUT_1_COL_COUNT
    "1", "2", "3", "M", "G", "T",
    "4", "5", "6", "X", "Y", "Z",
    "7", "8", "9", "E", "F", "R",
    ".", "0", "-", "/", "S", "V",
  #elif KB_COL_COUNT == LAYOUT_2_COL_COUNT
    "1", "2", "3", "M", "G", "T", "V",
    "4", "5", "6", "X", "Y", "Z", "S",
    "7", "8", "9", "E", "F", "R", "Q",
    ".", "0", "-", "/", "I", "J", "P",
  #else
    "A", "B", "C", "D", "E", "F", "G", "1", "2", "3",
    "H", "I", "J", "K", "L", "M", "N", "4", "5", "6",
    "O", "P", "Q", "R", "S", "T", "U", "7", "8", "9",
    "V", "W", "X", "Y", "Z", "(", ")", ".", "0", "-",
    "!", "@", "#", "%", "&", ",", ";", "*", "/", "+",
    "~", "`", "$","\\","\"", "'", ":", "_", "=", "?",
  #endif
};

// keyboard keys for second layout
static const char * const gcodeKeyABC[KEY_COUNT] = {
  "Prev", "Next", "Clear", "Send", "123", "Space", "Del", "Back",
  #if KB_COL_COUNT == LAYOUT_1_COL_COUNT
    "A", "B", "C", "D", "H", "I",
    "J", "K", "L", "N", "O", "P",
    ",", ";", ":", "Q", "U", "W",
    "+", "*", "?", "!", "#", "&",
  #elif KB_COL_COUNT == LAYOUT_2_COL_COUNT
    "A", "B", "C", "D", "H", "K", "L",
    ",", ";", ":", "N", "O", "U", "W",
    "+", "*", "?", "!", "#", "&", "$",
    "/", "=", "(", ")", "@", "_", "%",
  #else
    #if TERMINAL_KEYBOARD_LAYOUT == LAYOUT_QWERTY
      "!", "@", "#", "%", "&", "*", "(", ")", "-", "+",
      "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
      "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P",
      "A", "S", "D", "F", "G", "H", "J", "K", "L", ";",
      "Z", "X", "C", "V", "B", "N", "M", ",", ".", "/",
      "~", "`", "$","\\","\"", "'", ":", "_", "=", "?",
    #elif TERMINAL_KEYBOARD_LAYOUT == LAYOUT_QWERTZ
      "!", "\"", "$", "%", "&", "/", "(", ")", "=", "?",
      "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
      "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P",
      "A", "S", "D", "F", "G", "H", "J", "K", "L", "@",
      "Y", "X", "C", "V", "B", "N", "M", ",", ".", "-",
      "|", ";", ":", "_", "#", "~", "+", "*", "'", "\\",
    #elif TERMINAL_KEYBOARD_LAYOUT == LAYOUT_AZERTY
      "#", "@", "~", "&", "(", ")", "_", "'", "\"", "%",
      "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
      "A", "Z", "E", "R", "T", "Y", "U", "I", "O", "P",
      "Q", "S", "D", "F", "G", "H", "J", "K", "L", "M",
      "W", "X", "C", "V", "B", "N", ".", ",", ":", ";",
      "-", "+", "*", "\\", "|", "/", "?","!", "$", "=",
    #endif
  #endif
};

static const uint16_t fontSrcColor[3][3] = {
  // Gcode                  ACK                    Background
  {COLORSCHEME1_TERM_GCODE, COLORSCHEME1_TERM_ACK, COLORSCHEME1_TERM_BACK},  // Material Dark
  {COLORSCHEME2_TERM_GCODE, COLORSCHEME2_TERM_ACK, COLORSCHEME2_TERM_BACK},  // Material Light
  {COLORSCHEME3_TERM_GCODE, COLORSCHEME3_TERM_ACK, COLORSCHEME3_TERM_BACK},  // High Contrast
};

static KEYBOARD_DATA keybData = {{'\0'}, 0};
static uint8_t saveGcodeIndex = 0;
  
static TERMINAL_DATA * terminalData;
static char * terminalBuf;
static TERMINAL_WINDOW curView = KEYBOARD_VIEW;

static bool numpad =
  #ifdef KB_TYPE_QWERTY
    false;  // show qwerty as default for larger
  #else
    true;
  #endif

static inline void keyboardDrawButton(uint8_t index, uint8_t isPressed)
{
  if (index >= COUNT(editorKeyRect))
    return;

  // setup colors and button info
  #ifdef KEYBOARD_MATERIAL_THEME
    uint16_t fontColor = CTRL_FONT_COLOR;
    uint16_t bgColor = KEY_BG_COLOR;
    GUI_RECT rectBtn = {editorKeyRect[index].x0 + 3, editorKeyRect[index].y0 + 3,
                        editorKeyRect[index].x1 - 3, editorKeyRect[index].y1 - 3};

    switch (index)
    {
      case GKEY_SEND:
        bgColor = CTRL_SEND_BG_COLOR;
        break;

      case GKEY_BACK:
        bgColor = CTRL_BACK_BG_COLOR;
        break;

      case GKEY_ABC_123:
        bgColor = CTRL_ABC_BG_COLOR;
        break;

      default:
        if (index < GKEY_SEND)  // if pressed a key on text box
        {
          fontColor = TEXTBOX_FONT_COLOR;
          bgColor = CTRL_ABC_BG_COLOR;
        }
        else
        {
          fontColor = KEY_FONT_COLOR;
        }
        break;
    }

    if (numpad && ( gcodeKey123[index][0] > 47 ) && (gcodeKey123[index][0] < 58))   fontColor = YELLOW;
    if (numpad && ((gcodeKey123[index][0] == 71) || (gcodeKey123[index][0] == 77))) fontColor = ORANGE;

    BUTTON btn = {.fontColor  = fontColor,
                  .backColor  = bgColor,
                  .context    = (uint8_t *)((numpad) ? gcodeKey123[index] : gcodeKeyABC[index]),
                  .lineColor  = bgColor,
                  .lineWidth  = 0,
                  .pBackColor = fontColor,
                  .pFontColor = bgColor,
                  .pLineColor = fontColor,
                  .radius     = BTN_ROUND_CORNER,
                  .rect       = rectBtn};

    setFontSize(index >= GKEY_SEND ? FONT_SIZE_LARGE : FONT_SIZE_NORMAL);

    // draw button
    GUI_DrawButton(&btn, isPressed);

    if (index < GKEY_SEND)  // if key on text box, draw a status info on Send button area
    {
      char statusText[10];

      if (isPressed)  // if pressed key
      {
        if (index < GKEY_CLEAR)
        {
          for (uint8_t q = 0; q < MAX_GCODE_COUNT; q++)
          {
            keybData.gcodeIndex = (keybData.gcodeIndex + MAX_GCODE_COUNT + (2 * (index == GKEY_NEXT)) - 1) % MAX_GCODE_COUNT;
            if (keybData.gcodeTable[keybData.gcodeIndex][0])
              break;
          }
          sprintf(statusText, "%s %d/%d", gcodeKey123[index], keybData.gcodeIndex + 1, MAX_GCODE_COUNT);
        }
        else
        {
          strcpy(statusText, gcodeKey123[GKEY_CLEAR]);
        }
      }
      else  // if released key
      {
        setFontSize(FONT_SIZE_LARGE);
        strcpy(statusText, gcodeKey123[GKEY_SEND]);
      }

      rectBtn = (GUI_RECT){editorKeyRect[GKEY_SEND].x0 + 3, editorKeyRect[GKEY_SEND].y0 + 3,
                           editorKeyRect[GKEY_SEND].x1 - 3, editorKeyRect[GKEY_SEND].y1 - 3};

      fontColor = CTRL_FONT_COLOR;
      bgColor = CTRL_SEND_BG_COLOR;

      BUTTON btn2 = {.fontColor  = fontColor,
                     .backColor  = bgColor,
                     .context    = (uint8_t *) statusText,
                     .lineColor  = bgColor,
                     .lineWidth  = 0,
                     .pBackColor = fontColor,
                     .pFontColor = bgColor,
                     .pLineColor = fontColor,
                     .radius     = BTN_ROUND_CORNER,
                     .rect       = rectBtn};

      // draw button
      GUI_DrawButton(&btn2, isPressed);
    }
  #else  // KEYBOARD_MATERIAL_THEME
    uint16_t fontColor;
    uint16_t bgColor;

    if (isPressed)
    {
      if (index > GKEY_BACK)  // if pressed a key on keyboard
      {
        fontColor = KEY_BG_COLOR;
        bgColor = KEY_FONT_COLOR;
      }
      else  // if pressed a key on send area or control bar
      {
        fontColor = BAR_BG_COLOR;
        bgColor = BAR_FONT_COLOR;
      }
    }
    else
    {
      if (index > GKEY_BACK)  // if pressed a key on keyboard
      {
        fontColor = KEY_FONT_COLOR;
        bgColor = KEY_BG_COLOR;
      }
      else  // if pressed a key on send area or control bar
      {
        fontColor = BAR_FONT_COLOR;
        bgColor = BAR_BG_COLOR;
      }
    }

    drawStandardValue(&editorKeyRect[index], VALUE_STRING, (numpad) ? gcodeKey123[index] : gcodeKeyABC[index],
                      (index >= GKEY_SEND) ? FONT_SIZE_LARGE : FONT_SIZE_NORMAL, fontColor, bgColor, 1, true);

    if (index < GKEY_SEND)  // if key on text box, draw a status info on Send button area
    {
      char statusText[10];

      if (isPressed)  // if pressed key
      {
        if (index < GKEY_CLEAR)
        {
          keybData->gcodeIndex = (keybData->gcodeIndex + MAX_GCODE_COUNT + (2 * (index == GKEY_NEXT)) - 1) % MAX_GCODE_COUNT;
          sprintf(statusText, "%s %d/%d", gcodeKey123[index], keybData->gcodeIndex + 1, MAX_GCODE_COUNT);
        }
        else
        {
          strcpy(statusText, gcodeKey123[GKEY_CLEAR]);
        }

        fontColor = BAR_BG_COLOR;
        bgColor = BAR_FONT_COLOR;
      }
      else  // if released key
      {
        strcpy(statusText, gcodeKey123[GKEY_SEND]);

        fontColor = BAR_FONT_COLOR;
        bgColor = BAR_BG_COLOR;
      }

      drawStandardValue(&editorKeyRect[GKEY_SEND], VALUE_STRING, statusText, FONT_SIZE_NORMAL, fontColor, bgColor, 1, true);
    }
  #endif  // KEYBOARD_MATERIAL_THEME
}

static inline void drawGcodeText(char * gcode)
{
  drawStandardValue(&textBoxRect, VALUE_STRING, gcode, FONT_SIZE_NORMAL, TEXTBOX_FONT_COLOR, TEXTBOX_BG_COLOR, 1, true);
}

static inline void drawKeyboard(void)
{
  #ifndef KEYBOARD_MATERIAL_THEME
    GUI_SetColor(KEY_BORDER_COLOR);

    // draw vertical button borders
    for (int i = 0; i < (KB_COL_COUNT - 1); i++)
    {
      GUI_VLine(editorKeyRect[i + GKEY_BACK + 1].x1, editorAreaRect[1].y0, editorAreaRect[1].y1);
    }

    RAPID_SERIAL_LOOP();
    // draw horizontal button borders
    for (int i = 0; i < (KB_ROW_COUNT - 1); i++)
    {
      GUI_HLine(editorAreaRect[1].x0, editorKeyRect[(i * KB_COL_COUNT) + GKEY_BACK + 1].y1, editorAreaRect[1].x1);
    }
  #endif

  for (uint8_t i = GKEY_SEND; i < COUNT(gcodeKey123); i++)  // draw all the visible keys (text box keys are skipped)
  {
    keyboardDrawButton(i, false);
    RAPID_SERIAL_LOOP();
  }
}

static inline void keyboardDrawMenu(void)
{
  setMenu(MENU_TYPE_FULLSCREEN, NULL, COUNT(editorKeyRect), editorKeyRect, keyboardDrawButton, NULL);

  // clear keyboard area
  GUI_SetBkColor(KB_BG_COLOR);
  GUI_ClearPrect(&editorAreaRect[1]);

  // clear bar area
  GUI_SetBkColor(BAR_BG_COLOR);
  GUI_ClearPrect(&editorAreaRect[0]);
  GUI_ClearPrect(&editorAreaRect[2]);

  #ifndef KEYBOARD_MATERIAL_THEME
    GUI_SetColor(BAR_BORDER_COLOR);

    // draw text box area shadow border
    GUI_DrawPrect(&textBoxRect);

    // draw bar area shadow border
    GUI_HLine(editorAreaRect[0].x0, editorAreaRect[0].y1 - 1, editorAreaRect[0].x1);  // last row of bar 1 area used for shadow border
    GUI_HLine(editorAreaRect[2].x0, editorAreaRect[2].y0, editorAreaRect[2].x1);      // first row of bar 2 area used for shadow border
  #endif

  GUI_SetTextMode(GUI_TEXTMODE_TRANS);

  // draw keyboard
  drawKeyboard();
}

static inline void menuKeyboardView(void)
{
  KEY_VALUES key_num = KEY_IDLE;

  uint8_t nowIndex = 0;
  uint8_t lastIndex = 0xFF;  // trigger text box draw
  bool saveEnabled = true;
  static CMD gcodeBuf = {'\0'};

  keyboardDrawMenu();

  while ((curView == KEYBOARD_VIEW) && (MENU_IS(menuTerminal)))
  {

    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case GKEY_IDLE:
        break;

      case GKEY_PREV:
      case GKEY_NEXT:
        nowIndex = sprintf(gcodeBuf, keybData.gcodeTable[keybData.gcodeIndex]);  // load gcode from history table and update gcode size
        lastIndex = ~nowIndex;  // trigger text box redraw
        saveEnabled = true;
        break;

      case GKEY_CLEAR:
        nowIndex = 0;  // reset gcode size
        break;

      case GKEY_SEND:
        if (nowIndex)
        {
          if (saveEnabled)  // avoid saving lines again that were called from the gcode history table
          {
            strcpy(keybData.gcodeTable[saveGcodeIndex], gcodeBuf);  // save gcode to history table
            saveGcodeIndex = (saveGcodeIndex + 1) % MAX_GCODE_COUNT;     // move to next save index in the gcode history table
          }
          else
            saveGcodeIndex = keybData.gcodeIndex;

          strcpy(&gcodeBuf[nowIndex], "\n");
          handleCmd(gcodeBuf);
        }

        keybData.gcodeIndex = saveGcodeIndex;  // save and update gcode index
        curView = TERMINAL_VIEW;
        break;

      case GKEY_ABC_123:
        TOGGLE_BIT(numpad, 0);

        drawKeyboard();
        break;

      case GKEY_SPACE:
        if (nowIndex && (nowIndex < CMD_MAX_SIZE - 2))  // -2 to leave space for '\n' and '\0' char
          gcodeBuf[nowIndex++] = ' ';
        break;

      case GKEY_DEL:
        nowIndex -= !(!nowIndex);
        break;

      case GKEY_BACK:
        keybData.gcodeIndex = saveGcodeIndex;
        CLOSE_MENU();
        break;

      default: // process character keys
        if (nowIndex < CMD_MAX_SIZE - 2)  // -2 to leave space for '\n' and '\0' char
        {
          gcodeBuf[nowIndex++] = (numpad) ? gcodeKey123[key_num][0] : gcodeKeyABC[key_num][0];
          saveEnabled = true;
        }
        break;
    }

    if (lastIndex != nowIndex)
    {
      lastIndex = nowIndex;  // update gcode size
      gcodeBuf[nowIndex] = '\0';

      drawGcodeText(gcodeBuf);

      if (*gcodeBuf == '\0')  // text area empty
      {
        for (uint8_t i = 0; i < GKEY_SEND; i++)  // draw text box keys
        {
          keyboardDrawButton(i, false);
        }
      }
    }

    loopBackEnd();
  }

  // restore default
  GUI_RestoreColorDefault();
}

static inline void saveGcodeTerminalCache(const char * str, uint16_t strLen)
{
  lastDataTime = OS_GetTimeMs(); // remember the most recent data receive time

  if ((terminalData->bufTail + strLen) <= MAX_TERMINAL_BUF_SIZE)
  {
    memcpy(&terminalBuf[terminalData->bufTail], str, strLen);
    terminalData->bufTail += strLen;
  }
  else
  { // data will be wrapped around
    uint16_t len = (terminalData->bufTail + strLen) - MAX_TERMINAL_BUF_SIZE;
    memcpy(&terminalBuf[terminalData->bufTail], str, (strLen - len));
    terminalData->bufTail = 0;
    memcpy(&terminalBuf[terminalData->bufTail], str + (strLen - len), len);
    terminalData->bufTail += len;
  }
}

void terminalCache(const char * stream, uint16_t streamLen, SERIAL_PORT_INDEX portIndex, TERMINAL_SRC src)
{
  #ifdef TERMINAL_KEYBOARD_VIEW_SUPPRESS_ACK
    if (curView == KEYBOARD_VIEW)
      return;
  #endif
  
  int walker = 0;
  int i = 2;
  char * index = (char *)stream; // Workaround for const string manipulation
  while (i < streamLen)
  {
    if ((index[i + walker - 2] == ' ') && (index[i + walker - 1] == ' ') && (index[i + walker] == ' '))
    {
      walker++;
      streamLen--;
    }
    else 
      i++;
    if (walker)
      index[i] = index[i + walker];
  }
  
  if (terminalData->pageCount)
  {
    uint16_t headOff = terminalData->pageTable[terminalData->pageHead];  
    uint16_t used;
 
    if (terminalData->bufTail >= headOff)
      used = terminalData->bufTail - headOff;
    else
      used = MAX_TERMINAL_BUF_SIZE - headOff + terminalData->bufTail;  
  
   // Delete oldest page if needed, creates at least enough space for 1 new line
   if ((streamLen + 16 >= MAX_TERMINAL_BUF_SIZE - used)) // include some margin for Port ID and ">>" and string source data
   {
      terminalData->pageHead = (terminalData->pageHead + 1) % MAX_PAGE_COUNT;

      terminalData->pageCount--;
      if (terminalData->pageIndex > terminalData->pageCount)
      {
         terminalData->pageIndex--;
         oldPageIndex = terminalData->pageIndex; // don't redraw because of this change
         pageNumberColor = RED;
      }
    }
  }

  uint16_t lineStart = terminalData->bufTail; // remember index in case a new page is needed

  // save source identifier
  if (terminalData->lastSrc != src)
  {
    terminalData->lastSrc = src;
    saveGcodeTerminalCache(&terminalData->lastSrc, 1);
  }

  uint8_t extraLength = 0;

  // Calculate needed lines based on length, subtract '\n' from streamLen
  lineCounter += 1 + ((streamLen - 2 + extraLength) / CHARS_X);

  // Do we need to create a new page for this line?
  if (lineCounter > LINES_Y)
  {
    lineCounter = 1 + ((streamLen - 2 + extraLength) / CHARS_X);             // this will be the first line of the next page
    terminalData->pageTail = (terminalData->pageTail + 1) % MAX_PAGE_COUNT;  // create the a new page
    terminalData->pageTable[terminalData->pageTail] = lineStart; // point page to before the last incomplete line
    terminalData->pageCount++;

    // delete oldest page if needed
    if ((terminalData->pageTail == terminalData->pageHead) || // pageTable is full
        (terminalData->pageCount + 1) == MAX_PAGE_COUNT)      // maximum page count reached
    {
      terminalData->pageHead = (terminalData->pageHead + 1) % MAX_PAGE_COUNT; // delete oldest page
      terminalData->pageCount--;
    }

    if (terminalData->pageIndex)
    {
      terminalData->pageIndex++;
      oldPageIndex = terminalData->pageIndex; // don't redraw because of this change
      pageNumberColor = RED;
    }
  }

  saveGcodeTerminalCache(stream, streamLen);
  
}

static inline void terminalDrawButton(uint8_t index, uint8_t isPressed)
{
  if (index >= TERM_KEY_COUNT)
    return;

  const char * terminalKey[] = {"<", ">", textSelect(itemToggle[infoSettings.terminal_ack].index), "Back"};

  #ifdef KEYBOARD_MATERIAL_THEME
    uint16_t fontcolor = KEY_FONT_COLOR;
    uint16_t bgcolor = KEY_BG_COLOR;
    GUI_RECT rectBtn = {terminalKeyRect[index].x0 + 3, terminalKeyRect[index].y0 + 3,
                        terminalKeyRect[index].x1 - 3, terminalKeyRect[index].y1 - 3};

    if (index == TERM_BACK)
    {
      fontcolor = CTRL_FONT_COLOR;
      bgcolor = CTRL_BACK_BG_COLOR;
    }

    BUTTON btn = {.fontColor  = fontcolor,
                  .backColor  = bgcolor,
                  .context    = (uint8_t *) terminalKey[index],
                  .lineColor  = bgcolor,
                  .lineWidth  = 0,
                  .pBackColor = fontcolor,
                  .pFontColor = bgcolor,
                  .pLineColor = fontcolor,
                  .radius     = BTN_ROUND_CORNER,
                  .rect       = rectBtn};

    setFontSize(FONT_SIZE_LARGE);
    GUI_DrawButton(&btn, isPressed);
    setFontSize(FONT_SIZE_NORMAL);
  #else
    uint16_t color;
    uint16_t bgColor;

    if (isPressed)
    {
      color = BAR_BG_COLOR;
      bgColor = BAR_FONT_COLOR;
    }
    else
    {
      color = BAR_FONT_COLOR;
      bgColor = BAR_BG_COLOR;
    }

    drawStandardValue(&terminalKeyRect[index], VALUE_STRING, terminalKey[index], FONT_SIZE_LARGE, color, bgColor, 1, true);
  #endif  // KEYBOARD_MATERIAL_THEME
}

static inline void terminalDrawPageNumber(uint16_t fontColor)
{
  char tempstr[10];

  sprintf(tempstr, "%d/%d", (terminalData->pageCount + 1) - terminalData->pageIndex, terminalData->pageCount + 1);

  drawStandardValue(&terminalPageRect, VALUE_STRING, &tempstr, FONT_SIZE_LARGE, fontColor, BAR_BG_COLOR, 1, true);
}

static inline void terminalDrawMenu(void)
{
  setMenu(MENU_TYPE_FULLSCREEN, NULL, COUNT(terminalKeyRect), terminalKeyRect, terminalDrawButton, NULL);

  // clear terminal area
  GUI_SetBkColor(fontSrcColor[infoSettings.terminal_color_scheme][2]);
  GUI_ClearPrect(&terminalAreaRect[0]);

  // clear bar area
  GUI_SetBkColor(BAR_BG_COLOR);
  GUI_ClearPrect(&terminalAreaRect[1]);

  // draw bar area shadow border
  GUI_SetColor(BAR_BORDER_COLOR);
  GUI_HLine(terminalAreaRect[1].x0, terminalAreaRect[1].y0, terminalAreaRect[1].x1);  // first row of bar area used for shadow border

  // draw keyboard
  for (uint8_t i = 0; i < COUNT(terminalKeyRect); i++)
  {
    terminalDrawButton(i, false);
    RAPID_SERIAL_LOOP();
  }

  terminalDrawPageNumber(BAR_FONT_COLOR);
}

static void menuTerminalView(void)
{

  KEY_VALUES key_num = KEY_IDLE;
  CHAR_INFO info;
  TERMINAL_SRC src = 0; // out of bounce, will be save immediately
  uint8_t oldPageCount = 0;
  uint16_t bufIndex = 0;
  uint16_t lastNewline = bufIndex;
  int16_t cursorX = CURSOR_START_X;
  int16_t cursorY = terminalAreaRect[0].y0;
  pageNumberColor = BAR_FONT_COLOR;

  terminalDrawMenu();

  while ((curView == TERMINAL_VIEW) && (MENU_IS(menuTerminal)))
  {

    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case TERM_PAGE_UP:  // page up
        if (terminalData->pageIndex < terminalData->pageCount)
          terminalData->pageIndex++;
        else
          terminalData->pageIndex = 0;
        pageNumberColor = BAR_FONT_COLOR;       
        break;

      case TERM_PAGE_DOWN:  // page down
        if (terminalData->pageIndex > 0)
          terminalData->pageIndex--;
        else
          terminalData->pageIndex = terminalData->pageCount;
        pageNumberColor = BAR_FONT_COLOR;
        break;

      case TERM_TOGGLE_ACK:  // toggle ack in terminal
        TOGGLE_BIT(infoSettings.terminal_ack, 0);

        terminalDrawButton(TERM_TOGGLE_ACK, false);
        break;

      case TERM_BACK:  // back
        curView = KEYBOARD_VIEW;
        break;

      default:
        break;
    }

    if (ELAPSED(lastDataTime, 25)) // wait after last data arrival to prevent too much scrolling
    {
      lastDataTime = OS_GetTimeMs();

      // show new page if index has changed
      bool drawPage = (oldPageIndex != terminalData->pageIndex);

      if (terminalData->pageIndex == 0) // live view
      {
        uint16_t pt = terminalData->pageTable[terminalData->pageTail];
        uint16_t bt = terminalData->bufTail;

        bool inside = (pt <= bt) ? (bufIndex >= pt && bufIndex <= bt) // jump to the latest page, if not in the page already
                                 : (bufIndex >= pt || bufIndex <= bt);
        if (!inside) 
          drawPage = true;
      }

      if (drawPage)
      {
        uint8_t pageTableIndex = (terminalData->pageTail + MAX_PAGE_COUNT
                               - terminalData->pageIndex) % MAX_PAGE_COUNT;

        bufIndex = terminalData->pageTable[pageTableIndex];
        //src = SRC_TERMINAL_GCODE;

        cursorX  = CURSOR_START_X; // start a new page
        cursorY  = terminalAreaRect[0].y0;
        GUI_SetBkColor(fontSrcColor[infoSettings.terminal_color_scheme][2]);
        GUI_ClearPrect(&terminalAreaRect[0]);
      }


      while (bufIndex != terminalData->bufTail) // draw terminal data if something changed, can take about 100ms
      {

        getCharacterInfo((uint8_t *)(terminalBuf + bufIndex), &info);

        if (info.bytes == 0)  // if '\0' is found, move to next byte in the buffer (avoiding an infinite loop due to info.bytes set to 0)
        {
          bufIndex = (bufIndex + 1) % MAX_TERMINAL_BUF_SIZE;

          break;
        }

        // detect source identifier
        if ((info.codePoint == SRC_TERMINAL_GCODE) || (info.codePoint == SRC_TERMINAL_ACK ))
          src = info.codePoint;

        // check next line
        if ((cursorX + info.pixelWidth > terminalAreaRect[0].x1) ||
           ((terminalBuf[bufIndex] == '\n') && (cursorX != CURSOR_START_X)))
        {
          cursorX = CURSOR_START_X;  // jump to next line
          cursorY += info.pixelHeight;
        }

        if (terminalBuf[bufIndex] != '\n')
        {
          // check next page
          if (cursorY + info.pixelHeight > terminalAreaRect[0].y1)
          {
            if (terminalData->pageIndex != 0) // abort after 1 page or keep scrolling when new data arrives
              break; // break while loop
         
            bufIndex = lastNewline; // start next page with line that was not completed on the previous page

            cursorX = CURSOR_START_X; // move cursor to top left of screen
            cursorY = terminalAreaRect[0].y0;

            GUI_SetBkColor(fontSrcColor[infoSettings.terminal_color_scheme][2]);
            GUI_ClearPrect(&terminalAreaRect[0]);

            continue; // continue drawing from updated bufIndex
          }
        
          GUI_SetColor(fontSrcColor[infoSettings.terminal_color_scheme][(src == SRC_TERMINAL_GCODE ? 0 : 1)]);
          GUI_SetBkColor(fontSrcColor[infoSettings.terminal_color_scheme][2]);

          GUI_DispOne(cursorX, cursorY, &info); // Draw a single character
          cursorX += info.pixelWidth;
        }
        else
          lastNewline = (bufIndex + 1) % MAX_TERMINAL_BUF_SIZE;

        bufIndex += info.bytes;
        if (bufIndex >= MAX_TERMINAL_BUF_SIZE) // wrap around
          bufIndex = 0;

      } // draw terminal data while loop

      // update page index and count if needed
      if ((oldPageCount != terminalData->pageCount) || (oldPageIndex != terminalData->pageIndex))
      {
         oldPageCount = terminalData->pageCount;
         oldPageIndex = terminalData->pageIndex;
       
         if (!terminalData->pageIndex)
           pageNumberColor = BAR_FONT_COLOR;
      
         terminalDrawPageNumber(pageNumberColor);
      }

    }
    loopBackEnd();
  }

  terminalData->pageCount = 0;
  terminalData->pageHead = 0;
  terminalData->pageTail = 0;
  terminalData->pageIndex = 0;
  terminalData->bufTail = 0;
  terminalData->lastSrc = 0; // set to out of bounce value so new source identifier will be stored
  pageNumberColor = BAR_FONT_COLOR;
  lineCounter = 0;

  // restore default
  GUI_RestoreColorDefault();
}

void menuTerminal(void)
{
  TERMINAL_DATA termData = {{0}, 0, 0, 0, 0, 0, 0};  // pageTable, pageCount, pageHead, pageTail, pageIndex, bufTail, lastSrc
  terminalData = &termData;
  curView = KEYBOARD_VIEW;
  terminalBuf = (char *)malloc(MAX_TERMINAL_BUF_SIZE);

  if (terminalBuf)
  {
    while (MENU_IS(menuTerminal))
    {
      (curView == KEYBOARD_VIEW) ? menuKeyboardView() : menuTerminalView();
    }

    free(terminalBuf);
    terminalBuf = NULL;
  }
  else
    curView = KEYBOARD_VIEW; // could not allocate terminal buffer, return to keyboard view

}
