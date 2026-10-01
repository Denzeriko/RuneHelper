#include "items/ItemText.h"

#include "common/Text.h"

namespace
{
constexpr ItemLanguage kLanguages[] = {
    { "en",
      "Item Class",
      "Waystones",
      "Tablet",
      "Rarity",
      "Item Level",
      "Corrupted",
      "Twice Corrupted",
      "Map Device",
      "remaining",
      "Tablets" },
    { "ru",
      "Класс предмета",
      "Путевые камни",
      "Плитки",
      "Редкость",
      "Уровень предмета",
      "Осквернено",
      "Дважды осквернено",
      "Машине картоходца",
      "Осталось зарядов" },
    { "de",
      "Gegenstandsklasse",
      "Wegsteine",
      "Tafel",
      "Seltenheit",
      "Gegenstandsstufe",
      "Verderbt",
      "Doppelt verderbt",
      "Kartenapparat",
      "übrig" },
    { "fr",
      "Classe d'objet",
      "Pierres de téléportation",
      "Tablette",
      "Rareté",
      "Niveau de l'objet",
      "Corrompu",
      "Double Corruption",
      "Dispositif cartographique",
      "restante" },
    { "es",
      "Clase de objeto",
      "Piedras guía",
      "Tablilla",
      "Rareza",
      "Nivel de objeto",
      "Corrupto",
      "Doblemente corrupto",
      "artefacto de mapas",
      "restante" },
    { "pt",
      "Classe do Item",
      "Pedras-guia",
      "Tábua",
      "Raridade",
      "Nível do Item",
      "Corrompido",
      "Corrompido Duas Vezes",
      "Dispositivo de Mapas",
      "Resta" },
    { "ko", "아이템 종류", "경로석", "서판", "아이템 희귀도", "아이템 레벨", "타락", "두 번 타락", "지도 장치", "잔여 사용 횟수" },
    { "ja",
      "アイテムクラス",
      "ウェイストーン",
      "石板",
      "レアリティ",
      "アイテムレベル",
      "コラプト状態",
      "ダブルコラプト状態",
      "マップデバイス",
      "残り使用可能回数" },
    { "th", "ชนิดไอเทม", "ศิลานำทาง", "แผ่นหิน", "ความหายาก", "เลเวลไอเทม", "มีมลทิน", "มีมลทินสองครั้ง", "เครื่องเปิดแผนที่", "เหลือการใช้งานอีก" },
};

std::string NormalizeLine(std::string_view text)
{
    std::string result;

    for (std::size_t i = 0; i < text.size();)
    {
        char32_t codepoint = DecodeUtf8(text, i);

        if (codepoint == 0xA0 || codepoint == 0x202F || codepoint == 0x3000)
            codepoint = ' ';
        else if (codepoint == 0xFF1A)
            codepoint = ':';

        AppendUtf8(result, codepoint);
    }

    return std::string(Trim(result));
}

}

std::string_view ItemPropertyValue(std::string_view line, std::string_view label)
{
    const auto colon = line.find(':');

    if (colon == std::string_view::npos || Trim(line.substr(0, colon)) != label)
        return {};

    return Trim(line.substr(colon + 1));
}

std::optional<ItemText> ParseItemText(std::string_view text)
{
    if (text.empty() || text.size() > 65536)
        return std::nullopt;

    while (text.ends_with('\0'))
        text.remove_suffix(1);

    if (text.find('\0') != std::string_view::npos)
        return std::nullopt;

    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);

    ItemText item;
    bool header = true;

    while (!text.empty())
    {
        const auto end = text.find('\n');
        std::string line = NormalizeLine(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);

        if (line.empty())
            continue;

        if (line.size() > 2048)
            return std::nullopt;

        if (!item.language)
        {
            for (const auto& language : kLanguages)
            {
                const auto value = ItemPropertyValue(line, language.itemClass);

                if (!value.empty())
                {
                    item.language = &language;
                    item.itemClass = value;
                    break;
                }
            }

            if (!item.language)
                return std::nullopt;

            continue;
        }

        if (line == "--------")
        {
            header = false;

            if (item.sections.empty() || !item.sections.back().empty())
                item.sections.emplace_back();

            continue;
        }

        if (header)
        {
            const auto rarity = ItemPropertyValue(line, item.language->rarity);

            if (!rarity.empty())
                item.rarity = rarity;
            else if (item.name.empty())
                item.name = line;
            else
                item.base = line;
        }
        else
        {
            if (line == item.language->corrupted || line == item.language->twiceCorrupted)
                item.corrupted = true;

            item.sections.back().push_back(std::move(line));
        }
    }

    if (!item.language || header || item.name.empty() || item.rarity.empty())
        return std::nullopt;

    if (!item.sections.empty() && item.sections.back().empty())
        item.sections.pop_back();

    return item;
}
