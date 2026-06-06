#include "identity.h"
#include <Preferences.h>
#include <ctype.h>

static Preferences p;
static String g_name, g_host;
static bool   rebootReq = false;

// lowercase; keep [a-z0-9-]; map space/underscore to '-'; cap length.
static String sanitizeHost(const String& in) {
  String o;
  for (size_t i = 0; i < in.length(); i++) {
    char c = tolower(in[i]);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') o += c;
    else if (c == ' ' || c == '_') o += '-';
  }
  if (o.length() == 0) o = "hydro";
  if (o.length() > 31) o = o.substring(0, 31);
  return o;
}

void identity_begin() {
  p.begin("ident", false);
  g_name = p.getString("name", "");
  g_host = p.getString("host", "");
  if (g_name.length() == 0 || g_host.length() == 0) {
    uint64_t mac = ESP.getEfuseMac();
    char suf[7];
    // Use the device-unique HIGH 3 bytes. The low 3 bytes are the Espressif OUI
    // (shared across a production batch) and collide between units.
    snprintf(suf, sizeof(suf), "%06x", (unsigned)((mac >> 24) & 0xFFFFFF));
    if (g_host.length() == 0) g_host = String("hydro-") + suf;
    if (g_name.length() == 0) g_name = String("Hydro-") + suf;
  }
}

const char* identity_name() { return g_name.c_str(); }
const char* identity_host() { return g_host.c_str(); }

void identity_set(const String& name, const String& host) {
  if (name.length()) { g_name = name.length() > 31 ? name.substring(0, 31) : name; p.putString("name", g_name); }
  if (host.length()) { g_host = sanitizeHost(host);                                p.putString("host", g_host); }
  rebootReq = true;   // hostname/mDNS change takes effect on reboot
}

bool identity_consume_reboot() { if (rebootReq) { rebootReq = false; return true; } return false; }
