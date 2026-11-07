#include "status_popover.h"

#include <Arduino.h>
#include <ctype.h>
#include <lvgl.h>
#include <string.h>

#include "extra/libs/qrcode/lv_qrcode.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/f2b_api.h"
#include "theme.h"
#include "ui/quick_panel.h"
#include "ui_common.h"

#define SP_BOX_W     448
#define SP_PAD        14
#define SP_QR        120
#define SP_BTN_W     180
#define SP_BTN_H      44

static lv_obj_t *s_pop = nullptr;

bool isStatusPopoverOpen() { return s_pop != nullptr; }

void closeStatusPopover() { releaseScreen(&s_pop); }

static void closeCb(lv_event_t *e) {
  (void)e;
  // Deleted on the next pass: this runs inside one of the popover's objects.
  if (s_pop) { lv_obj_del_async(s_pop); s_pop = nullptr; }
}

// "<app>/?barcode=<code>", the code percent-encoded. False when it does not fit.
static bool appLinkFor(const char* code, char* out, size_t n) {
  size_t used = (size_t)snprintf(out, n, "%s/?barcode=", g_f2b_url);
  if (used >= n) return false;
  static const char hex[] = "0123456789ABCDEF";
  for (const char* p = code; *p; p++) {
    const unsigned char c = (unsigned char)*p;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      if (used + 1 >= n) return false;
      out[used++] = (char)c;
    } else {
      if (used + 3 >= n) return false;
      out[used++] = '%';
      out[used++] = hex[c >> 4];
      out[used++] = hex[c & 0x0F];
    }
  }
  out[used] = '\0';
  return true;
}

void showStatusPopover(const char* text, const char* code) {
  if (!text || !text[0]) return;
  logSDf("SHOW: StatusPopover%s", code && code[0] ? " (code)" : "");
  releaseScreen(&s_pop);

  char link[160] = "";
  const bool with_qr = code && code[0] && g_f2b_url[0] && appLinkFor(code, link, sizeof(link));
  const int box_h = with_qr ? 290 : 190;
  const int text_w = with_qr ? SP_BOX_W - 3 * SP_PAD - SP_QR : SP_BOX_W - 2 * SP_PAD;

  lv_obj_t *pop = lv_obj_create(lv_scr_act());
  s_pop = pop;
  lv_obj_set_size(pop, 480, 320);
  lv_obj_set_pos(pop, 0, 0);
  lv_obj_set_style_bg_color(pop, lv_color_hex(UI_COL_SCRIM), 0);
  lv_obj_set_style_bg_opa(pop, UI_OPA_SCRIM, 0);
  lv_obj_set_style_border_width(pop, 0, 0);
  lv_obj_set_style_radius(pop, 0, 0);
  lv_obj_set_style_pad_all(pop, 0, 0);
  lv_obj_clear_flag(pop, LV_OBJ_FLAG_SCROLLABLE);
  // A tap beside the box closes it, as on the weight popup.
  lv_obj_add_event_cb(pop, [](lv_event_t *e) {
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) closeCb(e);
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_t *box = lv_obj_create(pop);
  lv_obj_set_size(box, SP_BOX_W, box_h);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_POPUP_BORDER), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, UI_RADIUS_BOX, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  uiSheetIn(box);

  // The message itself, and below it what to do about an unknown code. Both
  // in a column that scrolls, should a language need more room.
  lv_obj_t *col = lv_obj_create(box);
  lv_obj_set_size(col, text_w, box_h - SP_PAD - SP_BTN_H - 2 * SP_PAD);
  lv_obj_set_pos(col, SP_PAD, SP_PAD);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_style_pad_row(col, 10, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(col, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_AUTO);

  lv_obj_t *msg = lv_label_create(col);
  lv_label_set_text(msg, text);
  lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(msg, text_w - 6);
  lv_obj_set_style_text_color(msg, lv_color_hex(UI_COL_INK), 0);
  lv_obj_set_style_text_font(msg, UI_FONT_TITLE, 0);

  if (with_qr) {
    lv_obj_t *help = lv_label_create(col);
    lv_label_set_text(help, T(STR_NS_NOT_FOUND_HELP));
    lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(help, text_w - 6);
    lv_obj_set_style_text_color(help, lv_color_hex(UI_COL_INK_2), 0);
    lv_obj_set_style_text_font(help, UI_FONT_SMALL, 0);

    lv_obj_t *qr = lv_qrcode_create(box, SP_QR, lv_color_hex(UI_COL_QR_DARK),
                                    lv_color_hex(UI_COL_QR_LIGHT));
    lv_qrcode_update(qr, link, strlen(link));
    // The white frame is the quiet zone a phone needs around the code.
    lv_obj_set_style_border_color(qr, lv_color_hex(UI_COL_QR_LIGHT), 0);
    lv_obj_set_style_border_width(qr, 5, 0);
    lv_obj_set_pos(qr, SP_BOX_W - SP_PAD - SP_QR - 10, SP_PAD);
    lv_obj_t *cap = lv_label_create(box);
    lv_label_set_text(cap, T(STR_STATUS_QR_CAP));
    lv_label_set_long_mode(cap, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(cap, SP_QR + 10);
    lv_obj_set_style_text_color(cap, lv_color_hex(UI_COL_INK_SOFT), 0);
    lv_obj_set_style_text_font(cap, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(cap, SP_BOX_W - SP_PAD - SP_QR - 10, SP_PAD + SP_QR + 16);
    logSDf("Status popover: app link %s", link);
  }

  lv_obj_t *btn = lv_btn_create(box);
  lv_obj_set_size(btn, SP_BTN_W, SP_BTN_H);
  lv_obj_set_pos(btn, (SP_BOX_W - SP_BTN_W) / 2, box_h - SP_PAD - SP_BTN_H);
  lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_POPUP_BORDER), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  uiSecondaryFill(btn);
  lv_obj_add_event_cb(btn, closeCb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *l = lv_label_create(btn);
  lv_label_set_text(l, T(STR_BT_CLOSE));
  lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(l, UI_FONT_BODY, 0);
  lv_obj_center(l);
}
