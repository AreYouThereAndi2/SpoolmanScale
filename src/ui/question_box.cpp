#include "question_box.h"

#include "lang.h"
#include "ui/theme.h"
#include "ui_common.h"

#define QB_BTN_W      118
#define QB_BTN_MID_W  124
#define QB_BTN_H      56
#define QB_BTN_Y      156

// The box carries the caller's answer byte as its user data, each button its
// own answer, so one callback serves every box there is.
static void answerCb(lv_event_t* e) {
  lv_obj_t* btn = lv_event_get_target(e);
  lv_obj_t* box = lv_obj_get_parent(btn);
  volatile uint8_t* out = (volatile uint8_t*)lv_obj_get_user_data(box);
  if (out) *out = (uint8_t)(intptr_t)lv_event_get_user_data(e);
}

static void addLabel(lv_obj_t* box, const char* text, uint32_t color,
                     const lv_font_t* font, lv_coord_t y, bool wrap) {
  lv_obj_t* l = lv_label_create(box);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_width(l, 360);
  lv_label_set_long_mode(l, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
}

static void addButton(lv_obj_t* box, int str, QbAnswer a, lv_coord_t x, lv_coord_t w) {
  if (str <= 0) return;
  lv_obj_t* b = lv_btn_create(box);
  lv_obj_set_size(b, w, QB_BTN_H);
  lv_obj_set_pos(b, x, QB_BTN_Y);
  lv_obj_set_style_radius(b, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 4, 0);
  uint32_t text = UI_COL_ACCENT;
  if (a == QB_LEFT) {
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_OK_BG), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_OK_BG_PRESSED), LV_STATE_PRESSED);
    text = UI_COL_AMS_YES_TEXT;
  } else if (a == QB_RIGHT) {
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_BAD_BG), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_AMS_NO_PRESSED), LV_STATE_PRESSED);
    text = UI_COL_AMS_NO_TEXT;
  } else {
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_SURFACE), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(UI_COL_LINE), 0);
  }
  lv_obj_add_event_cb(b, answerCb, LV_EVENT_CLICKED, (void*)(intptr_t)a);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, T(str));
  lv_obj_set_width(l, w - 8);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(text), 0);
  lv_obj_set_style_text_font(l, UI_FONT_SMALL, 0);
  lv_obj_center(l);
}

lv_obj_t* questionBoxCreate(const char* title, const char* question, const char* hint,
                            int left_str, int mid_str, int right_str,
                            volatile uint8_t* answer_out) {
  if (answer_out) *answer_out = QB_NONE;
  lv_obj_t* scr = lv_obj_create(lv_scr_act());
  lv_obj_set_size(scr, 480, 320);
  lv_obj_set_pos(scr, 0, 0);
  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COL_SCRIM), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_70, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_radius(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* box = lv_obj_create(scr);
  lv_obj_set_size(box, 400, 236);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_POPUP_BORDER), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, 12, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_user_data(box, (void*)answer_out);

  addLabel(box, title ? title : "", UI_COL_ACCENT, &lv_font_montserrat_ext_14, 16, false);
  addLabel(box, question ? question : "", UI_COL_INK, &lv_font_montserrat_ext_18, 44, true);
  if (hint && hint[0]) addLabel(box, hint, UI_COL_CAPTION, &lv_font_montserrat_ext_14, 96, true);

  addButton(box, left_str,  QB_LEFT,  12,  QB_BTN_W);
  addButton(box, mid_str,   QB_MID,   138, QB_BTN_MID_W);
  addButton(box, right_str, QB_RIGHT, 270, QB_BTN_W);
  return scr;
}
