/*
 Copyright (C) 2025 retroelec <retroelec42@gmail.com>

 This program is free software; you can redistribute it and/or modify it
 under the terms of the GNU General Public License as published by the
 Free Software Foundation; either version 3 of the License, or (at your
 option) any later version.

 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 for more details.

 For the complete text of the GNU General Public License see
 http://www.gnu.org/licenses/.
*/
#include <LV_Helper.h>
#include <LilyGoLib.h>
#include <WiFi.h>
#include <sntp.h>
#include <time.h>

#ifndef WIFI_SSID
#define WIFI_SSID "default_ssid"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS "default_password"
#endif

const char *ssid = WIFI_SSID;
const char *password = WIFI_PASS;

// ntp settings
const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "ntp11.metas.ch";
const char *time_zone = "CET-1CEST,M3.5.0,M10.5.0/3";

const uint8_t MAX_NUM_BARRELS = 8;

// interval calling the animation function
const uint8_t ANIMATION_INTERVAL = 25;

#include <AudioFileSourceFunction.h>
#include <AudioFileSourceID3.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
// Audio file header
#include "ckongstart_mp3.h"
#include "mariodies_mp3.h"
#include "mariojump_mp3.h"

TaskHandle_t playMP3Handler;

// It is recommended to use I2S channel 1.
// If the PDM microphone is turned on,
// the decoder must use channel 1 because PDM can only use channel 1
const uint8_t i2sPort = 1;

AudioFileSourcePROGMEM *file = NULL;
AudioOutputI2S *out = NULL;
AudioGeneratorMP3 *mp3 = NULL;
AudioFileSourceID3 *id3 = NULL;

void playMP3Task(void *prarms) {
  vTaskSuspend(NULL);
  while (1) {
    if (mp3->isRunning()) {
      if (!mp3->loop()) {
        mp3->stop();
      }
    } else {
      vTaskSuspend(NULL);
    }
    delay(2);
  }
  vTaskDelete(NULL);
}

// global lvgl objects
lv_obj_t *view;
lv_obj_t *barrel_sprite[MAX_NUM_BARRELS];
lv_obj_t *kong_sprite;
lv_obj_t *mario_sprite;
lv_obj_t *fire_sprite;
lv_obj_t *hour_label;
lv_obj_t *minute_label;
lv_obj_t *second_label;
lv_obj_t *colon_label1;
lv_obj_t *colon_label2;
lv_obj_t *date_label;
lv_obj_t *battery_label;
lv_obj_t *ntp_label;
lv_obj_t *sound_label;
lv_obj_t *brightness_label;

// images
LV_IMG_DECLARE(bg_png);
LV_IMG_DECLARE(barrel1_png);
LV_IMG_DECLARE(barrel2_png);
LV_IMG_DECLARE(fire1_png);
LV_IMG_DECLARE(fire2_png);
LV_IMG_DECLARE(kong_png);
LV_IMG_DECLARE(kong2_png);
LV_IMG_DECLARE(marior1_png);
LV_IMG_DECLARE(mariojumpr_png);
LV_IMG_DECLARE(mariodied_png);

// state of clock
enum State { STANDARD, DATE, ANIM };
enum State state = STANDARD;

// variable used to detect double clicks for getting the time by ntp
uint32_t ntp_last_click_time = 0;

// variables used to enter sleep mode / wakeup
unsigned long last_activity_time = 0;
const unsigned long sleep_timeout = 60000; // 60 seconds

// variables for the animation function
lv_timer_t *animation_timer = NULL;
bool animation_running = false;

// variables for the marion jump function
lv_timer_t *mario_jump_timer = NULL;
bool mario_jump_running = false;

// flag if mario jumps up
bool mario_jumpsup = true;

// interval to "create" a new barrel
int next_anim_interval;

// flag if mario should autmatically jump in ANIM mode
bool autojump = true;

// collision with barrel?
bool mario_died = false;

// sound on/off
bool soundon = true;

// brightness high/low
bool brightnesshi = false;

// platform definitions
const int platform_y[] = {90, 140, 166, 219};
const int platform_x_start[] = {78, 13, 13, 13};
const int platform_x_end[] = {221, 221, 221, 221};
const float platform_slope[] = {0.028169, -0.068421, 0.068421, -0.042105};

// variables for animation of the fire sprite
int fire_dir = 1;
int fire_animcnt = 0;
bool fire_imgtoggle = false;

// variables for animation of the kong sprite
int kong_animcnt = 0;
bool kong_imgview;

// barrels array
struct Barrel {
  int nr;
  int x;
  int platform;
  int animcnt;
  bool imgtoggle;
  int dir;
  bool inbetween;
  int fallingcnt;
  bool bumping;
  int bumbingcnt;
  lv_obj_t *sprite;
};
struct Barrel barrel[MAX_NUM_BARRELS];
bool barrel_occupied[MAX_NUM_BARRELS] = {false};

void ntp_callback_function(struct timeval *t) {
  Serial.println("got time adjustment from NTP");
  watch.hwClockWrite();
  lv_label_set_text(ntp_label, LV_SYMBOL_OK);
  delay(1000);
  lv_obj_add_flag(ntp_label, LV_OBJ_FLAG_HIDDEN);
}

void update_battery_status() {
  int battery_percent = watch.getBatteryPercent();
  bool is_charging = watch.isCharging();
  const char *charging_symbol = is_charging ? LV_SYMBOL_CHARGE : "";
  char battery_text[6];
  snprintf(battery_text, sizeof(battery_text), "%s%d%%", charging_symbol,
           battery_percent);
  lv_label_set_text(battery_label, battery_text);
}

lv_obj_t *create_sprite(uint8_t x, uint8_t y, const void *imgsrc) {
  lv_obj_t *spriteimg = lv_img_create(view);
  lv_img_set_src(spriteimg, imgsrc);
  lv_obj_set_style_img_recolor(spriteimg, lv_color_black(), 0);
  lv_obj_set_style_img_recolor_opa(spriteimg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_img_opa(spriteimg, LV_OPA_COVER, 0);
  lv_obj_set_pos(spriteimg, x, y);
  return spriteimg;
}

void animate_barrel(struct Barrel *barrel) {
  if (barrel->inbetween) {
    barrel->fallingcnt--;
    int y = lv_obj_get_y(barrel->sprite);
    lv_obj_set_y(barrel->sprite, y + 2);
    if (barrel->fallingcnt % 2 == 0) {
      int x = barrel->x;
      lv_obj_set_x(barrel->sprite, x + barrel->dir);
      barrel->x = x + barrel->dir;
    }
    if (barrel->fallingcnt == 0) {
      barrel->inbetween = false;
      barrel->dir *= -1;
      barrel->bumping = true;
      barrel->bumbingcnt = 5;
    }
  } else {
    int platform = barrel->platform;
    int x = barrel->x;
    // new sprite position
    int y = platform_slope[platform] * x + platform_y[platform];
    if (barrel->bumping) {
      if (barrel->bumbingcnt >= 3) {
        y -= 6 - barrel->bumbingcnt;
      } else {
        y -= barrel->bumbingcnt;
      }
      barrel->bumbingcnt--;
      if (barrel->bumbingcnt == 0) {
        barrel->bumping = false;
      }
    }
    x += barrel->dir * 2;
    barrel->x = x;
    lv_obj_set_pos(barrel->sprite, x, y);
    // sprite img animation
    barrel->animcnt++;
    barrel->animcnt %= 8;
    if (barrel->animcnt == 0) {
      barrel->imgtoggle = !barrel->imgtoggle;
      lv_img_set_src(barrel->sprite,
                     barrel->imgtoggle ? &barrel1_png : &barrel2_png);
    }
    // end of platform reached?
    if (((barrel->dir == 1) && (x >= platform_x_end[platform])) ||
        ((barrel->dir == -1) && (x <= platform_x_start[platform]))) {
      if (platform < 3) {
        barrel->platform++;
        barrel->inbetween = true;
        barrel->fallingcnt = 14;
      } else {
        barrel_occupied[barrel->nr] = false;
        lv_obj_set_pos(barrel->sprite, 240, 240);
      }
    }
  }
}

void animate_fire() {
  int platform = 2;
  int x = lv_obj_get_x(fire_sprite);
  int y = platform_slope[platform] * x + platform_y[platform] - 9;
  if (x <= 40) {
    fire_dir = 1;
  } else if (x >= 180) {
    fire_dir = -1;
  }
  x += fire_dir * 2;
  lv_obj_set_pos(fire_sprite, x, y);
  fire_animcnt++;
  fire_animcnt %= 8;
  if (fire_animcnt == 0) {
    switch (rand() % 3) {
    case 0:
      fire_dir = -1;
      break;
    case 1:
      fire_dir = 0;
      break;
    case 2:
      fire_dir = 1;
      break;
    }
    fire_imgtoggle = !fire_imgtoggle;
    lv_img_set_src(fire_sprite, fire_imgtoggle ? &fire1_png : &fire2_png);
  }
}

void animate_kong() {
  kong_animcnt++;
  kong_animcnt %= 120;
  if (kong_animcnt == 0) {
    switch (rand() % 3) {
    case 0:
    case 1:
      kong_imgview = true;
      break;
    case 2:
      kong_imgview = false;
      break;
    }
    lv_img_set_src(kong_sprite, kong_imgview ? &kong_png : &kong2_png);
  }
}

void mario_jump_callback(lv_timer_t *timer) {
  int y = lv_obj_get_y(mario_sprite);
  if (mario_jumpsup) {
    if (y > 107) {
      y -= 2;
    } else {
      mario_jumpsup = false;
    }
  } else {
    if (y < 123) {
      y += 2;
    } else {
      y = 123;
      if (mario_jump_running) {
        lv_img_set_src(mario_sprite, &marior1_png);
        lv_timer_del(mario_jump_timer);
        mario_jump_running = false;
      }
    }
  }
  lv_obj_set_y(mario_sprite, y);
}

void start_mario_jump() {
  if ((!mario_jump_running) && (!mario_died)) {
    mario_jumpsup = true;
    mario_jump_timer = lv_timer_create(mario_jump_callback, 50, NULL);
    mario_jump_running = true;
    lv_img_set_src(mario_sprite, &mariojumpr_png);
    vTaskSuspend(playMP3Handler);
    file->open(mariojump_mp3, mariojump_mp3_len);
    mp3->begin(id3, out);
    vTaskResume(playMP3Handler);
  }
}

void animate_mario() {
  for (uint8_t i = 0; i < MAX_NUM_BARRELS; i++) {
    if ((barrel[i].platform == 1) && (barrel[i].x >= 179) &&
        (barrel[i].x <= 180)) {
      start_mario_jump();
      break;
    }
  }
}

void check4collision() {
  for (uint8_t i = 0; i < MAX_NUM_BARRELS; i++) {
    if ((barrel[i].platform == 1) && (barrel[i].x >= 154) &&
        (barrel[i].x <= 155) && (lv_obj_get_y(mario_sprite) >= 115)) {
      mario_died = true;
      barrel[i].platform = 0;
      next_anim_interval = 5000;
      break;
    }
  }
}

void init_barrel(int barrelnr) {
  barrel[barrelnr].nr = barrelnr;
  barrel[barrelnr].x = 78;
  barrel[barrelnr].platform = 0;
  barrel[barrelnr].animcnt = 0;
  barrel[barrelnr].imgtoggle = false;
  barrel[barrelnr].dir = 1;
  barrel[barrelnr].inbetween = false;
  barrel[barrelnr].fallingcnt = 0;
  barrel[barrelnr].bumping = false;
  barrel[barrelnr].bumbingcnt = 0;
  barrel[barrelnr].sprite = barrel_sprite[barrelnr];
}

void occupy_free_barrel() {
  for (uint8_t i = 0; i < MAX_NUM_BARRELS; i++) {
    if (!barrel_occupied[i]) {
      barrel_occupied[i] = true;
      init_barrel(i);
      return;
    }
  }
}

void init_animation() {
  autojump = true;
  // first barrel after 3000 ms
  next_anim_interval = 3000;
  for (uint8_t i = 0; i < MAX_NUM_BARRELS; i++) {
    barrel_occupied[i] = false;
    lv_obj_set_pos(barrel_sprite[i], 240, 240);
  }
}

void animation_callback(lv_timer_t *timer) {
  next_anim_interval -= ANIMATION_INTERVAL;
  if (!mario_died) {
    if (next_anim_interval <= 0) {
      next_anim_interval = 800 + (rand() % 2000); // 800-2800 ms
      occupy_free_barrel();
    }
    for (uint8_t i = 0; i < MAX_NUM_BARRELS; i++) {
      if (barrel_occupied[i]) {
        animate_barrel(&barrel[i]);
      }
    }
    animate_fire();
    animate_kong();
    if (autojump) {
      animate_mario();
    }
    check4collision();
  } else {
    if (next_anim_interval == 5000 - 1000) {
      lv_img_set_src(mario_sprite, &mariodied_png);
      vTaskSuspend(playMP3Handler);
      file->open(mariodies_mp3, mariodies_mp3_len);
      mp3->begin(id3, out);
      vTaskResume(playMP3Handler);
    } else if (next_anim_interval <= 0) {
      mario_died = false;
      lv_img_set_src(mario_sprite, &marior1_png);
      init_animation();
    }
  }
}

void start_animation() {
  if (!animation_running) {
    vTaskSuspend(playMP3Handler);
    file->open(ckongstart_mp3, ckongstart_mp3_len);
    mp3->begin(id3, out);
    vTaskResume(playMP3Handler);
    init_animation();
    animation_timer =
        lv_timer_create(animation_callback, ANIMATION_INTERVAL, NULL);
    animation_running = true;
  }
}

void init_sprites() {
  lv_obj_set_pos(barrel_sprite[0], 166, 94);
  lv_obj_set_pos(barrel_sprite[1], 180, 127);
  lv_obj_set_pos(barrel_sprite[2], 4, 167);
  lv_obj_set_pos(barrel_sprite[3], 240, 240);
  lv_obj_set_pos(barrel_sprite[4], 240, 240);
  lv_obj_set_pos(barrel_sprite[5], 240, 240);
  lv_obj_set_pos(barrel_sprite[6], 240, 240);
  lv_obj_set_pos(barrel_sprite[7], 240, 240);
  lv_obj_set_pos(fire_sprite, 196, 167);
  lv_img_set_src(fire_sprite, &fire1_png);
  lv_img_set_src(kong_sprite, &kong_png);
}

void stop_animation() {
  if (animation_running) {
    lv_timer_del(animation_timer);
    animation_timer = NULL;
    animation_running = false;
  }
  init_sprites();
}

void show_state() {
  switch (state) {
  case STANDARD:
    stop_animation();
    lv_obj_clear_flag(hour_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(minute_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(second_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(colon_label1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(colon_label2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(date_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(battery_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(sound_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(brightness_label, LV_OBJ_FLAG_HIDDEN);
    break;
  case DATE:
    stop_animation();
    lv_obj_clear_flag(hour_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(minute_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(second_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(colon_label1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(colon_label2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(date_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(battery_label, LV_OBJ_FLAG_HIDDEN);
    if (soundon) {
      lv_obj_clear_flag(sound_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (brightnesshi) {
      lv_obj_clear_flag(brightness_label, LV_OBJ_FLAG_HIDDEN);
    }
    break;
  case ANIM:
    start_animation();
    lv_obj_add_flag(date_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hour_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(minute_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(second_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(colon_label1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(colon_label2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(battery_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(sound_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(brightness_label, LV_OBJ_FLAG_HIDDEN);
    break;
  }
}

void touch_event_change_state(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_point_t touch_point;
  lv_indev_get_point(lv_indev_get_act(), &touch_point);
  if (touch_point.y <= 120) {
    if (touch_point.x < 180) {
      // touch in the upper left part
      state = static_cast<State>(static_cast<int>(state) + 1); // state++
      if (state > ANIM) {
        state = STANDARD;
      } else if (state == ANIM) {
        autojump = true;
      }
      show_state();
    }
  } else {
    // touch in the lower part
    if (touch_point.x < 180) {
      if (state == ANIM) {
        autojump = false;
        start_mario_jump();
      }
    } else {
      if (state == STANDARD) {
        brightnesshi = !brightnesshi;
      } else {
        soundon = !soundon;
        if (soundon) {
          out->SetGain(0.2);
        } else {
          out->SetGain(0.0);
        }
      }
      if (state == DATE) {
        if (soundon) {
          lv_obj_clear_flag(sound_label, LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_add_flag(sound_label, LV_OBJ_FLAG_HIDDEN);
        }
      }
    }
  }
}

void setup() {
  Serial.begin(115200);

  watch.begin();

  beginLvglHelper();

  // main object
  view = lv_obj_create(lv_scr_act());
  lv_obj_set_size(view, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(view, LV_OBJ_FLAG_SCROLLABLE);

  // remove white edges
  lv_obj_set_style_pad_all(view, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(view, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(view, 0, LV_PART_MAIN);

  // background image
  lv_obj_set_style_bg_color(view, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(view, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_t *bgimg = lv_img_create(view);
  lv_img_set_src(bgimg, &bg_png);
  lv_obj_center(bgimg);

  // sprite images
  barrel_sprite[0] = create_sprite(166, 94, &barrel1_png);
  barrel_sprite[1] = create_sprite(166, 94, &barrel2_png);
  barrel_sprite[2] = create_sprite(166, 94, &barrel2_png);
  barrel_sprite[3] = create_sprite(166, 94, &barrel1_png);
  barrel_sprite[4] = create_sprite(166, 94, &barrel1_png);
  barrel_sprite[5] = create_sprite(166, 94, &barrel1_png);
  barrel_sprite[6] = create_sprite(166, 94, &barrel1_png);
  barrel_sprite[7] = create_sprite(166, 94, &barrel1_png);
  fire_sprite = create_sprite(196, 167, &fire1_png);
  kong_sprite = create_sprite(24, 65, &kong_png);
  mario_sprite = create_sprite(146, 123, &marior1_png);
  init_sprites();

  // time
  hour_label = lv_label_create(view);
  lv_label_set_text(hour_label, "00");
  lv_obj_set_style_text_letter_space(hour_label, 2, LV_PART_MAIN);
  lv_obj_set_style_text_font(hour_label, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_obj_set_style_text_color(hour_label, lv_color_white(), LV_PART_MAIN);
  lv_obj_align(hour_label, LV_ALIGN_BOTTOM_MID, -50, -55);
  minute_label = lv_label_create(view);
  lv_label_set_text(minute_label, "00");
  lv_obj_set_style_text_letter_space(minute_label, 2, LV_PART_MAIN);
  lv_obj_set_style_text_font(minute_label, &lv_font_montserrat_28,
                             LV_PART_MAIN);
  lv_obj_set_style_text_color(minute_label, lv_color_white(), LV_PART_MAIN);
  lv_obj_align(minute_label, LV_ALIGN_BOTTOM_MID, 0, -55);
  second_label = lv_label_create(view);
  lv_label_set_text(second_label, "00");
  lv_obj_set_style_text_letter_space(second_label, 2, LV_PART_MAIN);
  lv_obj_set_style_text_font(second_label, &lv_font_montserrat_20,
                             LV_PART_MAIN);
  lv_obj_set_style_text_color(second_label, lv_color_white(), LV_PART_MAIN);
  lv_obj_align(second_label, LV_ALIGN_BOTTOM_MID, 50, -55);
  colon_label1 = lv_label_create(view);
  lv_label_set_text(colon_label1, ":");
  lv_obj_set_style_text_font(colon_label1, &lv_font_montserrat_28,
                             LV_PART_MAIN);
  lv_obj_set_style_text_color(colon_label1, lv_color_white(), LV_PART_MAIN);
  lv_obj_align(colon_label1, LV_ALIGN_BOTTOM_MID, -25, -55);
  colon_label2 = lv_label_create(view);
  lv_label_set_text(colon_label2, ":");
  lv_obj_set_style_text_font(colon_label2, &lv_font_montserrat_28,
                             LV_PART_MAIN);
  lv_obj_set_style_text_color(colon_label2, lv_color_white(), LV_PART_MAIN);
  lv_obj_align(colon_label2, LV_ALIGN_BOTTOM_MID, 25, -55);

  // date
  date_label = lv_label_create(view);
  lv_label_set_text(date_label, "YYYY-MM-DD");
  lv_obj_set_style_text_color(date_label, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(date_label, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_align(date_label, LV_ALIGN_BOTTOM_MID, 0, -24);
  lv_obj_add_flag(date_label, LV_OBJ_FLAG_HIDDEN);

  // battery
  battery_label = lv_label_create(view);
  lv_label_set_text(battery_label, "100%");
  lv_obj_set_style_text_color(battery_label, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_20,
                             LV_PART_MAIN);
  lv_obj_align(battery_label, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_add_flag(battery_label, LV_OBJ_FLAG_HIDDEN);

  // ntp
  ntp_label = lv_label_create(view);
  lv_label_set_text(ntp_label, "");
  lv_obj_set_style_text_color(ntp_label, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(ntp_label, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_obj_set_pos(ntp_label, 200, 20);
  lv_obj_add_flag(ntp_label, LV_OBJ_FLAG_HIDDEN);

  // sound
  sound_label = lv_label_create(view);
  lv_label_set_text(sound_label, LV_SYMBOL_AUDIO);
  lv_obj_set_style_text_color(sound_label, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(sound_label, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_align(sound_label, LV_ALIGN_BOTTOM_MID, 40, -4);
  lv_obj_add_flag(sound_label, LV_OBJ_FLAG_HIDDEN);

  // brightness
  brightness_label = lv_label_create(view);
  lv_label_set_text(brightness_label, LV_SYMBOL_EYE_OPEN);
  lv_obj_set_style_text_color(brightness_label, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(brightness_label, &lv_font_montserrat_20,
                             LV_PART_MAIN);
  lv_obj_align(brightness_label, LV_ALIGN_BOTTOM_MID, -40, -4);
  lv_obj_add_flag(brightness_label, LV_OBJ_FLAG_HIDDEN);

  // timer for clock (call every 1000ms)
  lv_timer_create(
      [](lv_timer_t *timer) {
        time_t now;
        struct tm timeinfo;
        char time_str[3];
        char date_str[11];
        time(&now);
        localtime_r(&now, &timeinfo);
        sprintf(time_str, "%02d", timeinfo.tm_hour);
        lv_label_set_text(hour_label, time_str);
        sprintf(time_str, "%02d", timeinfo.tm_min);
        lv_label_set_text(minute_label, time_str);
        sprintf(time_str, "%02d", timeinfo.tm_sec);
        lv_label_set_text(second_label, time_str);
        strftime(date_str, sizeof(date_str), "%Y-%m-%d", &timeinfo);
        lv_label_set_text(date_label, date_str);
      },
      1000, NULL);

  // check for battery level each 30 seconds
  lv_timer_create([](lv_timer_t *timer) { update_battery_status(); }, 30000,
                  NULL);

  // touch event to change state
  lv_obj_add_event_cb(view, touch_event_change_state, LV_EVENT_PRESSED, NULL);

  // double-click in the upper right corner to get the time via ntp
  lv_obj_t *double_tap_area = lv_obj_create(view);
  lv_obj_set_size(double_tap_area, 60, 120);
  lv_obj_align(double_tap_area, LV_ALIGN_TOP_LEFT, 180, 0);
  lv_obj_set_style_bg_opa(double_tap_area, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(double_tap_area, 0, 0);
  lv_obj_set_style_outline_width(double_tap_area, 0, 0);

  // touch event to get ntp time
  lv_obj_add_event_cb(
      double_tap_area,
      [](lv_event_t *e) {
        uint32_t now = lv_tick_get();
        if (now - ntp_last_click_time < 500) { // double click in 500ms?
          Serial.println("double-tap detected");
          lv_label_set_text(ntp_label, LV_SYMBOL_WIFI);
          lv_obj_clear_flag(ntp_label, LV_OBJ_FLAG_HIDDEN);
          if (WiFi.status() == WL_CONNECTED) {
            Serial.println("already connected to WiFi");
          } else {
            Serial.println("not connected, attempting to connect...");
            WiFi.begin(ssid, password);
          }
          unsigned long startAttemptTime = millis();
          while (WiFi.status() != WL_CONNECTED) {
            if (millis() - startAttemptTime > 5000) {
              Serial.println("failed to connect to WiFi within 5 seconds");
              lv_label_set_text(ntp_label, LV_SYMBOL_WARNING);
              delay(1000);
              lv_obj_add_flag(ntp_label, LV_OBJ_FLAG_HIDDEN);
              break;
            }
            delay(100);
            lv_task_handler();
          }
          if (WiFi.status() == WL_CONNECTED) {
            Serial.println("connected to WiFi");
            sntp_set_time_sync_notification_cb(ntp_callback_function);
            configTzTime(time_zone, ntpServer1, ntpServer2);
          }
        }
        ntp_last_click_time = now;
      },
      LV_EVENT_CLICKED, NULL);

  //  init variable for sleep functionality
  last_activity_time = millis();

  // disable unused interfaces
  watch.disableALDO4(); // radio VDD
  watch.disableBLDO2(); // drv2605

  file = new AudioFileSourcePROGMEM();

  // Set up the use of an external decoder
  out = new AudioOutputI2S(i2sPort, AudioOutputI2S::EXTERNAL_I2S);

  // Set up hardware connection
  out->SetPinout(BOARD_DAC_IIS_BCK, BOARD_DAC_IIS_WS, BOARD_DAC_IIS_DOUT);

  // Adjust to appropriate gain
  out->SetGain(0.2);

  id3 = new AudioFileSourceID3(file);
  mp3 = new AudioGeneratorMP3();
  xTaskCreate(playMP3Task, "mp3", 8192, NULL, 10, &playMP3Handler);
}

void loop() {
  if (millis() - last_activity_time > sleep_timeout) {
    esp_sleep_enable_ext1_wakeup(_BV(BOARD_TOUCH_INT), ESP_EXT1_WAKEUP_ALL_LOW);
    watch.setBrightness(0);
    esp_light_sleep_start();
  }
  if (watch.getTouched()) {
    if (brightnesshi) {
      watch.setBrightness(100);
    } else {
      watch.setBrightness(50);
    }
    last_activity_time = millis();
  }
  lv_timer_handler(); // LVGL-Aufgaben verarbeiten
  delay(10);
}
