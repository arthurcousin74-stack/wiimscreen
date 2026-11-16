#pragma once

#include <string>

// Interface language (setup page › Language). English by default; French is the original language of the app.
//
// Every text shown to the user (screen, messages, setup and update pages) is written in place as
// wr::L("English", "Français"): both versions stay next to each other in the code, with the same printf
// specifiers in the same order. The Journal (logEvent) stays in French: the hardware checklist quotes its lines.
namespace wr {

enum class Lang { En = 0, Fr = 1 };

void setLang(Lang l);
Lang lang();
inline const char* L(const char* en, const char* fr) { return lang() == Lang::Fr ? fr : en; }

// "en" / "fr" (setting stored in NVS, setup page); anything else → English
Lang langFromCode(const std::string& code);
const char* langCode(Lang l);

}  // namespace wr
