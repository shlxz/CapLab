#include "i18n.h"

namespace i18n {
bool english = false;   // 默认中文；开机由 main 从 NVS 读回

#define CAPLAB_ROW(id, zh, en) zh,
static const char* const kZh[] = {
    CAPLAB_STRINGS(CAPLAB_ROW)
};
#undef CAPLAB_ROW

#define CAPLAB_ROW(id, zh, en) en,
static const char* const kEn[] = {
    CAPLAB_STRINGS(CAPLAB_ROW)
};
#undef CAPLAB_ROW

static_assert(sizeof(kZh) / sizeof(kZh[0]) == S_COUNT, "zh table size mismatch");
static_assert(sizeof(kEn) / sizeof(kEn[0]) == S_COUNT, "en table size mismatch");

const char* t(Str s) {
    const unsigned i = (unsigned)s;
    return i < (unsigned)S_COUNT ? (english ? kEn[i] : kZh[i]) : "";
}
}  // namespace i18n
