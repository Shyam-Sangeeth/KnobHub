// The knob on the network (see net.h and, for the messages, core/link.h).
//
// Pairing: Knob Hub sends a random 32-byte key over USB, kept in the flash (Preferences). The knob's Wi-Fi network
// is kept there too, by Knob OS rather than by the Wi-Fi driver.
//
// Pairing mode (Settings > Connection > Wi-Fi setup, or Knob Hub's button), for up to 5 minutes: the knob opens its
// own network KnobHub-XXXX with a password shown on its screen, answers every name with its own address (so phones
// show the setup page by themselves) and serves the setup page at knobhub.local and 192.168.4.1. It lists the
// networks it can see; the one chosen is joined while the page waits, and once it's joined the knob saves it and turns
// its own network off.
//
// Knob Hub over Wi-Fi: a TCP server on port 5719, advertised as knob-xxxx.local. A new connection must prove it holds
// the key before anything else (and the knob proves it too); only then does it replace the connection in use. Every
// line after that is sealed (AES-256-GCM, a key for that connection, nonces counting up each way).
#include "net.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <vector>

#include "core/knob_os.h"

namespace net {
namespace {

constexpr uint16_t LinkPort = 5719;
constexpr uint32_t SetupMs = 5 * 60 * 1000;
// To join a network chosen on the setup page or in Knob Hub. Generous: a router can leave the knob unanswered for a
// while before letting it in (reason 2, which the Wi-Fi driver retries); a board that's never answered may need a lower
// transmit power (WIFI_TX_POWER, see boards/crowpanel_1_46.h).
constexpr uint32_t JoinMs = 60000;
constexpr uint32_t StartJoinAfterMs = 300;   // the page has its answer by then
constexpr uint32_t SetupAfterJoinMs = 10000; // the page shows it worked, then the knob's own network goes
constexpr uint32_t FailedAfterMs = 20000;    // not joined the saved network for this long: "Can't connect"
constexpr uint32_t AuthMs = 5000;
constexpr size_t MaxLine = 512 * 1024;
constexpr size_t KeySize = 32, NonceSize = 16, TagSize = 16;

Preferences prefs;
std::vector<uint8_t> key;  // empty until paired
std::string ssid, password;
std::string id, host, setup_network, setup_password;

std::string to_hex(const uint8_t* data, size_t n) {
    static const char Hex[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) {
        s += Hex[data[i] >> 4];
        s += Hex[data[i] & 15];
    }
    return s;
}

std::string key_id() {
    if (key.empty()) return "";
    uint8_t hash[32];
    mbedtls_sha256(key.data(), key.size(), hash, 0);
    return to_hex(hash, 4);
}

std::string base64(const uint8_t* data, size_t n) {
    size_t out = 0;
    mbedtls_base64_encode(nullptr, 0, &out, data, n);
    std::string s(out, '\0');
    mbedtls_base64_encode(reinterpret_cast<uint8_t*>(&s[0]), s.size(), &out, data, n);
    s.resize(out);
    return s;
}

bool unbase64(const char* text, std::vector<uint8_t>& out) {
    size_t n = 0, len = strlen(text);
    if (mbedtls_base64_decode(nullptr, 0, &n, reinterpret_cast<const uint8_t*>(text), len) ==
        MBEDTLS_ERR_BASE64_INVALID_CHARACTER)
        return false;
    out.resize(n);
    if (mbedtls_base64_decode(out.data(), out.size(), &n, reinterpret_cast<const uint8_t*>(text), len) != 0) return false;
    out.resize(n);
    return true;
}

// HMAC-SHA256 with the pairing key over label, the hub's nonce and the knob's.
void hmac(const char* label, const uint8_t* hub_nonce, const uint8_t* knob_nonce, uint8_t out[32]) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, key.data(), key.size());
    mbedtls_md_hmac_update(&ctx, reinterpret_cast<const uint8_t*>(label), strlen(label));
    mbedtls_md_hmac_update(&ctx, hub_nonce, NonceSize);
    mbedtls_md_hmac_update(&ctx, knob_nonce, NonceSize);
    mbedtls_md_hmac_finish(&ctx, out);
    mbedtls_md_free(&ctx);
}

bool same(const uint8_t* a, const uint8_t* b, size_t n) {
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

// --- Wi-Fi ---

bool setup_on = false;
uint32_t setup_until = 0;
enum class Join { Idle, Joining, Joined, Failed } join = Join::Idle;
std::string join_ssid, join_password;
uint32_t join_since = 0;
int join_channel = 0;  // 0: not seen in the scan
uint8_t join_bssid[6];
bool join_started = false;
// Set when the network being joined gives the knob an address. WiFi.status() can still say connected to the network
// it's leaving for a moment after WiFi.begin, so that alone doesn't mean the new one worked.
volatile bool join_got_ip = false;
volatile int last_reason = 0;  // WIFI_REASON_*, from the last disconnect
int fail_reason = 0;           // last_reason when the join was given up
uint32_t not_joined_since = 0;  // the saved network, while not joined
bool served = false;            // the link's server and mDNS are running for the joined network

DNSServer dns;
WebServer web(80);
int tx_power = 0;  // the board's (see start), or 0 for the driver's own

// Sets the radio's mode, with the board's transmit power (a mode change can start the radio afresh).
void radio_mode(wifi_mode_t mode) {
    WiFi.mode(mode);
    if (tx_power) WiFi.setTxPower(static_cast<wifi_power_t>(tx_power));
}

WifiState state() {
    if (setup_on) return WifiState::Setup;
    if (join == Join::Joining) return WifiState::Connecting;  // not Failed while it tries, however long the saved one was gone
    if (ssid.empty()) return WifiState::Off;
    if (WiFi.status() == WL_CONNECTED) return WifiState::Connected;
    return millis() - not_joined_since > FailedAfterMs ? WifiState::Failed : WifiState::Connecting;
}

void join_saved() {
    if (ssid.empty()) return;
    WiFi.begin(ssid.c_str(), password.c_str());
    not_joined_since = millis();
}

void serve_link();
void stop_link();
void save_joined();

const char SetupPage[] PROGMEM = R"html(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Knob Wi-Fi setup</title>
<style>
body{margin:0;font:16px system-ui,-apple-system,sans-serif;background:#101114;color:#f2f3f5}
main{max-width:420px;margin:0 auto;padding:24px 16px 40px}
h1{font-size:22px;margin:0 0 6px}
p{color:#9aa0aa;margin:0 0 16px;line-height:1.4}
button,input{font:inherit}
.net{display:flex;justify-content:space-between;gap:12px;width:100%;padding:14px;margin:0 0 8px;border:1px solid #2a2d33;
border-radius:10px;background:#181a1f;color:inherit;text-align:left;cursor:pointer}
.net span:last-child{color:#9aa0aa;white-space:nowrap}
.net.on{border-color:#8b7cf6}
.again{justify-content:center;color:#9aa0aa}
label{display:block;margin:18px 0 6px;color:#9aa0aa}
input{width:100%;box-sizing:border-box;padding:12px;border-radius:10px;border:1px solid #2a2d33;background:#181a1f;color:inherit}
.show{display:flex;align-items:center;gap:8px;margin:10px 0 0;color:#9aa0aa;cursor:pointer}
.show input{width:18px;height:18px;margin:0;accent-color:#8b7cf6}
.go{width:100%;margin-top:16px;padding:14px;border:0;border-radius:10px;background:#8b7cf6;color:#fff;font-weight:600;cursor:pointer}
.go:disabled{opacity:.5}
#status{margin-top:16px;min-height:1.4em;line-height:1.4}
</style></head><body><main>
<h1>Wi-Fi setup</h1>
<p>Choose the network your PC is on. The knob joins it, then turns its own network and this page off. This device
may drop off the knob's network while it joins: the knob's screen says how it went.</p>
<div id="nets"><p>Looking for networks...</p></div>
<button class="net again" type="button" id="again">Look again</button>
<form id="form">
<label for="ssid">Network</label><input id="ssid" required autocomplete="off" autocapitalize="off">
<label for="pw">Password</label><input id="pw" type="password" autocomplete="current-password" autocapitalize="off">
<label class="show"><input type="checkbox" id="show"> Show password</label>
<button class="go" id="go">Join</button>
</form>
<div id="status" role="status"></div>
</main><script>
const $=id=>document.getElementById(id);
const esc=s=>s.replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const strength=r=>r>-60?'Strong':r>-75?'Good':'Weak';
// A request that gives up after a few seconds: the knob's network may be gone (see status).
const ask=(url,options)=>fetch(url,{...options,signal:AbortSignal.timeout(4000)});
let own="the knob's network",missed=0;
async function nets(){
  let d;
  try{d=await (await ask('/networks')).json()}catch(e){setTimeout(nets,1500);return}
  own=d.own;
  if(d.scanning){setTimeout(nets,1000);return}
  const list=$('nets');
  list.innerHTML=d.networks.length?'':'<p>No networks found. Type the name below.</p>';
  for(const n of d.networks){
    const b=document.createElement('button');
    b.type='button';b.className='net';
    b.innerHTML='<span>'+esc(n.ssid)+'</span><span>'+(n.open?'Open, ':'')+strength(n.rssi)+'</span>';
    b.onclick=()=>{$('ssid').value=n.ssid;document.querySelectorAll('.net').forEach(x=>x.classList.remove('on'));
      b.classList.add('on');$('pw').focus()};
    list.appendChild(b);
  }
}
$('again').onclick=async()=>{$('nets').innerHTML='<p>Looking for networks...</p>';
  try{await ask('/scan',{method:'POST'})}catch(e){}setTimeout(nets,1500)};
$('form').onsubmit=async e=>{
  e.preventDefault();
  $('go').disabled=true;
  missed=0;
  $('status').textContent='Joining '+$('ssid').value+'...';
  try{await ask('/join',{method:'POST',body:new URLSearchParams({ssid:$('ssid').value,password:$('pw').value})})}catch(e){}
  setTimeout(status,1500);
};
// The knob has one radio: to join the network it moves to that network's channel, and its own network moves too, so
// this phone or PC usually drops off it for a while. Then the knob's screen says how it went.
async function status(){
  let d;
  try{d=await (await ask('/status')).json()}catch(e){
    if(++missed==2)$('status').textContent='This device has left the knob\'s network while the knob joins '+$('ssid').value+
      '. Look at the knob: it says "Connected to '+$('ssid').value+'" when it has joined, and you can close this page.'+
      ' If it says it couldn\'t, join '+own+' again and this page carries on.';
    setTimeout(status,1500);return}
  missed=0;
  if(d.join=='joining'){setTimeout(status,1000);return}
  $('go').disabled=false;
  const n=d.network;
  $('status').textContent=d.join=='joined'?'Joined '+n+'. The knob is turning its own network off: you can close this page.'
    :d.problem=='password'?n+' turned the password down. Check it (Show password helps) and try again.'
    :d.problem=='not_found'?"The knob can't see "+n+'. It only uses 2.4 GHz networks: check that '+n+' has one and the knob is in range.'
    :d.problem=='security'?n+' uses security the knob can\'t use: it needs WPA2 or WPA3.'
    :d.problem=='timeout'?n+" didn't answer in time. Try again."
    :"Couldn't join "+n+' (Wi-Fi error '+d.reason+'). Try again.';
}
$('show').onchange=()=>{$('pw').type=$('show').checked?'text':'password'};
nets();
</script></body></html>)html";

// The networks in the last scan: strongest first, each name once (one network often has several access points).
std::vector<WifiNetwork> scanned_networks() {
    std::vector<WifiNetwork> list;
    int n = WiFi.scanComplete();
    std::vector<int> order;
    for (int i = 0; i < n; i++)
        if (!WiFi.SSID(i).isEmpty()) order.push_back(i);
    std::sort(order.begin(), order.end(), [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });
    for (int i : order) {
        std::string name = WiFi.SSID(i).c_str();
        if (std::any_of(list.begin(), list.end(), [&](const WifiNetwork& w) { return w.ssid == name; })) continue;
        list.push_back({name, static_cast<int>(WiFi.RSSI(i)), WiFi.encryptionType(i) == WIFI_AUTH_OPEN});
    }
    return list;
}

void page_networks() {
    JsonDocument d;
    d["own"] = setup_network;
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        d["scanning"] = true;
    } else if (n == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true);
        d["scanning"] = true;
    } else {
        JsonArray list = d["networks"].to<JsonArray>();
        for (const WifiNetwork& w : scanned_networks()) {
            JsonObject net = list.add<JsonObject>();
            net["ssid"] = w.ssid;
            net["rssi"] = w.rssi;
            net["open"] = w.open;
        }
    }
    String out;
    serializeJson(d, out);
    web.send(200, "application/json", out);
}

// Starts joining a network (from the setup page or Knob Hub): the radio moves a moment later (start_join), so the
// setup page gets its answer first.
void begin_join(const std::string& name, const std::string& pass) {
    join_ssid = name;
    join_password = pass;
    // The strongest access point of that name in the last scan: its channel and address.
    join_channel = 0;
    int best = -1;
    for (int i = 0; i < WiFi.scanComplete(); i++)
        if (join_ssid == WiFi.SSID(i).c_str() && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) best = i;
    if (best >= 0) {
        join_channel = WiFi.channel(best);
        memcpy(join_bssid, WiFi.BSSID(best), sizeof join_bssid);
    }
    join = Join::Joining;
    join_since = millis();
    join_started = false;
    last_reason = 0;
}

void page_join() {
    std::string name = web.arg("ssid").c_str(), pass = web.arg("password").c_str();
    if (name.empty() || name.size() > 32 || pass.size() > 63) {
        web.send(400, "application/json", "{}");
        return;
    }
    begin_join(name, pass);
    web.send(200, "application/json", "{}");
}

// In pairing mode the knob's one radio serves its own network and joins the other, so both must be on one channel:
// if the radio hops between them while joining, the password handshake times out. Its own network moves to the
// chosen one's channel first (the phone or PC on it joins again by itself), and the knob joins that access point.
// Otherwise (from Knob Hub) the Wi-Fi driver picks the access point, as when it rejoins the saved network: tied to the
// strongest one of that name, a join failed (reason 2, the router not answering) where the saved network's rejoin
// worked; another network can have the same name (e.g. "Wi-Fi"), or a router several access points.
void start_join() {
    join_started = true;
    // Already on this network with this password: WiFi.begin keeps the connection, so no new address comes.
    join_got_ip = WiFi.status() == WL_CONNECTED && join_ssid == WiFi.SSID().c_str() && join_password == WiFi.psk().c_str();
    if (setup_on && join_channel > 0) {
        WiFi.softAP(setup_network.c_str(), setup_password.c_str(), join_channel);
        WiFi.begin(join_ssid.c_str(), join_password.c_str(), join_channel, join_bssid);
    } else {
        WiFi.begin(join_ssid.c_str(), join_password.c_str());
    }
}

// Why the last try failed, for the page: the Wi-Fi driver's reason for the last disconnect.
const char* join_problem() {
    switch (fail_reason) {
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "password";
        case WIFI_REASON_NO_AP_FOUND: return "not_found";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: return "security";
        // The router never let it start (not the password): it turned the knob away the whole time.
        case WIFI_REASON_AUTH_EXPIRE:
        case 0: return "timeout";
        default: return "other";
    }
}

void page_status() {
    static const char* const Names[] = {"idle", "joining", "joined", "failed"};
    JsonDocument d;
    d["join"] = Names[static_cast<int>(join)];
    d["network"] = join_ssid;
    if (join == Join::Failed) {
        d["problem"] = join_problem();
        d["reason"] = fail_reason;
    }
    String out;
    serializeJson(d, out);
    web.send(200, "application/json", out);
}

void start_setup() {
    uint8_t digits[8];
    esp_fill_random(digits, sizeof digits);
    setup_password.clear();
    for (uint8_t d : digits) setup_password += static_cast<char>('0' + d % 10);
    setup_on = true;
    setup_until = millis() + SetupMs;
    join = Join::Idle;

    // Knob Hub over Wi-Fi waits until setup is done (the network may change).
    stop_link();
    // While trying for a network that isn't there, the knob would hop channels and its own network with it.
    if (WiFi.status() != WL_CONNECTED) WiFi.disconnect();
    radio_mode(WIFI_AP_STA);
    WiFi.softAP(setup_network.c_str(), setup_password.c_str());
    dns.start(53, "*", WiFi.softAPIP());
    static bool routes = false;
    if (!routes) {
        routes = true;
        web.on("/", HTTP_GET, [] { web.send_P(200, "text/html", SetupPage); });
        web.on("/networks", HTTP_GET, page_networks);
        web.on("/scan", HTTP_POST, [] {
            WiFi.scanDelete();
            WiFi.scanNetworks(true);
            web.send(200, "application/json", "{}");
        });
        web.on("/join", HTTP_POST, page_join);
        web.on("/status", HTTP_GET, page_status);
        // Phones and PCs check a web address to see whether a network needs signing in: send them to the setup page.
        web.onNotFound([] {
            web.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
            web.send(302, "text/plain", "");
        });
    }
    web.begin();
    MDNS.begin("knobhub");
    WiFi.scanNetworks(true);
}

void end_setup() {
    if (!setup_on) return;
    setup_on = false;
    web.stop();
    dns.stop();
    MDNS.end();
    WiFi.softAPdisconnect(true);
    radio_mode(WIFI_STA);
    WiFi.scanDelete();
    if (join == Join::Joined) {
        save_joined();
    } else {
        WiFi.disconnect();
        join_saved();
    }
    join = Join::Idle;
}

void save_joined() {
    ssid = join_ssid;
    password = join_password;
    prefs.putString("ssid", ssid.c_str());
    prefs.putString("password", password.c_str());
}

// A network being joined: kept once joined (in pairing mode, when it ends shortly after); if it can't be, the saved
// one is tried again (in pairing mode, the knob waits for another choice).
void poll_join() {
    uint32_t now = millis();
    if (join == Join::Joining && !join_started) {
        if (now - join_since >= StartJoinAfterMs) start_join();
    } else if (join == Join::Joining) {
        if (join_got_ip && WiFi.status() == WL_CONNECTED) {
            if (setup_on) {
                join = Join::Joined;
                setup_until = now + SetupAfterJoinMs;
            } else {
                save_joined();
                join = Join::Idle;
            }
        } else if (now - join_since > JoinMs) {
            join = Join::Failed;
            fail_reason = last_reason;  // stopping it below is a disconnect of its own
            WiFi.disconnect();
            if (!setup_on) join_saved();
        }
    }
}

// Looking for networks for Knob Hub. A scan can't run while the knob is trying for a network, so a network that isn't
// there is let go of meanwhile.
bool scan_wanted = false, scanning = false, rejoin_after_scan = false;

void poll_scan() {
    if (scan_wanted && join != Join::Joining) {
        if (WiFi.status() != WL_CONNECTED && !setup_on) {
            WiFi.disconnect();
            rejoin_after_scan = !ssid.empty();
        }
        WiFi.scanDelete();
        if (WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING) {
            scan_wanted = false;
            scanning = true;
        }
        return;  // not started: again next time round
    }
    if (scanning && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
        scanning = false;
        if (rejoin_after_scan) join_saved();
        rejoin_after_scan = false;
    }
}

void poll_setup() {
    dns.processNextRequest();
    web.handleClient();
    if (static_cast<int32_t>(millis() - setup_until) >= 0) end_setup();
}

// --- Knob Hub over Wi-Fi ---

WiFiServer server(LinkPort);

struct Peer {
    WiFiClient client;
    std::string data;    // received, not split into lines yet
    size_t scanned = 0;  // data up to here has no line break
    bool too_long = false;

    void reset(const WiFiClient& c) {
        client = c;
        data.clear();
        scanned = 0;
        too_long = false;
    }

    // A whole line, if one has come. too_long is set (and nothing more is read) if one is longer than MaxLine.
    bool line(std::string& out) {
        while (!too_long) {
            size_t end = data.find('\n', scanned);
            if (end != std::string::npos) {
                out.assign(data, 0, end);
                data.erase(0, end + 1);
                scanned = 0;
                if (!out.empty() && out.back() == '\r') out.pop_back();
                if (out.empty()) continue;
                return true;
            }
            scanned = data.size();
            if (data.size() > MaxLine) {
                too_long = true;
                data = std::string();
                return false;
            }
            int available = client.available();
            if (available <= 0) return false;
            uint8_t buffer[1024];
            int n = client.read(buffer, std::min<size_t>(available, sizeof buffer));
            if (n <= 0) return false;
            data.append(reinterpret_cast<char*>(buffer), n);
        }
        return false;
    }

    void write_line(const std::string& s) {
        client.write(reinterpret_cast<const uint8_t*>(s.data()), s.size());
        client.write('\n');
    }
};

// A connection proving it holds the key.
struct Pending {
    Peer peer;
    uint8_t hub_nonce[NonceSize], knob_nonce[NonceSize];
    bool asked = false;  // the knob has answered the hub's nonce with its own and its proof
    uint32_t since = 0;
} pending;
bool pending_on = false;

// The connection in use.
Peer active;
bool active_on = false;
unsigned connections = 0;
mbedtls_gcm_context gcm;
uint64_t sent_count = 0, received_count = 0;
std::deque<std::string> lines;

// Each direction's nonces: 4 bytes saying which way (1 Knob Hub to knob, 2 knob to Knob Hub), then a count.
void nonce(uint32_t way, uint64_t count, uint8_t out[12]) {
    for (int i = 0; i < 4; i++) out[i] = static_cast<uint8_t>(way >> (24 - 8 * i));
    for (int i = 0; i < 8; i++) out[4 + i] = static_cast<uint8_t>(count >> (56 - 8 * i));
}

void drop_pending() {
    if (pending_on) pending.peer.client.stop();
    pending_on = false;
    pending.peer.reset(WiFiClient());
}

void drop_active() {
    if (!active_on) return;
    active.client.stop();
    active.reset(WiFiClient());
    active_on = false;
    mbedtls_gcm_free(&gcm);
    lines.clear();
}

// Moves the pending connection in as the one in use, with this connection's key.
void accept_pending() {
    uint8_t session[32];
    hmac("session", pending.hub_nonce, pending.knob_nonce, session);
    drop_active();
    mbedtls_gcm_init(&gcm);
    mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, session, 256);
    memset(session, 0, sizeof session);
    active.reset(pending.peer.client);
    active.data.swap(pending.peer.data);  // anything Knob Hub sent right after its proof
    active_on = true;
    pending_on = false;
    pending.peer.reset(WiFiClient());
    sent_count = received_count = 0;
    connections++;
}

void poll_pending() {
    std::string line;
    if (!pending.peer.line(line)) {
        if (pending.peer.too_long || !pending.peer.client.connected() || millis() - pending.since > AuthMs)
            drop_pending();
        return;
    }
    JsonDocument msg;
    if (deserializeJson(msg, line) || std::strcmp(msg["t"] | "", "auth") != 0) return drop_pending();
    std::vector<uint8_t> bytes;
    if (!pending.asked) {
        if (key.empty()) {
            pending.peer.write_line("{\"t\":\"auth\",\"error\":\"not paired\"}");
            return drop_pending();
        }
        if (!unbase64(msg["nonce"] | "", bytes) || bytes.size() != NonceSize) return drop_pending();
        memcpy(pending.hub_nonce, bytes.data(), NonceSize);
        esp_fill_random(pending.knob_nonce, NonceSize);
        uint8_t proof[32];
        hmac("knob", pending.hub_nonce, pending.knob_nonce, proof);
        JsonDocument reply;
        reply["t"] = "auth";
        reply["nonce"] = base64(pending.knob_nonce, NonceSize);
        reply["proof"] = base64(proof, sizeof proof);
        std::string out;
        serializeJson(reply, out);
        pending.peer.write_line(out);
        pending.asked = true;
        return;
    }
    uint8_t expected[32];
    hmac("hub", pending.hub_nonce, pending.knob_nonce, expected);
    if (!unbase64(msg["proof"] | "", bytes) || bytes.size() != sizeof expected || !same(bytes.data(), expected, 32))
        return drop_pending();
    accept_pending();
}

void serve_link() {
    if (served) return;
    MDNS.end();
    MDNS.begin(host.c_str());
    MDNS.addService("knobhub", "tcp", LinkPort);
    server.begin();
    server.setNoDelay(true);
    served = true;
}

void stop_link() {
    drop_pending();
    drop_active();
    if (!served) return;
    server.end();
    MDNS.end();
    served = false;
}

// The knob's id and its names on the network, from its MAC: knob-xxxx (mDNS, how Knob Hub finds it) and its own
// network in pairing mode, KnobHub-XXXX.
void identify() {
    if (!id.empty()) return;
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    id = to_hex(mac, 6);
    std::string tail = to_hex(mac + 4, 2);
    host = "knob-" + tail;
    for (char& c : tail) c = static_cast<char>(toupper(c));
    setup_network = "KnobHub-" + tail;
}

// The name routers list the knob by (its DHCP hostname): its own name, as letters, digits and dashes ("Smart Knob" is
// smart-knob), or knob-xxxx when nothing of the name is left.
std::string network_name;
bool started = false;

std::string name_for_network(const std::string& name) {
    std::string out;
    bool gap = false;
    for (unsigned char c : name) {
        if (c < 128 && isalnum(c)) {
            if (gap && !out.empty()) out += '-';
            gap = false;
            out += static_cast<char>(tolower(c));
        } else {
            gap = true;
        }
        if (out.size() >= 32) break;
    }
    return out.empty() ? host : out;
}

WifiState shown = WifiState::Unsupported;
std::string shown_network;
Join shown_join = Join::Idle;
bool shown_scanning = false;

}  // namespace

void start(int board_tx_power) {
    tx_power = board_tx_power;
    prefs.begin("knob", false);
    size_t n = prefs.getBytesLength("key");
    if (n == KeySize) {
        key.resize(KeySize);
        prefs.getBytes("key", key.data(), KeySize);
    }
    ssid = prefs.getString("ssid", "").c_str();
    password = prefs.getString("password", "").c_str();

    identify();
    WiFi.persistent(false);  // the network is kept above, not by the Wi-Fi driver
    WiFi.setHostname(network_name.empty() ? host.c_str() : network_name.c_str());
    started = true;
    radio_mode(WIFI_STA);
    WiFi.setSleep(false);  // answers in milliseconds instead of a tenth of a second
    WiFi.setAutoReconnect(true);
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) { last_reason = info.wifi_sta_disconnected.reason; },
                 ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { join_got_ip = true; }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    join_saved();
}

void poll() {
    if (setup_on) poll_setup();
    poll_join();
    poll_scan();
    bool joined = WiFi.status() == WL_CONNECTED;
    if (joined) not_joined_since = millis();
    if (joined && !setup_on) serve_link();
    else if (!joined && served) stop_link();

    if (served) {
        if (server.hasClient()) {
            drop_pending();
            pending.peer.reset(server.accept());
            pending.peer.client.setNoDelay(true);
            pending.asked = false;
            pending.since = millis();
            pending_on = true;
        }
        if (pending_on) poll_pending();
        if (active_on) {
            std::string sealed;
            while (active.line(sealed)) {
                std::vector<uint8_t> bytes;
                if (!unbase64(sealed.c_str(), bytes) || bytes.size() < TagSize) {
                    drop_active();
                    break;
                }
                size_t len = bytes.size() - TagSize;
                std::string plain(len, '\0');
                uint8_t iv[12];
                nonce(1, received_count++, iv);
                if (mbedtls_gcm_auth_decrypt(&gcm, len, iv, sizeof iv, nullptr, 0, bytes.data() + len, TagSize,
                                             bytes.data(), reinterpret_cast<uint8_t*>(&plain[0])) != 0) {
                    drop_active();  // not from Knob Hub, or out of order
                    break;
                }
                lines.push_back(std::move(plain));
            }
            if (active_on && (active.too_long || !active.client.connected())) drop_active();
        }
    }

    WifiState now = state();
    std::string network = now == WifiState::Off || now == WifiState::Setup ? "" : ssid;
    bool looking = scan_wanted || scanning;
    if (now != shown || network != shown_network || join != shown_join || looking != shown_scanning) {
        shown = now;
        shown_network = network;
        shown_join = join;
        shown_scanning = looking;
        knob_os_wifi_changed();
    }
}

void pair(const std::string& line) {
    JsonDocument msg;
    std::vector<uint8_t> bytes;
    if (deserializeJson(msg, line) || !unbase64(msg["key"] | "", bytes) || bytes.size() != KeySize) return;
    if (bytes == key) return;
    key = bytes;
    prefs.putBytes("key", key.data(), key.size());
    drop_pending();
    drop_active();  // it proved the old key
    shown = WifiState::Unsupported;  // redraw: Wi-Fi setup is possible now
}

bool linked() { return active_on; }
unsigned generation() { return connections; }

bool next_line(std::string& line) {
    if (lines.empty()) return false;
    line = std::move(lines.front());
    lines.pop_front();
    return true;
}

void send(const char* line) {
    if (!active_on) return;
    size_t len = strlen(line);
    std::vector<uint8_t> sealed(len + TagSize);
    uint8_t iv[12];
    nonce(2, sent_count++, iv);
    mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, len, iv, sizeof iv, nullptr, 0,
                              reinterpret_cast<const uint8_t*>(line), sealed.data(), TagSize, sealed.data() + len);
    active.write_line(base64(sealed.data(), sealed.size()));
}

void drop() {
    drop_pending();
    drop_active();
}

}  // namespace net

KnobIdentity hal_identity() { return {net::id, net::host, net::key_id()}; }

void hal_set_name(const std::string& name) {
    net::identify();
    std::string wanted = net::name_for_network(name);
    if (wanted == net::network_name) return;
    net::network_name = wanted;
    if (!net::started) return;  // start() uses it
    WiFi.STA.setHostname(wanted.c_str());
    // Routers learn the name when the knob joins, so it joins again (Knob Hub over Wi-Fi reconnects by itself).
    if (WiFi.status() == WL_CONNECTED && !net::setup_on && net::join != net::Join::Joining) WiFi.reconnect();
}

WifiStatus hal_wifi() {
    WifiStatus s;
    s.state = net::state();
    if (s.state != WifiState::Off && s.state != WifiState::Setup) s.network = net::join == net::Join::Joining ? net::join_ssid : net::ssid;
    if (s.state == WifiState::Setup) {
        s.setup_network = net::setup_network;
        s.setup_password = net::setup_password;
    }
    if (net::join == net::Join::Joining) s.joining = net::join_ssid;
    if (net::join == net::Join::Failed) {
        s.join_failed = net::join_ssid;
        s.join_problem = net::join_problem();
        s.join_reason = net::fail_reason;
    }
    s.scanning = net::scan_wanted || net::scanning;
    return s;
}

void hal_wifi_scan() { net::scan_wanted = true; }

std::vector<WifiNetwork> hal_wifi_networks() { return net::scanned_networks(); }

void hal_wifi_join(const std::string& ssid, const std::string& password) {
    if (ssid.empty() || ssid.size() > 32 || password.size() > 63) return;
    net::begin_join(ssid, password);
}

bool hal_wifi_setup(std::string& why) {
    if (net::key.empty()) {
        why = "Pair over USB first";
        return false;
    }
    if (!net::setup_on) net::start_setup();
    return true;
}

void hal_wifi_stop_setup() { net::end_setup(); }

void hal_wifi_forget() {
    net::ssid.clear();
    net::password.clear();
    net::prefs.remove("ssid");
    net::prefs.remove("password");
    net::end_setup();
    net::stop_link();
    WiFi.disconnect();
}
