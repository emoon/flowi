#include "localization_private.h"

// The built-in CLDR plural rules, keyed by a language tag's primary subtag.

// Which of the built-in rule sets a language uses. A language none of them names selects other for every count.
typedef enum PluralRules {
    PluralRules_OneOther,
    PluralRules_ZeroOneOther,
    PluralRules_Slavic,
    PluralRules_OtherOnly,
    PluralRules_None,
} PluralRules;

typedef struct PluralRuleLanguages {
    PluralRules rules;
    const char* const* tags;
} PluralRuleLanguages;

static bool tag_is_one_of(FlString tag, const char* const* tags) {
    for (; *tags != nullptr; tags++) {
        if (fl_string_equals_nocase(tag, string_from_cstr(*tags))) {
            return true;
        }
    }
    return false;
}

// Only the tag's primary subtag is matched, so pt-br selects the way pt does. A language none of these lists
// names (Arabic, Polish and Czech among them) has no rules here.

static PluralRules plural_rules_for_tag(FlString tag) {
    static const char* const one_other[] = { "en", "de", "nl", "sv", "da", "no", "nb", "nn", "es",
                                             "it", "ca", "el", "fi", "et", "bg", "hu", "tr", nullptr };
    static const char* const zero_one_other[] = { "fr", "pt", "hi", "fa", nullptr };
    static const char* const slavic[] = { "ru", "uk", "be", "sr", "hr", "bs", nullptr };
    static const char* const other_only[] = { "zh", "ja", "ko", "th", "vi", "id", "ms", nullptr };
    static const PluralRuleLanguages languages[] = {
        { PluralRules_OneOther, one_other },
        { PluralRules_ZeroOneOther, zero_one_other },
        { PluralRules_Slavic, slavic },
        { PluralRules_OtherOnly, other_only },
    };

    const i64 dash = fl_string_find_char(tag, '-');
    const FlString primary = dash < 0 ? tag : fl_string_substr(tag, 0, (u64)dash);

    for (u32 i = 0; i < sizeof_array(languages); i++) {
        if (tag_is_one_of(primary, languages[i].tags)) {
            return languages[i].rules;
        }
    }
    return PluralRules_None;
}

static bool in_range(u64 value, u64 low, u64 high) {
    return value >= low && value <= high;
}

static LocalizationPluralCategory slavic_category(u64 n) {
    const u64 ones = n % 10;
    const u64 tens = n % 100;

    if (ones == 1 && tens != 11) {
        return LocalizationPlural_One;
    }
    if (in_range(ones, 2, 4) && !in_range(tens, 12, 14)) {
        return LocalizationPlural_Few;
    }
    return LocalizationPlural_Many;
}

static LocalizationPluralCategory one_when(bool is_one) {
    return is_one ? LocalizationPlural_One : LocalizationPlural_Other;
}

// CLDR selects on the magnitude of the count.

static u64 count_magnitude(i64 count) {
    return count < 0 ? -(u64)count : (u64)count;
}

static LocalizationPluralCategory plural_category(PluralRules rules, i64 count) {
    const u64 n = count_magnitude(count);

    switch (rules) {
        case PluralRules_OneOther:
            return one_when(n == 1);
        case PluralRules_ZeroOneOther:
            return one_when(n <= 1);
        case PluralRules_Slavic:
            return slavic_category(n);
        case PluralRules_OtherOnly:
        case PluralRules_None:
            break;
    }

    return LocalizationPlural_Other;
}

bool localization_select_plural(FlString tag, i64 count, LocalizationPluralCategory* out) {
    const PluralRules rules = plural_rules_for_tag(tag);
    *out = plural_category(rules, count);
    return rules != PluralRules_None;
}

LocalizationPluralCategory localization_plural_category(FlString tag, i64 count) {
    LocalizationPluralCategory category = LocalizationPlural_Other;
    localization_select_plural(tag, count, &category);
    return category;
}
