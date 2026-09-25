from dataclasses import dataclass

BASIC = " '()+,-.0123456789:"
LATIN = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'
CYRILLIC = 'АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя'
NOTO_CJK = '/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc'


@dataclass(frozen=True)
class Language:
    code: str
    font: str
    quantity: str
    unprefixed: tuple[str, ...]
    level: tuple[str, ...]
    prefixed: tuple[str, ...]
    underlined: tuple[str, ...]
    charset: str | None = None
    extra: str = ''
    font_index: int = 0
    latin_font: str | None = None
    plain_words: tuple[str, ...] = ()
    letters: str | None = None
    tracking: tuple[float, float] = (0.0, 0.0)
    cap: float = 0.72


LANGUAGES = {
    language.code: language
    for language in (
        Language(
            code='en',
            font='Fontin-Regular.otf',
            quantity='prefix',
            charset=BASIC + LATIN,
            unprefixed=('Skill', 'Support', 'Unique', 'Rare Unique'),
            level=(' (Level {n})',),
            prefixed=('Skill: {tail}', 'Support: {tail}', 'Unique {tail}', 'Rare Unique Item', 'Skill Level {n}: {tail}'),
            letters='abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ',
            underlined=('Unique',),
        ),
        Language(
            code='ru',
            font='FontinSans_Cyrillic_46b/FontinSans_Cyrillic_R_46b.otf',
            quantity='suffix',
            charset=BASIC + CYRILLIC,
            unprefixed=('Умение', 'Поддержка', 'Уровень умения', 'Уникальн', 'Редкий уникальный'),
            level=(' (Уровень {n})', ' (уровень {n})'),
            prefixed=(
                'Умение: {tail}',
                'Поддержка: {tail}',
                'Уникальный {tail}',
                'Уникальная {tail}',
                'Уникальное {tail}',
                'Редкий уникальный предмет',
                'Уровень умения {n}: {tail}',
            ),
            letters='абвгдеёжзийклмнопрстуфхцчшщъыьэюяАБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ',
            underlined=('Уникальный', 'Уникальная', 'Уникальное', 'уникальный'),
            tracking=(0.03, 0.13),
        ),
        Language(
            code='de',
            font='Fontin-Regular.otf',
            quantity='prefix',
            charset=BASIC + LATIN + 'ÄÖÜäöüß',
            unprefixed=('Fertigkeit', 'Unterstützung', 'Einzigartig', 'Seltener einzigartiger'),
            level=(' (Stufe {n})',),
            prefixed=(
                'Fertigkeit: {tail}',
                'Unterstützung: {tail}',
                'Einzigartiger {tail}',
                'Einzigartige {tail}',
                'Einzigartiges {tail}',
                'Seltener einzigartiger Gegenstand',
                'Fertigkeitsstufe {n}: {tail}',
            ),
            letters='abcdefghijklmnopqrstuvwxyzäöüßABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÜ',
            underlined=('Einzigartiger', 'Einzigartige', 'Einzigartiges', 'einzigartiger'),
        ),
        Language(
            code='fr',
            font='Fontin-Regular.otf',
            quantity='prefix',
            charset=BASIC + LATIN + 'àâäçéèêëîïôöùûüÿœæÀÂÄÇÉÈÊËÎÏÔÖÙÛÜŸŒÆ',
            unprefixed=('Aptitude', 'Gemme de soutien'),
            plain_words=('Unique',),
            level=(' (Niveau {n})',),
            prefixed=('Aptitude : {tail}', 'Gemme de soutien : {tail}', '{tail} Unique', 'Objet Unique rare'),
            letters='abcdefghijklmnopqrstuvwxyzéèêàâçîïôûùëABCDEFGHIJKLMNOPQRSTUVWXYZÉ',
            underlined=('Unique',),
        ),
        Language(
            code='es',
            font='Fontin-Regular.otf',
            quantity='suffix_x',
            charset=BASIC + LATIN + 'áéíóúüñÁÉÍÓÚÜÑ',
            unprefixed=('Habilidad', 'Asistencia'),
            plain_words=('único', 'única'),
            level=(' (nivel {n})',),
            prefixed=('Habilidad: {tail}', 'Asistencia: {tail}', '{tail} único', '{tail} única', 'Objeto único raro'),
            letters='abcdefghijklmnopqrstuvwxyzáéíóúñABCDEFGHIJKLMNOPQRSTUVWXYZ',
            underlined=('único', 'única'),
        ),
        Language(
            code='pt',
            font='Fontin-Regular.otf',
            quantity='bare',
            charset=BASIC + LATIN + 'áàâãçéêíóôõúüÁÀÂÃÇÉÊÍÓÔÕÚÜ',
            unprefixed=('Habilidade', 'Reforço'),
            plain_words=('Único', 'Única'),
            level=(' (Nível {n})',),
            prefixed=('Habilidade: {tail}', 'Reforço: {tail}', '{tail} Único', '{tail} Única', 'Item Único Raro'),
            letters='abcdefghijklmnopqrstuvwxyzáàâãçéêíóôõúABCDEFGHIJKLMNOPQRSTUVWXYZ',
            underlined=('Único', 'Única'),
        ),
        Language(
            code='ko',
            font=NOTO_CJK,
            font_index=1,
            quantity='prefix',
            extra=BASIC + LATIN + '스킬 레벨 고유 희귀한 아이템 미가공 젬 정신력 무작위 화폐 개',
            unprefixed=('스킬 레벨',),
            plain_words=('고유',),
            level=(' ({n}레벨)',),
            prefixed=('스킬 레벨 {n}: {tail}', '고유 {tail}', '희귀한 고유 아이템'),
            underlined=('고유',),
            tracking=(0.0, 0.05),
            cap=0.88,
        ),
        Language(
            code='ja',
            font=NOTO_CJK,
            quantity='prefix',
            extra=BASIC + LATIN + 'スキルレベル ユニーク 貴重なアイテム 完全 上級 個 ランダムなカレンシー ・ー',
            unprefixed=('スキルレベル',),
            plain_words=('ユニーク',),
            level=(' (レベル{n})', '(レベル{n})'),
            prefixed=('スキルレベル {n}: {tail}', 'ユニーク{tail}', '貴重なユニークアイテム'),
            underlined=('ユニーク',),
            tracking=(0.0, 0.05),
            cap=0.88,
        ),
        Language(
            code='th',
            font='/usr/share/fonts/noto/NotoSansThai-Regular.ttf',
            latin_font='/usr/share/fonts/noto/NotoSans-Regular.ttf',
            quantity='prefix',
            extra=BASIC + LATIN + 'สกิล เสริม ยูนิค ไอเทมยูนิคที่พบได้ยาก เลเวล ไร้ที่ติ ชั้นสูง',
            unprefixed=('สกิล', 'เสริม'),
            plain_words=('ยูนิค',),
            level=(' (เลเวล {n})',),
            prefixed=('สกิล: {tail}', 'เสริม: {tail}', '{tail}ยูนิค', 'ไอเทมยูนิคที่พบได้ยาก'),
            underlined=('ยูนิค',),
            cap=0.8,
        ),
    )
}
