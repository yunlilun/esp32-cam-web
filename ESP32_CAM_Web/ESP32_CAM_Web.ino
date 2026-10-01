#include <Arduino.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <FS.h>
#include <SD_MMC.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>

// ==================== 摄像头引脚 (OV3660) ====================
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5
#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM       8
#define Y3_GPIO_NUM       9
#define Y2_GPIO_NUM       11
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM     13

// ==================== SD卡 ====================
#define SD_CLK 39
#define SD_CMD 38
#define SD_D0  40

// ==================== LED ====================
#define LED_PIN 48
Adafruit_NeoPixel led(1, LED_PIN, NEO_GRB + NEO_KHZ800);

httpd_handle_t cam_httpd = NULL;
unsigned long bootTime = 0;

bool recording = false;
File videoFile;
int videoCounter = 0;

Preferences prefs;
String apPassword = "12345678";

void setLED(uint8_t r, uint8_t g, uint8_t b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}

// ==================== 摄像头初始化 ====================
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;
  return esp_camera_init(&config) == ESP_OK;
}

bool initSD() {
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  if (!SD_MMC.begin("/sdcard", true, true, SDMMC_FREQ_DEFAULT, 5)) return false;
  return SD_MMC.cardType() != CARD_NONE;
}

// ==================== HTML 页面 ====================
const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-S3-CAM 控制台</title>
<style>
:root{--bg:#0f1115;--card:#1a1d24;--line:#2a2f3a;--txt:#e6e8ec;--sub:#8b93a7;--accent:#3b82f6;--green:#10b981;--red:#ef4444}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,"Segoe UI",sans-serif;background:var(--bg);color:var(--txt);padding:12px}
.wrap{max-width:1280px;margin:0 auto}
header{display:flex;justify-content:space-between;align-items:center;padding:10px 4px;border-bottom:1px solid var(--line);margin-bottom:12px}
h1{font-size:18px}.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--red);margin-right:6px}.dot.on{background:var(--green)}
.grid{display:grid;grid-template-columns:1fr 340px;gap:12px}@media(max-width:900px){.grid{grid-template-columns:1fr}}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px;margin-bottom:12px}
.card h2{font-size:13px;color:var(--sub);font-weight:600;text-transform:uppercase;margin-bottom:10px}
.vbox{position:relative;background:#000;border-radius:8px;overflow:hidden;aspect-ratio:4/3}
.vbox img{width:100%;height:100%;object-fit:contain;display:block}
.vov{position:absolute;top:8px;left:8px;background:rgba(0,0,0,.6);color:#fff;font-size:11px;padding:3px 8px;border-radius:4px}
.brow{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}
button{background:var(--accent);color:#fff;border:none;border-radius:6px;padding:8px 14px;font-size:13px;cursor:pointer}
button:hover{filter:brightness(1.1)}button.red{background:var(--red)}button.green{background:var(--green)}button.ghost{background:transparent;border:1px solid var(--line);color:var(--txt)}button.sm{padding:5px 10px;font-size:12px}
.row{display:flex;justify-content:space-between;align-items:center;padding:6px 0;border-bottom:1px solid var(--line);font-size:13px}
.row:last-child{border-bottom:none}.row label{color:var(--sub)}
select{width:100%;background:#0f1115;border:1px solid var(--line);color:var(--txt);border-radius:6px;padding:6px;font-size:13px}
.sl{display:flex;align-items:center;gap:10px;margin:6px 0}.sl span:first-child{width:60px;font-size:12px;color:var(--sub)}.sl span:last-child{width:36px;text-align:right;font-size:12px;color:var(--accent);font-weight:600}
input[type=range]{flex:1;background:#0f1115;border:1px solid var(--line);border-radius:6px;padding:0;height:6px}
input[type=text],input[type=password]{width:100%;background:#0f1115;border:1px solid var(--line);color:var(--txt);border-radius:6px;padding:6px;font-size:13px}
.fl{max-height:280px;overflow-y:auto;font-size:12px}
.fi{display:flex;align-items:center;justify-content:space-between;padding:8px;border-bottom:1px solid var(--line)}
.fi:hover{background:#232833}.fi .nm{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.fi .sz{color:var(--sub);margin:0 8px;font-size:11px}
.tg{position:relative;width:40px;height:22px;background:#374151;border-radius:11px;cursor:pointer;transition:.2s}
.tg.on{background:var(--accent)}.tg::after{content:"";position:absolute;top:2px;left:2px;width:18px;height:18px;background:#fff;border-radius:50%;transition:.2s}.tg.on::after{transform:translateX(18px)}
.toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%);background:var(--green);color:#fff;padding:10px 20px;border-radius:6px;font-size:13px;opacity:0;transition:.3s;pointer-events:none;z-index:999}.toast.show{opacity:1}.toast.err{background:var(--red)}
</style></head><body>
<div class="wrap">
<header><h1>📷 ESP32-S3-CAM 控制台</h1><div><span class="dot" id="dot"></span><span id="ct" style="font-size:12px;color:var(--sub)">未连接</span></div></header>
<div class="grid">
<div>
<div class="card"><h2>实时画面</h2>
<div class="vbox"><img id="stream" alt="点击开始"><div class="vov" id="fps">FPS: --</div></div>
<div class="brow">
<button class="green" onclick="startStream()">▶ 开始视频流</button>
<button class="red" onclick="stopStream()">■ 停止视频流</button>
<button onclick="location.href='/capture?t='+Date.now()">📸 拍照</button>
<button id="recBtn" class="red" onclick="toggleRecord()">⏺ 开始录像</button>
</div></div>

<div class="card"><h2>特效 / 镜像</h2>
<div class="row"><label>特效滤镜</label><select id="special_effect" onchange="setVar('special_effect',this.value)">
<option value="0">无</option><option value="1">负片</option><option value="2">灰度</option>
<option value="3">红色调</option><option value="4">绿色调</option><option value="5">蓝色调</option><option value="6">复古</option>
</select></div>
<div class="row"><label>水平镜像</label><div class="tg" id="t_hmirror" onclick="tglVar('hmirror',this)"></div></div>
<div class="row"><label>垂直翻转</label><div class="tg" id="t_vflip" onclick="tglVar('vflip',this)"></div></div>
<div class="row"><label>白平衡</label><select id="wb_mode" onchange="setVar('wb_mode',this.value)">
<option value="0">自动</option><option value="1">晴天</option><option value="2">阴天</option>
<option value="3">办公室</option><option value="4">室内</option></select></div>
</div>

<div class="card"><h2>画面调节</h2>
<div class="row"><label>分辨率</label><select id="framesize" onchange="setVar('framesize',this.value)">
<option value="1">160x120</option><option value="4">240x240</option>
<option value="5">320x240 QVGA</option><option value="6">400x296</option>
<option value="8">640x480 VGA</option><option value="9">800x600</option><option value="10">1024x768</option>
</select></div>
<div class="row"><label>图像质量</label><select id="quality" onchange="setVar('quality',this.value)">
<option value="6">极高</option><option value="10">高</option>
<option value="15">中</option><option value="20">低</option>
</select></div>
<div class="sl"><span>亮度</span><input type="range" id="brightness" min="-2" max="2" value="0" oninput="setVar('brightness',this.value);document.getElementById('v_brightness').textContent=this.value"><span id="v_brightness">0</span></div>
<div class="sl"><span>对比度</span><input type="range" id="contrast" min="-2" max="2" value="0" oninput="setVar('contrast',this.value);document.getElementById('v_contrast').textContent=this.value"><span id="v_contrast">0</span></div>
<div class="sl"><span>饱和度</span><input type="range" id="saturation" min="-2" max="2" value="0" oninput="setVar('saturation',this.value);document.getElementById('v_saturation').textContent=this.value"><span id="v_saturation">0</span></div>
</div>

<div class="card"><h2>TF 卡文件</h2>
<div class="brow"><button class="sm" onclick="loadFiles()">🔄 刷新列表</button>
<button class="sm ghost" onclick="deleteAll()">🗑 清空所有</button></div>
<div class="fl" id="fl" style="margin-top:10px"><div style="color:var(--sub);text-align:center;padding:20px">点击刷新加载</div></div>
<div class="row" style="margin-top:8px"><label>已用 / 总容量</label><span id="sdInfo">-- / --</span></div>
</div>
</div>

<div>
<div class="card"><h2>系统状态</h2>
<div class="row"><label>运行时间</label><span id="uptime">--</span></div>
<div class="row"><label>可用内存</label><span id="heap">--</span></div>
<div class="row"><label>PSRAM</label><span id="psram">--</span></div>
<div class="row"><label>WiFi 信号</label><span id="rssi">--</span></div>
<div class="row"><label>IP 地址</label><span id="ip">--</span></div>
<div class="row"><label>传感器</label><span id="sensor">--</span></div>
</div>
<div class="card"><h2>系统设置</h2>
<div class="row"><label>AP 密码</label><input type="text" id="apPass" style="width:120px"></div>
<div class="brow" style="margin-top:6px">
<button class="sm" onclick="saveApPass()">保存密码</button>
<button class="sm red" onclick="if(confirm('确定重启？'))location.href='/restart'">重启设备</button>
</div>
<div style="font-size:11px;color:var(--sub);margin-top:6px">密码至少 8 位，保存后需重启生效</div>
</div>
<div class="card"><h2>操作</h2>
<div class="brow">
<button class="sm" onclick="refreshAll()">刷新</button>
<button class="sm ghost" onclick="resetAll()">恢复默认</button>
</div></div>
</div>
</div></div>

<div class="toast" id="toast"></div>
<script>
let streaming=false,frames=0,lastT=Date.now();
const $=id=>document.getElementById(id);
function startStream(){$('stream').src='/stream?t='+Date.now();streaming=true;frames=0;lastT=Date.now();
$('stream').onload=()=>frames++;
setInterval(()=>{if(!streaming)return;const n=Date.now();$('fps').textContent='FPS: '+Math.round(frames*1000/(n-lastT));frames=0;lastT=n;},1000);toast('视频流已开始');}
function stopStream(){$('stream').src='';streaming=false;$('fps').textContent='FPS: --';toast('已停止');}
function toggleRecord(){fetch('/record?action='+(document.getElementById('recBtn').dataset.rec==='1'?'stop':'start')).then(r=>r.json()).then(d=>{
const b=$('recBtn');b.dataset.rec=d.recording?'1':'0';b.textContent=d.recording?'⏹ 停止录像':'⏺ 开始录像';b.className=d.recording?'green':'red';toast(d.recording?'开始录像':'已停止');});}
function setVar(n,v){fetch('/control?var='+n+'&val='+v).then(()=>{});}
function tglVar(n,el){el.classList.toggle('on');setVar(n,el.classList.contains('on')?'1':'0');}
function setTg(id,v){const e=$(id);if(v)e.classList.add('on');else e.classList.remove('on');}
function loadFiles(){fetch('/list').then(r=>r.json()).then(d=>{
if(!d.files||!d.files.length){$('fl').innerHTML='<div style="color:var(--sub);text-align:center;padding:20px">没有文件</div>';}
else{$('fl').innerHTML=d.files.map(f=>`<div class="fi"><span class="nm">${f.name}</span><span class="sz">${fmt(f.size)}</span>
<button class="sm" onclick="location.href='/download?file=${encodeURIComponent(f.name)}'">下载</button>
<button class="sm red" onclick="delFile('${f.name}')">删</button></div>`).join('');}
$('sdInfo').textContent=fmt(d.used)+' / '+fmt(d.total);});}
function delFile(n){if(!confirm('删除 '+n+' ?'))return;fetch('/delete?file='+encodeURIComponent(n)).then(()=>{toast('已删除');loadFiles();});}
function deleteAll(){if(!confirm('清空全部文件？'))return;fetch('/delete_all').then(()=>{toast('已清空');loadFiles();});}
function refreshAll(){fetch('/status').then(r=>r.json()).then(d=>{
$('uptime').textContent=d.uptime;$('heap').textContent=fmt(d.heap);$('psram').textContent=fmt(d.psram);
$('rssi').textContent=d.rssi+' dBm';$('ip').textContent=d.ip;
$('sensor').textContent=({0x2640:'OV2640',0x3660:'OV3660',0x5640:'OV5640',0x2145:'GC2145',0x40DC:'GC2145'}[d.pid]||'0x'+d.pid.toString(16));
$('framesize').value=d.framesize;$('quality').value=d.quality;
$('brightness').value=d.brightness;$('v_brightness').textContent=d.brightness;
$('contrast').value=d.contrast;$('v_contrast').textContent=d.contrast;
$('saturation').value=d.saturation;$('v_saturation').textContent=d.saturation;
setTg('t_hmirror',d.hmirror);setTg('t_vflip',d.vflip);
if(d.apPass)$('apPass').value=d.apPass;
$('dot').classList.add('on');$('ct').textContent='已连接';
}).catch(()=>{$('dot').classList.remove('on');$('ct').textContent='未连接';});}
function saveApPass(){var p=$('apPass').value;if(p.length<8){toast('密码至少8位',true);return;}
fetch('/set_ap_pass?pass='+encodeURIComponent(p)).then(r=>r.json()).then(d=>{if(d.ok)toast('已保存，重启后生效');else toast('保存失败',true);});}
function resetAll(){['brightness:0','contrast:0','saturation:0','quality:12','framesize:5','special_effect:0','wb_mode:0','hmirror:0','vflip:0'].forEach(s=>{const[k,v]=s.split(':');setVar(k,v);});toast('已恢复默认');setTimeout(refreshAll,500);}
function fmt(b){if(!b)return'0 B';if(b<1024)return b+' B';if(b<1048576)return(b/1024).toFixed(1)+' KB';if(b<1073741824)return(b/1048576).toFixed(1)+' MB';return(b/1073741824).toFixed(2)+' GB';}
let tt;function toast(m,e){const t=$('toast');t.textContent=m;t.className='toast show'+(e?' err':'');clearTimeout(tt);tt=setTimeout(()=>t.className='toast',2000);}
document.addEventListener('keydown',e=>{if(e.target.tagName==='INPUT'||e.target.tagName==='SELECT')return;
if(e.key==='s')startStream();if(e.key==='x')stopStream();if(e.key==='r')toggleRecord();});
refreshAll();setInterval(refreshAll,5000);
</script></body></html>)HTML";

// ==================== 处理器 ====================
esp_err_t index_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
}

esp_err_t status_handler(httpd_req_t* req) {
  static char json[1200];
  sensor_t* s = esp_camera_sensor_get();
  unsigned long up = (millis() - bootTime) / 1000;
  String ip = WiFi.softAPIP().toString();
  snprintf(json, sizeof(json),
    "{\"uptime\":\"%02lu:%02lu:%02lu\",\"heap\":%u,\"psram\":%u,"
    "\"rssi\":%d,\"ip\":\"%s\",\"pid\":%u,\"framesize\":%u,\"quality\":%u,"
    "\"brightness\":%d,\"contrast\":%d,\"saturation\":%d,"
    "\"hmirror\":%u,\"vflip\":%u,\"special_effect\":%u,\"wb_mode\":%u,\"recording\":%d,"
    "\"apPass\":\"%s\"}",
    up/3600, (up%3600)/60, up%60,
    (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
    WiFi.RSSI(), ip.c_str(), s->id.PID, s->status.framesize, s->status.quality,
    s->status.brightness, s->status.contrast, s->status.saturation,
    s->status.hmirror, s->status.vflip, s->status.special_effect, s->status.wb_mode,
    recording ? 1 : 0, apPassword.c_str());
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json, strlen(json));
}

esp_err_t control_handler(httpd_req_t* req) {
  char buf[80], var[32], val[16];
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK) return httpd_resp_send_500(req);
  if (httpd_query_key_value(buf, "var", var, sizeof(var)) != ESP_OK ||
      httpd_query_key_value(buf, "val", val, sizeof(val)) != ESP_OK) return httpd_resp_send_500(req);
  int v = atoi(val);
  sensor_t* s = esp_camera_sensor_get();
  int res = 0;
  if (!strcmp(var, "framesize")) res = s->set_framesize(s, (framesize_t)v);
  else if (!strcmp(var, "quality")) res = s->set_quality(s, v);
  else if (!strcmp(var, "brightness")) res = s->set_brightness(s, v);
  else if (!strcmp(var, "contrast")) res = s->set_contrast(s, v);
  else if (!strcmp(var, "saturation")) res = s->set_saturation(s, v);
  else if (!strcmp(var, "hmirror")) res = s->set_hmirror(s, v);
  else if (!strcmp(var, "vflip")) res = s->set_vflip(s, v);
  else if (!strcmp(var, "special_effect")) res = s->set_special_effect(s, v);
  else if (!strcmp(var, "wb_mode")) res = s->set_wb_mode(s, v);
  else res = -1;
  httpd_resp_set_type(req, "application/json");
  const char* ok = (res == 0) ? "{\"ok\":true}" : "{\"ok\":false}";
  return httpd_resp_send(req, ok, strlen(ok));
}

esp_err_t capture_handler(httpd_req_t* req) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return httpd_resp_send_500(req);
  if (SD_MMC.cardType() != CARD_NONE) {
    char path[48];
    snprintf(path, sizeof(path), "/PHOTO_%lu.jpg", millis() / 1000);
    File f = SD_MMC.open(path, FILE_WRITE);
    if (f) { f.write(fb->buf, fb->len); f.close(); }
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=capture.jpg");
  esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

esp_err_t stream_handler(httpd_req_t* req) {
  camera_fb_t* fb = NULL;
  esp_err_t res = ESP_OK;
  char part_buf[64];
  res = httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
  if (res != ESP_OK) return res;
  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) { res = ESP_FAIL; break; }
    int hlen = snprintf(part_buf, sizeof(part_buf),
      "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", (unsigned)fb->len);
    res = httpd_resp_send_chunk(req, "\r\n--frame\r\n", 11);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, part_buf, hlen);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    if (recording && videoFile) videoFile.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    if (res != ESP_OK) break;
    vTaskDelay(pdMS_TO_TICKS(30));
  }
  return res;
}

esp_err_t list_handler(httpd_req_t* req) {
  static char json[4096];
  int pos = 0;
  pos += snprintf(json + pos, sizeof(json) - pos, "{\"files\":[");
  bool first = true;
  uint64_t totalSize = 0, usedSize = 0;
  if (SD_MMC.cardType() != CARD_NONE) {
    totalSize = SD_MMC.totalBytes();
    usedSize = SD_MMC.usedBytes();
    File root = SD_MMC.open("/");
    File f = root.openNextFile();
    while (f && pos < (int)sizeof(json) - 200) {
      if (!f.isDirectory()) {
        if (!first) pos += snprintf(json + pos, sizeof(json) - pos, ",");
        pos += snprintf(json + pos, sizeof(json) - pos,
          "{\"name\":\"%s\",\"size\":%u}", f.name(), (unsigned)f.size());
        first = false;
      }
      f = root.openNextFile();
    }
  }
  pos += snprintf(json + pos, sizeof(json) - pos,
    "],\"total\":%llu,\"used\":%llu}", (unsigned long long)totalSize, (unsigned long long)usedSize);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json, strlen(json));
}

esp_err_t download_handler(httpd_req_t* req) {
  char buf[128], file[64];
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK) return httpd_resp_send_500(req);
  if (httpd_query_key_value(buf, "file", file, sizeof(file)) != ESP_OK) return httpd_resp_send_500(req);
  char path[80];
  snprintf(path, sizeof(path), "/%s", file);
  File f = SD_MMC.open(path);
  if (!f) return httpd_resp_send_404(req);
  httpd_resp_set_type(req, "application/octet-stream");
  char cd[128];
  snprintf(cd, sizeof(cd), "attachment; filename=%s", file);
  httpd_resp_set_hdr(req, "Content-Disposition", cd);
  char chunk[1024];
  while (f.available()) {
    size_t n = f.readBytes(chunk, sizeof(chunk));
    if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) break;
  }
  f.close();
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

esp_err_t delete_handler(httpd_req_t* req) {
  char buf[128], file[64];
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK) return httpd_resp_send_500(req);
  if (httpd_query_key_value(buf, "file", file, sizeof(file)) != ESP_OK) return httpd_resp_send_500(req);
  char path[80];
  snprintf(path, sizeof(path), "/%s", file);
  SD_MMC.remove(path);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, "{\"ok\":true}", 11);
}

esp_err_t delete_all_handler(httpd_req_t* req) {
  if (SD_MMC.cardType() != CARD_NONE) {
    File root = SD_MMC.open("/");
    File f = root.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        char path[128];
        snprintf(path, sizeof(path), "/%s", f.name());
        SD_MMC.remove(path);
      }
      f = root.openNextFile();
    }
  }
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, "{\"ok\":true}", 11);
}

esp_err_t record_handler(httpd_req_t* req) {
  char buf[64] = {0}, action[16] = {0};
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
    httpd_query_key_value(buf, "action", action, sizeof(action));
  }
  if (!strcmp(action, "start") && !recording) {
    char path[48];
    snprintf(path, sizeof(path), "/VIDEO_%03d.mjpeg", ++videoCounter);
    videoFile = SD_MMC.open(path, FILE_WRITE);
    if (videoFile) recording = true;
  } else if (!strcmp(action, "stop") && recording) {
    recording = false;
    if (videoFile) videoFile.close();
  }
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, recording ? "{\"recording\":true}" : "{\"recording\":false}", HTTPD_RESP_USE_STRLEN);
}

esp_err_t set_ap_pass_handler(httpd_req_t* req) {
  char buf[128], pass[64];
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK) return httpd_resp_send_500(req);
  if (httpd_query_key_value(buf, "pass", pass, sizeof(pass)) != ESP_OK) return httpd_resp_send_500(req);
  if (strlen(pass) < 8) {
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":false,\"msg\":\"密码至少8位\"}", HTTPD_RESP_USE_STRLEN);
  }
  apPassword = String(pass);
  prefs.begin("cam", false);
  prefs.putString("ap_pass", apPassword);
  prefs.end();
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, "{\"ok\":true}", 11);
}

esp_err_t restart_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, "{\"ok\":true}", 11);
  delay(500);
  ESP.restart();
  return ESP_OK;
}

// ==================== 启动服务器（单端口 80） ====================
void startServers() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 16;
  cfg.max_open_sockets = 4;
  cfg.lru_purge_enable = true;
  if (httpd_start(&cam_httpd, &cfg) == ESP_OK) {
    httpd_uri_t u1 = {"/", HTTP_GET, index_handler, NULL};
    httpd_uri_t u2 = {"/status", HTTP_GET, status_handler, NULL};
    httpd_uri_t u3 = {"/control", HTTP_GET, control_handler, NULL};
    httpd_uri_t u4 = {"/capture", HTTP_GET, capture_handler, NULL};
    httpd_uri_t u5 = {"/stream", HTTP_GET, stream_handler, NULL};
    httpd_uri_t u6 = {"/list", HTTP_GET, list_handler, NULL};
    httpd_uri_t u7 = {"/download", HTTP_GET, download_handler, NULL};
    httpd_uri_t u8 = {"/delete", HTTP_GET, delete_handler, NULL};
    httpd_uri_t u9 = {"/delete_all", HTTP_GET, delete_all_handler, NULL};
    httpd_uri_t u10 = {"/record", HTTP_GET, record_handler, NULL};
    httpd_uri_t u11 = {"/set_ap_pass", HTTP_GET, set_ap_pass_handler, NULL};
    httpd_uri_t u12 = {"/restart", HTTP_GET, restart_handler, NULL};
    httpd_register_uri_handler(cam_httpd, &u1);
    httpd_register_uri_handler(cam_httpd, &u2);
    httpd_register_uri_handler(cam_httpd, &u3);
    httpd_register_uri_handler(cam_httpd, &u4);
    httpd_register_uri_handler(cam_httpd, &u5);
    httpd_register_uri_handler(cam_httpd, &u6);
    httpd_register_uri_handler(cam_httpd, &u7);
    httpd_register_uri_handler(cam_httpd, &u8);
    httpd_register_uri_handler(cam_httpd, &u9);
    httpd_register_uri_handler(cam_httpd, &u10);
    httpd_register_uri_handler(cam_httpd, &u11);
    httpd_register_uri_handler(cam_httpd, &u12);
  }
}

void setup() {
  Serial.begin(115200);
  bootTime = millis();

  prefs.begin("cam", true);
  apPassword = prefs.getString("ap_pass", "12345678");
  prefs.end();

  led.begin();
  led.setBrightness(50);
  setLED(0, 0, 255);

  if (!initCamera()) {
    setLED(255, 0, 0);
    Serial.println("Camera init failed");
    while (1) delay(1000);
  }

  if (!initSD()) Serial.println("SD not available");
  else Serial.println("SD OK");

  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-CAM-Setup", apPassword.c_str());
  delay(500);
  Serial.println("==================== AP 模式 ====================");
  Serial.print("SSID: ESP32-CAM-Setup\nPassword: ");
  Serial.println(apPassword);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("=================================================");
  setLED(0, 0, 255);

  startServers();
  Serial.println("Ready. 浏览器访问 http://192.168.4.1");
}

void loop() {
  delay(1000);
}
