#include "i18n.h"

namespace wr {

static Lang g_lang = Lang::En;

void setLang(Lang l) { g_lang = l; }
Lang lang() { return g_lang; }

Lang langFromCode(const std::string& code) { return code == "fr" ? Lang::Fr : Lang::En; }
const char* langCode(Lang l) { return l == Lang::Fr ? "fr" : "en"; }

}  // namespace wr
