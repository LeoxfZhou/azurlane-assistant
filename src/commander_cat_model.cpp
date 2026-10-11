#include "commander_cat_model.hpp"

std::string toText(TalentKind value) {
    switch (value) {
        case TalentKind::Normal: return "普通";
        case TalentKind::Special: return "特殊";
        case TalentKind::Fixed: return "固定";
        case TalentKind::Placeholder: return "占位";
    }
    return "未知";
}

std::string toText(CatSlot value) {
    switch (value) {
        case CatSlot::Commander: return "司令";
        case CatSlot::Staff: return "参谋";
        case CatSlot::Either: return "司令/参谋";
    }
    return "未知";
}

std::string toText(CatDifficulty value) {
    switch (value) {
        case CatDifficulty::Easy: return "易";
        case CatDifficulty::Medium: return "中";
        case CatDifficulty::Hard: return "难";
        case CatDifficulty::VeryHard: return "极难";
        case CatDifficulty::Unreachable: return "不可达";
    }
    return "未知";
}
