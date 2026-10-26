// The scale as the converter sees it: which chip answered, the raw count, the
// weight, how far the readings scatter. Built for the first hour after
// soldering, when the question is "does it react at all, and the right way
// round", and for the day the numbers look wrong.
//
// Also the switch for zero tracking, which belongs to the weighing rather
// than to any other settings card.
#include "web/web_pages.h"

#include <Arduino.h>
#include <WebServer.h>

#include "app/app_state.h"
#include "app_config.h"
#include "hardware/scale.h"
#include "hardware/sd_logger.h"
#include "services/auto_zero.h"
#include "services/user_options.h"
#include "web/web_access.h"
#include "web/web_shell.h"
// Last on purpose: T() is a macro and ArduinoJson uses T as a template
// parameter, so lang.h has to come after anything that pulls it in.
#include "lang.h"

static const char* label() { return T(STR_W_NAV_SCALE); }

static bool applies() { return g_scale_fitted; }

static void row(String &h, const char *id, int label_id) {
  h += F("<tr><td>");
  h += T(label_id);
  h += F("</td><td id='");
  h += id;
  h += F("' style='color:var(--ink);font-variant-numeric:tabular-nums'>-</td></tr>");
}

static String body() {
  String h;
  h.reserve(3600);
  h += F("<div class='grid'><div class='card wide'><h2>");
  h += T(STR_W_C_SCALE_LIVE);
  h += F("</h2><p class='hint'>");
  h += T(STR_W_SCALE_LIVE_HINT);
  h += F("</p><table>");
  row(h, "sl-adc", STR_W_SCALE_ADC);
  row(h, "sl-raw", STR_W_SCALE_RAW);
  row(h, "sl-g", STR_W_SCALE_WEIGHT);
  row(h, "sl-pp", STR_W_SCALE_SPREAD);
  row(h, "sl-drift", STR_W_SCALE_DRIFT);
  row(h, "sl-rej", STR_W_SCALE_REJECTED);
  row(h, "sl-cal", STR_W_SCALE_CAL);
  row(h, "sl-zero", STR_W_SCALE_ZERO);
  // The raw value over the last twenty minutes. Warm-up drift is a curve that
  // flattens out, a loose connection is a step, interference is a fuzz.
  h += F("</table>"
         "<svg id='sl-chart' viewBox='0 0 300 60' preserveAspectRatio='none' "
         "style='width:100%;height:90px;margin-top:12px;background:var(--ink-4);"
         "border-radius:6px'><polyline id='sl-line' fill='none' "
         "stroke='var(--accent)' stroke-width='1.5' "
         "vector-effect='non-scaling-stroke' points=''/></svg>"
         "<p class='hint' id='sl-range'></p></div>"
         "<div class='card wide'><h2>");
  h += T(STR_W_C_AUTO_ZERO);
  h += F("</h2><div class='field'><label class='check'><span class='switch'>"
         "<input id='az' type='checkbox'");
  if (g_auto_zero) h += F(" checked");
  h += F("><i></i></span>");
  h += T(STR_W_AUTO_ZERO);
  h += F("</label><span class='hint'>");
  h += T(STR_W_AUTO_ZERO_HINT);
  h += F("</span><span class='msg' id='az-s'></span></div></div></div>"
         "<script>");
  h += webShellJsStrings();
  h += F("const SL={none:");
  h += jsStr(T(STR_W_SCALE_NONE));
  h += F(",raw:");
  h += jsStr(T(STR_W_SCALE_RAW));
  h += F("};"
         "const SLH=[];"
         "function slSet(id,v){const e=$(id);if(e)e.textContent=v;}"
         // Least squares over the last minute: raw counts per minute.
         "function slSlope(){"
         "const end=SLH[SLH.length-1].t,p=SLH.filter(x=>x.t>=end-60000);"
         "if(p.length<20||end-p[0].t<30000)return null;"
         "let n=p.length,sx=0,sy=0,sxx=0,sxy=0;"
         "for(const x of p){const t=(x.t-p[0].t)/60000;sx+=t;sy+=x.r;sxx+=t*t;sxy+=t*x.r;}"
         "const den=n*sxx-sx*sx;return den?(n*sxy-sx*sy)/den:null;}"
         "function slChart(){"
         "if(SLH.length<2)return;"
         "let lo=SLH[0].r,hi=lo;for(const x of SLH){if(x.r<lo)lo=x.r;if(x.r>hi)hi=x.r;}"
         "const t0=SLH[0].t,dt=SLH[SLH.length-1].t-t0||1,dr=hi-lo||1;"
         "$('sl-line').setAttribute('points',SLH.map(x=>"
         "((x.t-t0)/dt*300).toFixed(1)+','+(57-(x.r-lo)/dr*54).toFixed(1)).join(' '));"
         "slSet('sl-range',SL.raw+' '+lo+' ... '+hi+' ('+Math.round(dt/60000)+' min)');}"
         "function slTick(){"
         "if(document.hidden)return;"
         "fetch('/api/scale/live',{cache:'no-store'}).then(r=>r.json()).then(d=>{"
         "slSet('sl-adc',d.ready?d.adc:SL.none);"
         "slSet('sl-raw',d.ready?d.raw:'-');"
         "slSet('sl-g',d.ready?d.g.toFixed(1)+' g':'-');"
         "slSet('sl-pp',d.full?d.spread.toFixed(2)+' g':'-');"
         "slSet('sl-rej',d.rej);"
         "slSet('sl-cal',d.cal.toFixed(4));"
         "slSet('sl-zero',d.zero);"
         "if(!d.ready)return;"
         "const now=Date.now();SLH.push({t:now,r:d.raw});"
         "while(SLH[0].t<now-1200000)SLH.shift();"
         "const k=slSlope();"
         "slSet('sl-drift',k===null?'-':(d.cald?(k/d.cal).toFixed(2)+' g/min  (':'(')+Math.round(k)+'/min)');"
         "slChart();}).catch(()=>{});}"
         "slTick();setInterval(slTick,500);"
         "$('az').addEventListener('change',()=>{"
         "const want=$('az').checked;"
         "post('/api/scale/autozero',want?'1':'0').then(r=>{"
         "if(!r.ok)$('az').checked=!want;"
         "flash('az-s',r.ok?WS.ok:WS.err,!r.ok,4000);});});"
         "</script>");
  return h;
}

static void routes(WebServer &srv) {
  srv.on("/api/scale/live", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_SCALE))) return;
    float lo = scale_filter_buf[0], hi = scale_filter_buf[0];
    for (int i = 1; i < SCALE_FILTER_SIZE; i++) {
      if (scale_filter_buf[i] < lo) lo = scale_filter_buf[i];
      if (scale_filter_buf[i] > hi) hi = scale_filter_buf[i];
    }
    String j = F("{\"ready\":");
    j += scale_ready ? F("true") : F("false");
    j += F(",\"adc\":\"");
    j += scaleHardwareName();
    j += F("\",\"raw\":");
    j += scale_ready ? String(scaleHardwareReadRaw()) : String(0);
    j += F(",\"g\":");
    j += String(scale_weight_g, 2);
    j += F(",\"full\":");
    j += scale_filter_full ? F("true") : F("false");
    j += F(",\"spread\":");
    j += String(hi - lo, 3);
    j += F(",\"cald\":");
    j += cal_factor != CAL_FACTOR_DEFAULT ? F("true") : F("false");
    j += F(",\"cal\":");
    j += String(cal_factor, 4);
    j += F(",\"zero\":");
    j += String(zero_offset);
    j += F(",\"rej\":");
    j += String(scaleRejectedSamples());
    j += F("}");
    srv.send(200, "application/json", j);
  });

  srv.on("/api/scale/autozero", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_SCALE))) return;
    const bool on = (srv.arg("plain").toInt() != 0);
    autoZeroSet(on);
    logSDf("Web: auto zero -> %s", on ? "ON" : "OFF");
    srv.send(200, "application/json", on ? "{\"ok\":true,\"v\":1}"
                                         : "{\"ok\":true,\"v\":0}");
  });
}

extern const WebPage PAGE_SCALE;
const WebPage PAGE_SCALE = {
  "/scale", label, GATE_CONFIG, applies,
  body, routes
};
