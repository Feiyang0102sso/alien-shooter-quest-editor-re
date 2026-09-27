#include "text_encoding.h"
#include "../i18n/i18n.h"
#include <windows.h>
#include <stdexcept>
#include <string_view>

namespace quest {
std::wstring decode(const std::string& bytes, unsigned codepage) {
    if (bytes.empty()) return {};
    DWORD flags = 0;
    if (codepage == CP_UTF8) flags = MB_ERR_INVALID_CHARS;
    const int count = MultiByteToWideChar(codepage, flags, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (!count) throw std::runtime_error(quest::i18n::narrow("errors.decode_failed"));
    std::wstring text(count, L'\0');
    MultiByteToWideChar(codepage, flags, bytes.data(), static_cast<int>(bytes.size()), text.data(), count);
    return text;
}

std::string encode(const std::wstring& text, unsigned codepage) {
    if (text.empty()) return {};
    BOOL substituted = FALSE;
    BOOL* substitution = &substituted;
    DWORD flags = WC_NO_BEST_FIT_CHARS;
    if (codepage == CP_UTF8) {
        substitution = nullptr;
        flags = WC_ERR_INVALID_CHARS;
    }
    const int count = WideCharToMultiByte(codepage, flags, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, substitution);
    if (!count || substituted) throw std::runtime_error(quest::i18n::narrow("errors.text_not_representable"));
    std::string result(count, '\0');
    WideCharToMultiByte(codepage, flags, text.data(), static_cast<int>(text.size()), result.data(), count, nullptr, substitution);
    if (substituted) throw std::runtime_error(quest::i18n::narrow("errors.encoding_loses_characters"));
    return result;
}

std::string encode_edit(const std::string& original, const std::wstring& edited, unsigned codepage) {
    const auto displayed = decode(original,codepage);
    if (edited == displayed) return original;
    // Permissive Windows decoding may substitute characters; require byte-exact roundtrip before re-encoding.
    bool lossless = false;
    try {
        lossless = encode(displayed,codepage) == original;
    } catch (const std::runtime_error&) {
        // Report lossy original text as an encoding-selection error; encode reports errors in new input.
    }
    if (!lossless) throw std::runtime_error(quest::i18n::narrow("errors.lossy_edit"));
    return encode(edited,codepage);
}

namespace {
// Statistical encoding hints, not UI text; CJK presence alone can misclassify CP1251 Russian as GBK.
constexpr std::wstring_view kCommonChinese =
    L"的一是在不了有和人这中大为上个国我以要他时来用们生到作地于出就分对成会可主发年动同工也能下过子说产种面而方后多定行学法所民得经之进着等部度家电力里如水化高自二理起小物现实加量都两体制机当使点从业本去把性好应开它合还因由其些然前外天政四日那社义事平形相全表间样与关各重新线内数正心反你明看原又么利比或但质气第向道命变条只没结解问意建月公无系军很情者最立代想已通并提直题程展五果料象员革位入常文总次品式活设及管特件长求老头基资边流路级少图山统接知较将组见计别她手角期根论运农指几九区强放决西被干做必战先回则任取据处队南给色光门即保治北造百规热领七海口东导器压志世金增争济阶油思术极交受联什认六共权收证改清美再采转更单风切打白教速花带安场身车例真务具万每目至达走积示议声报斗完类八离名确才科张信马节话米整空元况今集温传土许步群广石记需段研界拉林律叫且究观越织装影算低持音众书布复容儿须际商非验连断深难近矿千周委素技备半办青省列习响约支般史感劳便团往酸历市克何除消构府太准精值号率族维划选标写存候亲快效斯院查江型眼王按格养易置派层片始却专状育厂京识适属圆包火住调满县局照参红细引听该铁价严龙飞";

int encoding_score(const std::string& bytes, unsigned codepage) {
    const auto text = decode(bytes,codepage);
    int score = 0;
    // Permissive decoding may use question marks instead of U+FFFD; check invalid byte sequences separately.
    if (!bytes.empty() && !MultiByteToWideChar(codepage,MB_ERR_INVALID_CHARS,
        bytes.data(),static_cast<int>(bytes.size()),nullptr,0)) score -= 10;
    bool previous_lowercase = false;
    for (const wchar_t character : text) {
        const bool lowercase = (character >= L'а' && character <= L'я') || character == L'ё';
        const bool uppercase = (character >= L'А' && character <= L'Я') || character == L'Ё';
        if (lowercase || uppercase) {
            score += 2;
            // GBK misread as CP1251 often produces unexpected case changes within words.
            if (previous_lowercase && uppercase) score -= 3;
        } else if (character >= 0x4E00 && character <= 0x9FFF) {
            score += 2;
            if (kCommonChinese.find(character) != std::wstring_view::npos) score += 3;
        } else if (character == 0xFFFD || (character >= 0xE000 && character <= 0xF8FF)) {
            score -= 10;
        } else if (character >= 0x80) {
            // Other scripts are not positive evidence; ASCII identifiers and Font tags do not affect scoring.
            score -= 2;
        }
        previous_lowercase = lowercase;
    }
    return score;
}
}

unsigned detect_game_encoding(const std::string& bytes, unsigned fallback) {
    const int russian_score = encoding_score(bytes,1251);
    const int chinese_score = encoding_score(bytes,936);
    constexpr int minimum_margin = 4;
    if (chinese_score >= russian_score+minimum_margin) return 936;
    if (russian_score >= chinese_score+minimum_margin) return 1251;
    return fallback;
}


unsigned default_game_codepage() {
    const unsigned codepage = GetACP();
    if (codepage == CP_UTF8) return 1252;
    return codepage;
}
GameEncoding inspect_game_encoding(const std::string& bytes) {
    if (bytes.find('\0') != std::string::npos)
        throw std::runtime_error(i18n::narrow("errors.unsupported_configuration"));
    if (bytes.starts_with("\xEF\xBB\xBF"))
        throw std::runtime_error(i18n::narrow("errors.unsupported_game_utf8"));
    const auto detected = detect_game_encoding(bytes, 0);
    if (detected) return {detected, false};
    return {default_game_codepage(), true};
}
}
