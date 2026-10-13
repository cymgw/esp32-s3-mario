#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

// 原创像素风横版跳跃小游戏，玩法类似经典横版跳跃，但不使用任何受版权保护素材。
// 屏幕主接口：VCC、GND、CS、RESET、DC、SDI<MOSI>、SCK、LED、SDO<MISO>。
// 触摸接口：T_IRQ、T_DO、T_DIN、T_CS、T_CLK。
// 显示和触摸共用 SPI 的 SCK/MOSI/MISO，但 CS 必须分开。
constexpr int TFT_SCK = 12;       // 屏幕 SCK，同时接触摸 T_CLK
constexpr int TFT_MOSI = 11;      // 屏幕 SDI<MOSI>，同时接触摸 T_DIN
constexpr int TFT_MISO = 13;      // 屏幕 SDO<MISO>，同时接触摸 T_DO
constexpr int TFT_CS = 10;        // 屏幕 CS
constexpr int TFT_DC = 9;         // 屏幕 DC
constexpr int TFT_RST = 14;       // 屏幕 RESET
constexpr int TFT_BL = 21;        // 屏幕 LED/背光；如果 LED 直接接 3V3，改为 -1
constexpr int TOUCH_CS = 8;       // 触摸 T_CS
constexpr int TOUCH_IRQ = 7;      // 触摸 T_IRQ；不接也可以，但建议接上

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;
constexpr int HUD_H = 18;
constexpr int CONTROL_H = 46;
constexpr int GAME_H = SCREEN_H - CONTROL_H;
constexpr int WORLD_W = 1500;

// 底部三按钮热区：后 / 前 / 跳。
// XPT2046 电阻触摸通常只能读一个触点，不是真正多点触控。
// 这里用“方向保持 + 两指平均点容错”实现组合：后/前 + 跳，即向后/向前跳。
constexpr int BUTTON_Y = SCREEN_H - CONTROL_H;
constexpr int BACK_X1 = 8;
constexpr int BACK_X2 = 100;
constexpr int FORWARD_X1 = 114;
constexpr int FORWARD_X2 = 206;
constexpr int JUMP_X1 = 220;
constexpr int JUMP_X2 = 312;
constexpr uint32_t DIRECTION_HOLD_MS = 1000;
constexpr uint32_t MULTI_TOUCH_COMBO_MS = 350;
constexpr uint32_t TOUCH_HOLD_MS = 110;
// 240x320 全屏 SPI 刷新比较耗时；约 30FPS 兼顾响应和稳定性。
constexpr uint32_t FRAME_INTERVAL_MS = 33;

// XPT2046 原始值需要校准。若触摸按钮方向不对，优先调整这 4 个值或 swap/invert 逻辑。
constexpr int TOUCH_MIN_X = 250;
constexpr int TOUCH_MAX_X = 3900;
constexpr int TOUCH_MIN_Y = 250;
constexpr int TOUCH_MAX_Y = 3900;
// 横向偏移校准。正常先保持 0；若触摸整体偏左再增大，偏右则减小。
constexpr int TOUCH_X_OFFSET = 0;
constexpr int TOUCH_Y_OFFSET = 0;

constexpr uint16_t C_BLACK = 0x0000;
constexpr uint16_t C_WHITE = 0xFFFF;
constexpr uint16_t C_SKY = 0x6DDF;
constexpr uint16_t C_GRASS = 0x2E44;
constexpr uint16_t C_DIRT = 0x9B24;
constexpr uint16_t C_PLAYER = 0xFBE0;
constexpr uint16_t C_PLAYER_DARK = 0xC980;
constexpr uint16_t C_COIN = 0xFFE0;
constexpr uint16_t C_BLOCK = 0xCBE0;
constexpr uint16_t C_SPIKE = 0xF800;
constexpr uint16_t C_FLAG = 0x07E0;
constexpr uint16_t C_PANEL = 0x39E7;
constexpr uint16_t C_PANEL_DARK = 0x2104;

Arduino_DataBus *bus = new Arduino_ESP32SPI(
    TFT_DC,
    TFT_CS,
    TFT_SCK,
    TFT_MOSI,
    TFT_MISO
);

// 常见 2.4 寸 240x320 SPI TFT 多为 ILI9341。若花屏/白屏，尝试改为 ST7789 构造。
Arduino_GFX *display = new Arduino_ILI9341(bus, TFT_RST, 1, false);
// 16 位 Canvas 比 3bit Canvas 少一次调色转换，按键反馈和动画会更快；ESP32-S3 内存足够 320x240 缓冲。
Arduino_GFX *gfx = new Arduino_Canvas(SCREEN_W, SCREEN_H, display);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

struct Rect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

struct Coin {
  int16_t x;
  int16_t y;
  bool taken;
};

struct Controls {
  bool left;
  bool right;
  bool jump;
  int16_t x;
  int16_t y;
  bool touched;
};

const Rect platforms[] = {
    {0, 178, 260, 20},
    {300, 178, 220, 20},
    {560, 178, 260, 20},
    {860, 178, 250, 20},
    {1160, 178, 340, 20},
    {190, 138, 90, 12},
    {410, 118, 90, 12},
    {650, 128, 100, 12},
    {930, 112, 90, 12},
    {1090, 145, 120, 12},
};

const Rect spikes[] = {
    {330, 166, 18, 12},
    {790, 166, 18, 12},
    {1000, 166, 18, 12},
};

Coin coins[] = {
    {220, 116, false},
    {455, 96, false},
    {690, 106, false},
    {970, 90, false},
    {1135, 124, false},
    {1290, 150, false},
};

float playerX = 30;
float playerY = 130;
float velocityY = 0;
bool onGround = false;
bool won = false;
bool gameOver = false;
int score = 0;
int lives = 3;
int cameraX = 0;
uint32_t lastFrameMs = 0;
uint32_t lastTouchLogMs = 0;
int lastDirection = 0;
uint32_t lastDirectionMs = 0;
Controls heldControls = {false, false, false, -1, -1, false};
uint32_t heldUntilMs = 0;

int clampInt(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

bool intersects(const Rect &a, const Rect &b) {
  return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

Rect playerRectAt(float x, float y) {
  return {static_cast<int16_t>(x), static_cast<int16_t>(y), 16, 20};
}

void resetPlayer() {
  playerX = 30;
  playerY = 130;
  velocityY = 0;
  cameraX = 0;
  onGround = false;
}

void restartGame() {
  score = 0;
  lives = 3;
  won = false;
  gameOver = false;
  for (Coin &coin : coins) {
    coin.taken = false;
  }
  resetPlayer();
}

Controls readControls() {
  Controls c = {false, false, false, -1, -1, false};
  uint32_t now = millis();
  if (!touch.touched()) {
    if (now < heldUntilMs) {
      return heldControls;
    }
    return c;
  }

  TS_Point p = touch.getPoint();
  // XPT2046 setRotation(1) 后已经把触摸坐标转成横屏轴向。
  // 之前再交换 X/Y 会导致按钮响应跑到左侧竖排区域；这里不再交换，只做横屏直接映射。
  int sx = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_W - 1);
  int sy = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_H - 1);
  sx = clampInt(sx + TOUCH_X_OFFSET, 0, SCREEN_W - 1);
  sy = clampInt(sy + TOUCH_Y_OFFSET, 0, SCREEN_H - 1);

  c.x = sx;
  c.y = sy;
  c.touched = true;

  // 底部按钮区域。为了容忍触摸膜边缘校准偏差，纵向范围略向上放宽。
  if (sy >= BUTTON_Y - 16) {
    bool inBack = sx >= BACK_X1 && sx <= BACK_X2;
    bool inForward = sx >= FORWARD_X1 && sx <= FORWARD_X2;
    bool inJump = sx >= JUMP_X1 && sx <= JUMP_X2;
    bool inForwardJumpGap = sx > FORWARD_X2 && sx < JUMP_X1;

    // 两根手指同时按在电阻屏上时，XPT2046 常会读到两个点之间的“平均位置”。
    // 因此前/跳同时按，可能落在 FWD 与 JUMP 的间隙；后/跳同时按，可能落在 FWD 附近。
    bool recentLeft = lastDirection < 0 && now - lastDirectionMs <= MULTI_TOUCH_COMBO_MS;
    bool recentRight = lastDirection > 0 && now - lastDirectionMs <= MULTI_TOUCH_COMBO_MS;

    if (inJump || (recentRight && inForwardJumpGap) || (recentLeft && inForward)) {
      c.jump = true;
      if (now - lastDirectionMs <= DIRECTION_HOLD_MS) {
        if (lastDirection < 0) c.left = true;
        if (lastDirection > 0) c.right = true;
      }
    } else if (inBack) {
      c.left = true;
      lastDirection = -1;
      lastDirectionMs = now;
    } else if (inForward) {
      c.right = true;
      lastDirection = 1;
      lastDirectionMs = now;
    }
  } else if (gameOver || won) {
    restartGame();
  }

  heldControls = c;
  heldUntilMs = now + TOUCH_HOLD_MS;
  return c;
}

void updateGame(const Controls &controls, float dt) {
  if (won || gameOver) {
    return;
  }

  constexpr float moveSpeed = 5.5f;
  constexpr float gravity = 1.15f;
  constexpr float jumpSpeed = -11.5f;

  float nextX = playerX;
  if (controls.left) nextX -= moveSpeed * dt;
  if (controls.right) nextX += moveSpeed * dt;
  nextX = clampInt(static_cast<int>(nextX), 0, WORLD_W - 20);

  Rect horizontal = playerRectAt(nextX, playerY);
  for (const Rect &platform : platforms) {
    if (intersects(horizontal, platform)) {
      if (nextX > playerX) nextX = platform.x - 16;
      if (nextX < playerX) nextX = platform.x + platform.w;
      break;
    }
  }
  playerX = nextX;

  if (controls.jump && onGround) {
    velocityY = jumpSpeed;
    onGround = false;
  }

  velocityY += gravity * dt;
  if (velocityY > 13.0f) velocityY = 13.0f;

  float nextY = playerY + velocityY * dt;
  onGround = false;
  Rect vertical = playerRectAt(playerX, nextY);
  for (const Rect &platform : platforms) {
    if (intersects(vertical, platform)) {
      if (velocityY > 0) {
        nextY = platform.y - 20;
        velocityY = 0;
        onGround = true;
      } else if (velocityY < 0) {
        nextY = platform.y + platform.h;
        velocityY = 0;
      }
      vertical = playerRectAt(playerX, nextY);
    }
  }
  playerY = nextY;

  Rect player = playerRectAt(playerX, playerY);
  for (const Rect &spike : spikes) {
    if (intersects(player, spike)) {
      lives--;
      if (lives <= 0) gameOver = true;
      resetPlayer();
      return;
    }
  }

  for (Coin &coin : coins) {
    if (!coin.taken) {
      Rect coinBox = {coin.x - 5, coin.y - 5, 10, 10};
      if (intersects(player, coinBox)) {
        coin.taken = true;
        score++;
      }
    }
  }

  if (playerY > GAME_H + 30) {
    lives--;
    if (lives <= 0) gameOver = true;
    resetPlayer();
    return;
  }

  if (playerX > WORLD_W - 70) {
    won = true;
  }

  cameraX = static_cast<int>(playerX) - 95;
  cameraX = clampInt(cameraX, 0, WORLD_W - SCREEN_W);
}

void drawCloud(int x, int y) {
  gfx->fillCircle(x, y + 7, 9, C_WHITE);
  gfx->fillCircle(x + 10, y, 12, C_WHITE);
  gfx->fillCircle(x + 24, y + 8, 10, C_WHITE);
  gfx->fillRect(x - 2, y + 8, 32, 10, C_WHITE);
}

void drawControls(const Controls &controls) {
  gfx->fillRect(0, BUTTON_Y, SCREEN_W, CONTROL_H, C_PANEL_DARK);
  gfx->drawFastHLine(0, BUTTON_Y, SCREEN_W, C_WHITE);

  uint16_t backColor = controls.left ? C_COIN : C_PANEL;
  uint16_t forwardColor = controls.right ? C_COIN : C_PANEL;
  uint16_t jumpColor = controls.jump ? C_COIN : C_PANEL;

  gfx->fillRoundRect(BACK_X1, BUTTON_Y + 7, BACK_X2 - BACK_X1, 32, 7, backColor);
  gfx->fillRoundRect(FORWARD_X1, BUTTON_Y + 7, FORWARD_X2 - FORWARD_X1, 32, 7, forwardColor);
  gfx->fillRoundRect(JUMP_X1, BUTTON_Y + 7, JUMP_X2 - JUMP_X1, 32, 7, jumpColor);

  gfx->drawRoundRect(BACK_X1, BUTTON_Y + 7, BACK_X2 - BACK_X1, 32, 7, C_WHITE);
  gfx->drawRoundRect(FORWARD_X1, BUTTON_Y + 7, FORWARD_X2 - FORWARD_X1, 32, 7, C_WHITE);
  gfx->drawRoundRect(JUMP_X1, BUTTON_Y + 7, JUMP_X2 - JUMP_X1, 32, 7, C_WHITE);

  // 默认字体不包含中文字库，使用清晰的英文按钮标记。
  gfx->setTextSize(1);
  gfx->setTextColor(C_BLACK);
  gfx->setCursor(BACK_X1 + 22, BUTTON_Y + 19);
  gfx->print("BACK");
  gfx->setCursor(FORWARD_X1 + 18, BUTTON_Y + 19);
  gfx->print("FWD");
  gfx->setCursor(JUMP_X1 + 20, BUTTON_Y + 19);
  gfx->print("JUMP");
}

void drawGame(const Controls &controls) {
  gfx->fillScreen(C_SKY);

  drawCloud(42 - cameraX / 4, 36);
  drawCloud(180 - cameraX / 5, 28);
  drawCloud(310 - cameraX / 6, 54);

  for (const Rect &platform : platforms) {
    int sx = platform.x - cameraX;
    if (sx + platform.w < 0 || sx >= SCREEN_W) continue;
    gfx->fillRect(sx, platform.y, platform.w, platform.h, C_DIRT);
    gfx->fillRect(sx, platform.y, platform.w, 4, C_GRASS);
  }

  for (const Rect &spike : spikes) {
    int sx = spike.x - cameraX;
    if (sx + spike.w < 0 || sx >= SCREEN_W) continue;
    gfx->fillTriangle(sx, spike.y + spike.h, sx + spike.w / 2, spike.y, sx + spike.w, spike.y + spike.h, C_SPIKE);
  }

  for (const Coin &coin : coins) {
    if (coin.taken) continue;
    int sx = coin.x - cameraX;
    if (sx < -10 || sx > SCREEN_W + 10) continue;
    gfx->fillCircle(sx, coin.y, 5, C_COIN);
    gfx->drawCircle(sx, coin.y, 5, C_WHITE);
  }

  int flagX = WORLD_W - 42 - cameraX;
  if (flagX > -20 && flagX < SCREEN_W + 20) {
    gfx->drawFastVLine(flagX, 82, 96, C_WHITE);
    gfx->fillTriangle(flagX + 1, 82, flagX + 1, 112, flagX + 34, 98, C_FLAG);
  }

  int px = static_cast<int>(playerX) - cameraX;
  int py = static_cast<int>(playerY);
  gfx->fillRect(px, py, 16, 20, C_PLAYER);
  gfx->fillRect(px + 3, py + 3, 10, 5, C_PLAYER_DARK);
  gfx->fillRect(px + 3, py + 15, 4, 5, C_BLACK);
  gfx->fillRect(px + 10, py + 15, 4, 5, C_BLACK);

  gfx->fillRect(0, 0, SCREEN_W, HUD_H, C_BLACK);
  gfx->setTextSize(1);
  gfx->setTextColor(C_WHITE);
  gfx->setCursor(6, 5);
  gfx->print("COINS ");
  gfx->print(score);
  gfx->setCursor(88, 5);
  gfx->print("LIFE ");
  gfx->print(lives);
  gfx->setCursor(164, 5);
  gfx->print("X ");
  gfx->print(static_cast<int>(playerX));

  if (won || gameOver) {
    gfx->fillRoundRect(42, 66, 236, 78, 8, C_BLACK);
    gfx->drawRoundRect(42, 66, 236, 78, 8, C_WHITE);
    gfx->setTextSize(2);
    gfx->setTextColor(won ? C_FLAG : C_SPIKE);
    gfx->setCursor(won ? 92 : 88, 82);
    gfx->print(won ? "YOU WIN!" : "GAME OVER");
    gfx->setTextSize(1);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(78, 116);
    gfx->print("Touch screen to restart");
  }

  drawControls(controls);
  gfx->flush();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  if (TFT_BL >= 0) {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
  }

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI);

  if (!display->begin(40000000)) {
    Serial.println("TFT init failed");
    while (true) delay(1000);
  }

  if (!gfx->begin()) {
    Serial.println("Canvas init failed");
    while (true) delay(1000);
  }

  display->fillScreen(BLACK);
  touch.begin();
  touch.setRotation(1);
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI);

  Serial.println("Pixel jumper ready");
  Serial.println("Controls: bottom buttons: <BK=back, FW>=forward, JMP=jump; direction is buffered for jump combos");
  Serial.println("If touch is inaccurate, adjust TOUCH_MIN/MAX constants.");

  restartGame();
  drawGame({false, false, false, -1, -1, false});
  lastFrameMs = millis();
}

void loop() {
  uint32_t now = millis();
  Controls controls = readControls();  // 先读触摸，确保按下尽快被采样。
  uint32_t elapsedMs = now - lastFrameMs;
  if (elapsedMs < FRAME_INTERVAL_MS) {
    delay(1);
    return;
  }
  lastFrameMs = now;

  // 屏幕 SPI 全屏刷新比较慢，不能让物理动作跟着帧率变慢。
  // 按真实耗时换算成约 30FPS 的物理步长，并做上限保护避免卡顿后穿模。
  float dt = elapsedMs / 50.0f;
  if (dt < 1.0f) dt = 1.0f;
  if (dt > 4.0f) dt = 4.0f;

  updateGame(controls, dt);
  drawGame(controls);
}
