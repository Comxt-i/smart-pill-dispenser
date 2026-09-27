#pragma once
#include <stddef.h>
#include <string.h>
#include <ctype.h>

inline bool validSetupWifi(const char *ssid, const char *password)
{
  const size_t ssidLength = strlen(ssid), passwordLength = strlen(password);
  return ssidLength > 0 && ssidLength <= 32 &&
         (passwordLength == 0 || (passwordLength >= 8 && passwordLength <= 63));
}

inline bool normalizeSetupToken(const char *input, char out[21])
{
  size_t count = 0;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(input); *p; ++p)
  {
    if (isspace(*p) || *p == '-') continue;
    if (*p > 127 || !isalnum(*p) || count >= 20) { out[0] = '\0'; return false; }
    out[count++] = static_cast<char>(toupper(*p));
  }
  out[count] = '\0';
  if (count == 20) {
    for (size_t i = 0; i < count; ++i) if (!isxdigit(static_cast<unsigned char>(out[i]))) { out[0] = '\0'; return false; }
    return true; // Existing tickets remain usable during a rolling deployment.
  }
  if (count == 8) {
    for (size_t i = 0; i < count; ++i)
      if (!strchr("23456789ABCDEFGHJKMNPQRSTUVWXYZ", out[i])) { out[0] = '\0'; return false; }
    return true;
  }
  out[0] = '\0'; return false;
}
