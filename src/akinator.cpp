// akinator.cpp — Модуль игры «Акинатор» для Kotogram / KoteLoader (C++ ABI 3)
// Гибридный движок: Байесовская база знаний + AI-дедукция + поддержка прокси (by kote).

#include "module_abi.h"
#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace {
using json = nlohmann::json;

constexpr const char* VERSION = "1.4.0";

// ── Утилиты строк и URL ──────────────────────────────────────────────────────
std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::uppercase << ((int)(unsigned char)c) << std::nouppercase;
        }
    }
    return escaped.str();
}

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

std::string html_unescape(const std::string& str) {
    std::string res = str;
    static const std::vector<std::pair<std::string, std::string>> reps = {
        {"&quot;", "\""}, {"&amp;", "&"}, {"&apos;", "'"}, {"&#39;", "'"},
        {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}
    };
    for (const auto& [from, to] : reps) {
        size_t pos = 0;
        while ((pos = res.find(from, pos)) != std::string::npos) {
            res.replace(pos, from.length(), to);
            pos += to.length();
        }
    }
    return res;
}

inline long long normalize_cid(long long cid) {
    if (cid > 1000000000LL && cid < 1000000000000LL) {
        return -1000000000000LL - cid;
    }
    return cid;
}

inline long long bare_cid(long long cid) {
    if (cid < -1000000000000LL) {
        return -cid - 1000000000000LL;
    }
    return cid;
}

// ── Гибридная база знаний Акинатора (36 признаков) ───────────────────────────
// Признаки (0..35):
//  0: Реальный человек (живой или исторический)
//  1: Мужского пола
//  2: Женского пола
//  3: Из аниме или манги
//  4: Из видеоигры
//  5: Из фильма или сериала с живыми актёрами
//  6: Из мультфильма / анимации (не аниме)
//  7: Связан с интернетом, YouTube, Twitch или мемами
//  8: Говорит по-русски или из России / СНГ / СССР
//  9: Главный герой своего произведения
// 10: Злодей / антагонист / антигерой
// 11: Обладает сверхспособностями, магией или суперсилой
// 12: Использует холодное оружие (меч, клинок, кунаи, топор)
// 13: Использует огнестрельное оружие или бластеры
// 14: Носит маску, шлем или скрывает лицо
// 15: Человек по виду / расе
// 16: Животное, монстр или антропоморфное существо
// 17: Робот, киборг или искусственный интеллект
// 18: Светлые (жёлтые, белые, пепельные) волосы
// 19: Тёмные (чёрные, каштановые) волосы
// 20: Яркие/необычные волосы (рыжие, розовые, синие, зелёные, красные)
// 21: Лысый или совсем без волос/шерсти на голове
// 22: Носит очки, повязку на глазах или необычные глаза (шаринган и т.д.)
// 23: Подросток или ребёнок (выглядит моложе 20 лет)
// 24: Связан с музыкой (певец, рэпер, рок-музыкант)
// 25: Связан с историей, политикой, наукой или литературой
// 26: Связан со спортом, футболом или единоборствами
// 27: Связан с IT, технологиями или крупным бизнесом
// 28: Стример, видеоблогер или контент-мейкер
// 29: Супергерой или персонаж комиксов Marvel / DC
// 30: Связан с шутерами, экшенами или RPG-играми
// 31: Связан с Minecraft, Dota 2, CS, Undertale, FNAF или Brawl Stars
// 32: Уже умер (в реальности или погиб по сюжету)
// 33: Носит плащ, мантию, накидку или длинное пальто
// 34: Связан с криминалом, мафией, полицией или расследованиями
// 35: Умеет летать или телепортироваться

constexpr int NUM_TRAITS = 36;

struct QuestionDef {
    const char* ru;
    const char* en;
};

static const QuestionDef QUESTIONS[NUM_TRAITS] = {
    {"Ваш персонаж — реальный человек (живой или исторический)?", "Is your character a real person (living or historical)?"},
    {"Ваш персонаж мужского пола?", "Is your character male?"},
    {"Ваш персонаж женского пола?", "Is your character female?"},
    {"Ваш персонаж из аниме или манги?", "Is your character from anime or manga?"},
    {"Ваш персонаж из видеоигры?", "Is your character from a video game?"},
    {"Ваш персонаж из фильма или сериала с живыми актёрами?", "Is your character from a live-action movie or TV series?"},
    {"Ваш персонаж из мультфильма или анимационного фильма (не аниме)?", "Is your character from a cartoon or animated movie (non-anime)?"},
    {"Ваш персонаж прославился благодаря интернету, YouTube, Twitch или мемам?", "Is your character famous from the internet, YouTube, Twitch, or memes?"},
    {"Ваш персонаж из России / СНГ или говорит по-русски?", "Is your character Russian / from the CIS or Russian-speaking?"},
    {"Ваш персонаж — главный герой своего произведения?", "Is your character the main protagonist of their story?"},
    {"Ваш персонаж — злодей, антагонист или опасный преступник?", "Is your character a villain, antagonist, or criminal?"},
    {"Ваш персонаж обладает магией, чакрой или сверхспособностями?", "Does your character have magic or superpowers?"},
    {"Ваш персонаж сражается холодным оружием (меч, катана, топор, кунаи)?", "Does your character fight with melee weapons (sword, blade, axe)?"},
    {"Ваш персонаж использует огнестрельное оружие (пистолеты, автоматы, бластеры)?", "Does your character use firearms or blasters?"},
    {"Ваш персонаж носит маску, шлем или скрывает своё лицо?", "Does your character wear a mask, helmet, or hide their face?"},
    {"Ваш персонаж является человеком по своей расе/природе?", "Is your character biologically human?"},
    {"Ваш персонаж — животное, монстр или фантастическое существо?", "Is your character an animal, monster, or creature?"},
    {"Ваш персонаж — робот, киборг, аниматроник или ИИ?", "Is your character a robot, cyborg, animatronic, or AI?"},
    {"У вашего персонажа светлые (жёлтые, белые или серебристые) волосы?", "Does your character have blonde, white, or silver hair?"},
    {"У вашего персонажа тёмные (чёрные или каштановые) волосы?", "Does your character have dark (black or brown) hair?"},
    {"У вашего персонажа необычный цвет волос (рыжие, розовые, синие, зелёные, красные)?", "Does your character have bright/unusual hair (red, pink, blue, green)?"},
    {"Ваш персонаж лысый (или вообще без волос)?", "Is your character bald or hairless?"},
    {"Ваш персонаж носит очки, повязку на глазах или имеет особые глаза?", "Does your character wear glasses, a blindfold, or have special eyes?"},
    {"Ваш персонаж — ребёнок или подросток (моложе 20 лет)?", "Is your character a child or teenager (under 20)?"},
    {"Ваш персонаж профессионально связан с музыкой (певец, рэпер, музыкант)?", "Is your character a singer, rapper, or musician?"},
    {"Ваш персонаж связан с историей, политикой, наукой или литературой?", "Is your character a historical figure, politician, scientist, or writer?"},
    {"Ваш персонаж связан со спортом, футболом или боевыми искусствами?", "Is your character an athlete, footballer, or martial artist?"},
    {"Ваш персонаж — миллиардер, бизнесмен или создатель IT-компании?", "Is your character a billionaire, entrepreneur, or tech founder?"},
    {"Ваш персонаж — популярный стример, ютубер или блогер?", "Is your character a popular streamer, YouTuber, or blogger?"},
    {"Ваш персонаж — супергерой (или из вселенных Marvel / DC)?", "Is your character a superhero (or from Marvel / DC)?"},
    {"Ваш персонаж из игры жанра экшен, шутер или RPG?", "Is your character from an action, shooter, or RPG game?"},
    {"Ваш персонаж связан с Minecraft, Dota 2, CS, Undertale или FNAF?", "Is your character from Minecraft, Dota 2, CS, Undertale, or FNAF?"},
    {"Ваш персонаж уже умер (в реальной жизни или погиб по сюжету)?", "Has your character died (in real life or in the plot)?"},
    {"Ваш персонаж носит плащ, мантию, накидку или длинное пальто?", "Does your character wear a cape, cloak, robe, or long coat?"},
    {"Ваш персонаж связан с мафией, криминалом, полицией или детективами?", "Is your character involved with crime, mafia, police, or detective work?"},
    {"Ваш персонаж умеет летать или телепортироваться?", "Can your character fly or teleport?"}
};

struct CharacterDef {
    const char* name_ru;
    const char* desc_ru;
    const char* name_en;
    const char* desc_en;
    // 36 символов: '1' = Да, '0' = Нет, '?' = Неопределённо/Неважно
    // Индексы:        012345678901234567890123456789012345
    const char* traits;
};

static const CharacterDef CHARACTERS[] = {
    // ── Аниме и манга ────────────────────────────────────────────────────────
    {"Наруто Узумаки", "Главный герой аниме «Наруто», шиноби Конохи и джинчурики Девятихвостого", "Naruto Uzumaki", "Main protagonist of Naruto, shinobi of Konoha",
     "010100000101100100100001000000000100"},
    {"Саске Учиха", "Шиноби клана Учиха с Шаринганом из аниме «Наруто»", "Sasuke Uchiha", "Uchiha clan shinobi from Naruto",
     "010100000111100100010011000000000101"},
    {"Итачи Учиха", "Старший брат Саске, член организации Акацуки («Наруто»)", "Itachi Uchiha", "Sasuke's older brother, Akatsuki member (Naruto)",
     "010100000011100100010010000000001110"},
    {"Какаши Хатаке", "Копирующий ниндзя в маске, наставник Команды №7 («Наруто»)", "Kakashi Hatake", "Copy Ninja of Konoha (Naruto)",
     "010100000001101100100010000000000000"},
    {"Мадара Учиха", "Легендарный лидер клана Учиха («Наруто»)", "Madara Uchiha", "Legendary leader of the Uchiha clan (Naruto)",
     "010100000011100100010010000000001101"},
    {"Годжо Сатору", "Сильнейший маг в повязке на глазах из аниме «Магическая битва» (Jujutsu Kaisen)", "Satoru Gojo", "The strongest sorcerer from Jujutsu Kaisen",
     "010100000101001100100010000000001001"},
    {"Сукуна (Рёмен Сукуна)", "Король проклятий из аниме «Магическая битва»", "Ryomen Sukuna", "King of Curses from Jujutsu Kaisen",
     "010100000011000010001010000000001000"},
    {"Юдзи Итадори", "Главный герой аниме «Магическая битва», сосуд Сукуны", "Yuji Itadori", "Main protagonist of Jujutsu Kaisen",
     "010100000101000100001001000000000000"},
    {"Монки Д. Луффи", "Капитан Пиратов Соломенной Шляпы из аниме «Ван-Пис» (One Piece)", "Monkey D. Luffy", "Captain of the Straw Hat Pirates (One Piece)",
     "010100000101000100010001000000000100"},
    {"Ророноа Зоро", "Мечник с тремя катанами и зелёными волосами («Ван-Пис»)", "Roronoa Zoro", "Three-sword style swordsman from One Piece",
     "010100000101100100001010000000000100"},
    {"Эрен Йегер", "Главный герой аниме «Атака титанов» (Attack on Titan)", "Eren Yeager", "Main character of Attack on Titan",
     "010100000111100110010001000000001100"},
    {"Леви Аккерман", "Капитан Разведкорпуса, сильнейший боец человечества («Атака титанов»)", "Levi Ackerman", "Captain of the Survey Corps (Attack on Titan)",
     "010100000101100100010000000000000101"},
    {"Микаса Аккерман", "Приёмная сестра Эрена и элитный боец Разведкорпуса («Атака титанов»)", "Mikasa Ackerman", "Adoptive sister of Eren in Attack on Titan",
     "001100000101100100010001000000000101"},
    {"Лайт Ягами (Кира)", "Главный герой аниме «Тетрадь смерти» (Death Note), владелец тетради", "Light Yagami (Kira)", "Main character of Death Note",
     "010100000111000100010001000000001010"},
    {"Эл Лоулайт (L)", "Гениальный детектив-сладкоежка из аниме «Тетрадь смерти»", "L Lawliet", "World's greatest detective from Death Note",
     "010100000100000100010000000000001010"},
    {"Рюк", "Бог смерти (Синигами), обожающий яблоки («Тетрадь смерти»)", "Ryuk", "Shinigami who loves apples from Death Note",
     "010100000001000010010000000000000001"},
    {"Сайтама", "Лысый герой, побеждающий любого врага одним ударом («Ванпанчмен»)", "Saitama", "Hero for fun from One-Punch Man",
     "010100000101000100000100000001000100"},
    {"Генос", "Киборг-ученик Сайтамы из аниме «Ванпанчмен»", "Genos", "Saitama's cyborg disciple (One-Punch Man)",
     "010100000101010001100001000000000001"},
    {"Канеки Кен", "Главный герой аниме «Токийский гуль» (Tokyo Ghoul)", "Ken Kaneki", "Main protagonist of Tokyo Ghoul",
     "010100000101001110110011000000000000"},
    {"Танджиро Камадо", "Охотник на демонов и главный герой «Клинка, рассекающего демонов»", "Tanjiro Kamado", "Main protagonist of Demon Slayer",
     "010100000101100100011001000000000100"},
    {"Незуко Камадо", "Младшая сестра Танджиро, обращённая в демона («Клинок, рассекающий демонов»)", "Nezuko Kamado", "Tanjiro's demon sister from Demon Slayer",
     "001100000101001010010001000000000100"},
    {"Денджи (Человек-бензопила)", "Главный герой аниме и манги «Человек-бензопила» (Chainsaw Man)", "Denji", "Main protagonist of Chainsaw Man",
     "010100000101101110100001000000000000"},
    {"Макима", "Демон Завоевания и глава Бюро общественной безопасности («Человек-бензопила»)", "Makima", "Control Devil from Chainsaw Man",
     "001100000011000010001010000000001110"},
    {"Пауэр", "Одержимая Кровью с рожками из аниме «Человек-бензопила»", "Power", "Blood Fiend from Chainsaw Man",
     "001100000101100010101001000000001000"},
    {"Сон Гоку", "Главный герой аниме «Жемчуг дракона» (Dragon Ball), сайян", "Son Goku", "Main protagonist of Dragon Ball",
     "010100000101000010110000001000000001"},
    {"Джотаро Куджо", "Обладатель стенда Star Platinum из аниме «Невероятные приключения ДжоДжо»", "Jotaro Kujo", "JoJo's Bizarre Adventure Part 3 protagonist",
     "010100000101000100010001000000001100"},
    {"Дио Брандо (DIO)", "Главный антагонист и вампир со стендом The World («ДжоДжо»)", "Dio Brando", "Main villain and vampire from JoJo's Bizarre Adventure",
     "010100000011000010100000000000001101"},
    {"Ичиго Куросаки", "Временный синигами с рыжими волосами из аниме «Блич» (Bleach)", "Ichigo Kurosaki", "Main protagonist of Bleach",
     "010100000101101100001001000000000101"},
    {"Соскэ Айзен", "Бывший капитан Готей 13 и главный антагонист «Блич»", "Sosuke Aizen", "Main antagonist of Bleach",
     "010100000011100010010010000000000101"},
    {"Гатс", "Чёрный мечник с огромным мечом из аниме и манги «Берсерк»", "Guts", "The Black Swordsman from Berserk",
     "010100000100100100010010000000000100"},
    {"Эдвард Элрик", "Стальной алхимик из аниме «Fullmetal Alchemist»", "Edward Elric", "The Fullmetal Alchemist",
     "010100000101100100100001000000000100"},
    {"Рем", "Синеволосая горничная-демон из аниме «Re:Zero»", "Rem", "Blue-haired demon maid from Re:Zero",
     "001100000101100010001001000000000000"},
    {"Аска Лэнгли Сорью", "Пилот Евангелиона-02 с рыжими волосами из аниме «Евангелион»", "Asuka Langley Soryu", "Eva Unit-02 pilot from Neon Genesis Evangelion",
     "001100000100000100001001000000000000"},
    {"Синдзи Икари", "Пилот Евангелиона-01 и главный герой «Евангелиона»", "Shinji Ikari", "Eva Unit-01 pilot from Neon Genesis Evangelion",
     "010100000100000100010001000000000000"},
    {"Пикачу", "Электрический покемон и маскот франшизы «Покемон»", "Pikachu", "Yellow electric Pokemon and series mascot",
     "010110000101000010100000000000000000"},
    {"Фрирен", "Эльфийская волшебница из аниме «Провожающая в последний путь Фрирен»", "Frieren", "Elven mage from Frieren: Beyond Journey's End",
     "001100000101100010100000000000000101"},
    {"Аня Форджер", "Девочка-телепат с розовыми волосами из аниме «Семья шпиона» (Spy x Family)", "Anya Forger", "Telepathic girl from Spy x Family",
     "001100000101000100001001000000000000"},
    {"Хацуне Мику", "Виртуальная певица (вокалоид) с бирюзовыми хвостиками", "Hatsune Miku", "Virtual pop star and Vocaloid with turquoise twin-tails",
     "001110010100000001001001100000000000"},

    // ── Видеоигры ────────────────────────────────────────────────────────────
    {"Стив (Minecraft)", "Главный персонаж песочницы Minecraft в голубой футболке", "Steve (Minecraft)", "Default player character from Minecraft",
     "010010010100100100010000000000110000"},
    {"Крипер (Creeper)", "Зелёный взрывающийся моб из игры Minecraft", "Creeper", "Exploding green mob from Minecraft",
     "000010010010000010000100000000011000"},
    {"Херобрин (Herobrine)", "Мифический персонаж Minecraft со светящимися белыми глазами", "Herobrine", "Mythical Minecraft entity with glowing white eyes",
     "010010010011000010010010000000010001"},
    {"Санс (Sans)", "Скелет в синей куртке из игры Undertale, любящий каламбуры", "Sans", "Pun-loving skeleton from Undertale",
     "010010010001000010000110000000110001"},
    {"Фредди Фазбер (Freddy Fazbear)", "Аниматроник-медведь в цилиндре из игры Five Nights at Freddy's (FNAF)", "Freddy Fazbear", "Animatronic bear from Five Nights at Freddy's",
     "010011010110000011010000100000011000"},
    {"Уильям Афтон (Спрингтрап)", "Главный антагонист серии Five Nights at Freddy's в костюме аниматроника", "William Afton (Springtrap)", "Main antagonist of Five Nights at Freddy's",
     "010011010011101111010000000000011010"},
    {"Пудж (Pudge)", "Мясник с хуком (Meat Hook) — самый популярный герой Dota 2", "Pudge", "The Butcher with Meat Hook from Dota 2",
     "010010010011100010001000000000110000"},
    {"Инвокер (Invoker)", "Могущественный маг с 10 заклинаниями из игры Dota 2", "Invoker", "Arsenal Magus from Dota 2",
     "010010000001000010100010000000110100"},
    {"Шэдоу Фин (Shadow Fiend / Nevermore)", "Демон душ с койлами и реквиемом из игры Dota 2", "Shadow Fiend (Nevermore)", "Demon of souls from Dota 2",
     "010010010011000010010100000000110001"},
    {"Геральт из Ривии", "Беловолосый охотник на чудовищ из книг и игр «Ведьмак» (The Witcher)", "Geralt of Rivia", "White-haired monster hunter from The Witcher",
     "010011000101100100100010000000100100"},
    {"Цири (Цирилла)", "Дитя Старшей Крови с пепельными волосами из «Ведьмака»", "Ciri", "Lady of Space and Time from The Witcher",
     "001011000101100100100001000000100101"},
    {"Кратос", "Спартанский полубог и Бог войны из серии игр God of War", "Kratos", "Ghost of Sparta and God of War",
     "010010000101100010000100000000100000"},
    {"Артур Морган", "Ковбой и член банды Датча из игры Red Dead Redemption 2", "Arthur Morgan", "Main protagonist of Red Dead Redemption 2",
     "010010000110010100010000000000101010"},
    {"Карл Джонсон (CJ / СиДжей)", "Главный герой игры GTA: San Andreas с Гроув-стрит", "Carl Johnson (CJ)", "Main protagonist of GTA: San Andreas",
     "010010010110010100010000000000100010"},
    {"Тревор Филипс", "Безумный грабитель и один из трёх главных героев GTA V", "Trevor Philips", "Protagonist of Grand Theft Auto V",
     "010010000110010100010000000000100010"},
    {"Томми Версетти", "Главный герой игры GTA: Vice City в гавайской рубашке", "Tommy Vercetti", "Main protagonist of GTA: Vice City",
     "010010000110010100010000000000100010"},
    {"Леон С. Кеннеди", "Агент и полицейский из серии игр Resident Evil", "Leon S. Kennedy", "Main protagonist from Resident Evil",
     "010010000100010100100000000000100010"},
    {"Данте", "Охотник на демонов в красном плаще из серии Devil May Cry", "Dante", "Demon hunter from Devil May Cry",
     "010010000101110110100000000000100100"},
    {"Вергилий", "Брат-близнец Данте с катаной Ямато из Devil May Cry", "Vergil", "Dante's twin brother with Yamato from Devil May Cry",
     "010010010011100110100000000000100101"},
    {"Гордон Фримен", "Физик-теоретик в очках и с монтировкой из игры Half-Life", "Gordon Freeman", "Theoretical physicist with a crowbar from Half-Life",
     "010010000100110100010010010000100000"},
    {"Думгай (Палач Рока / Doom Slayer)", "Истребитель демонов в зелёной броне и шлеме из серии игр DOOM", "Doom Slayer (Doomguy)", "Demon-slaying marine from DOOM",
     "010010000101111100010000000000100000"},
    {"Мастер Чиф (Master Chief)", "Спартанец Джон-117 в зелёном шлеме из серии игр Halo", "Master Chief", "Spartan John-117 from Halo",
     "010010000100011100000000000000100000"},
    {"Саймон «Гоуст» Райли (Ghost)", "Боец спецназа в балаклаве с черепом из Call of Duty: Modern Warfare", "Simon 'Ghost' Riley", "Masked operative from Call of Duty: Modern Warfare",
     "010010010100011100100000000000101000"},
    {"Марио (Super Mario)", "Водопроводчик в красной кепке с усами — главный герой игр Nintendo", "Super Mario", "Iconic Nintendo plumber",
     "010011100101000100010000000000000000"},
    {"Ёжик Соник (Sonic)", "Сверхзвуковой синий ёж из серии игр SEGA", "Sonic the Hedgehog", "Blue supersonic hedgehog from SEGA",
     "010011110101000010001001000000000000"},
    {"Скорпион (Хандзо Хасаши)", "Жёлтый ниндзя с гарпуном «Get over here!» из игры Mortal Kombat", "Scorpion", "Yellow ninja from Mortal Kombat",
     "010011000101101100010010001000101001"},
    {"Саб-Зиро (Sub-Zero)", "Синий криомант-ниндзя, повелитель льда из игры Mortal Kombat", "Sub-Zero", "Blue cryomancer ninja from Mortal Kombat",
     "010011000101101100010000001000100000"},
    {"Довакин (Драконорождённый)", "Главный герой ролевой игры The Elder Scrolls V: Skyrim в рогатом шлеме", "Dragonborn (Dovahkiin)", "Main hero of The Elder Scrolls V: Skyrim",
     "010010000101101100100000000000100000"},
    {"Эцио Аудиторе да Фиренце", "Мастер-ассасин эпохи Возрождения в капюшоне из Assassin's Creed", "Ezio Auditore", "Master Assassin from Assassin's Creed",
     "010010000100101100010000000000101100"},
    {"V1 (Ultrakill)", "Кровожадный синий боевой робот из шутера ULTRAKILL", "V1 (ULTRAKILL)", "Blood-fueled machine from ULTRAKILL",
     "000010010100011001000100000000100000"},
    {"Лара Крофт", "Девушка-археолог и расхитительница гробниц из серии Tomb Raider", "Lara Croft", "Archaeologist adventurer from Tomb Raider",
     "001011000100010100010000000000100000"},

    // ── Фильмы, сериалы и супергерои ─────────────────────────────────────────
    {"Дарт Вейдер (Энакин Скайуокер)", "Тёмный лорд ситхов в чёрном шлеме и плаще из «Звёздных войн»", "Darth Vader", "Sith Lord from Star Wars",
     "010011000111101101000100000000101100"},
    {"Гарри Поттер", "Мальчик-волшебник в очках и со шрамом в виде молнии", "Harry Potter", "The Boy Who Lived from Harry Potter",
     "010011000101000100010011000000000101"},
    {"Волан-де-Морт (Том Реддл)", "Тёмный Лорд без носа и главный злодей «Гарри Поттера»", "Lord Voldemort", "Dark Lord and main villain of Harry Potter",
     "010001000011000100000100000000001101"},
    {"Гермиона Грейнджер", "Лучшая подруга Гарри Поттера и самая умная ведьма курса", "Hermione Granger", "Brilliant witch and Harry Potter's best friend",
     "001001000101000100010001000000000100"},
    {"Человек-паук (Питер Паркер)", "Супергерой Marvel в красно-синем костюме и маске, пускающий паутину", "Spider-Man (Peter Parker)", "Marvel superhero with spider powers",
     "010011100101001100010001000001100010"},
    {"Железный человек (Тони Старк)", "Гений, миллиардер, плейбой, филантроп в высокотехнологичной броне Marvel", "Iron Man (Tony Stark)", "Genius billionaire superhero from Marvel",
     "010011100100011101010000010101101001"},
    {"Бэтмен (Брюс Уэйн)", "Тёмный рыцарь Готэма в маске и плаще летучей мыши (DC Comics)", "Batman (Bruce Wayne)", "The Dark Knight of Gotham from DC Comics",
     "010011100100101100010000000101100110"},
    {"Джокер", "Клоун-принц преступного мира Готэма и заклятый враг Бэтмена", "The Joker", "Arch-nemesis of Batman and Clown Prince of Crime",
     "010011100010110100001000000001100010"},
    {"Дэдпул (Уэйд Уилсон)", "Болтливый наёмник с регенерацией в красно-чёрном костюме и с катанами", "Deadpool (Wade Wilson)", "Merc with a Mouth from Marvel",
     "010011010111111100000100000001100001"},
    {"Росомаха (Логан)", "Мутант из Людей Икс с адамантиевыми когтями и регенерацией", "Wolverine (Logan)", "X-Men mutant with adamantium claws",
     "010011100101100100010000000001101000"},
    {"Танос", "Безумный титан с Перчаткой Бесконечности из вселенной Marvel", "Thanos", "The Mad Titan with the Infinity Gauntlet (Marvel)",
     "010011000011100010000100000001001000"},
    {"Тор", "Бог грома из Асгарда с молотом Мьёльниром (Marvel)", "Thor", "God of Thunder from Marvel",
     "010011000101100010100000000001000101"},
    {"Супермен (Кларк Кент)", "Человек из стали с планеты Криптон в красно-синем костюме с плащом (DC)", "Superman (Clark Kent)", "Man of Steel from DC Comics",
     "010011100101000010010010000001000101"},
    {"Гоумлендер (Твердыня / Патриот)", "Жестокий лидер Семёрки со светлыми волосами и лазерами из глаз («Пацаны»)", "Homelander", "Leader of The Seven from The Boys",
     "010001010011000100100000000001000101"},
    {"Уолтер Уайт (Хайзенберг)", "Учитель химии, ставший наркобароном в сериале «Во все тяжкие» (Breaking Bad)", "Walter White (Heisenberg)", "Chemistry teacher turned kingpin in Breaking Bad",
     "010001010110010100000110010000001010"},
    {"Джесси Пинкман", "Напарник Уолтера Уайта в сериале «Во все тяжкие»", "Jesse Pinkman", "Walter White's partner in Breaking Bad",
     "010001000110010100110000000000000010"},
    {"Сол Гудман (Джимми Макгилл)", "Харизматичный криминальный адвокат из «Во все тяжкие» и «Лучше звоните Солу»", "Saul Goodman", "Criminal lawyer from Breaking Bad & Better Call Saul",
     "010001010100000100100000000000000010"},
    {"Капитан Джек Воробей", "Эксцентричный пират с дредами и треуголкой из «Пиратов Карибского моря»", "Captain Jack Sparrow", "Eccentric pirate captain from Pirates of the Caribbean",
     "010001000100110100010000000000000110"},
    {"Джон Уик", "Легендарный наёмный убийца по прозвищу Баба-Яга (в исполнении Киану Ривза)", "John Wick", "Legendary hitman played by Keanu Reeves",
     "010001000100110100010000001000100010"},
    {"Нео (Томас Андерсон)", "Избранный в чёрном плаще и тёмных очках из фильма «Матрица»", "Neo (Thomas Anderson)", "The One from The Matrix",
     "010001000101010100010010001100001101"},
    {"Тайлер Дёрден", "Харизматичный основатель «Бойцовского клуба» в красной кожаной куртке", "Tyler Durden", "Charismatic founder of Fight Club",
     "010001010110000100110010001000000010"},
    {"Патрик Бэйтман", "Инвестиционный банкир с Уолл-стрит из фильма «Американский психопат»", "Patrick Bateman", "Main character of American Psycho",
     "010001010110100100010000000100000010"},
    {"Томас Шелби", "Глава цыганской банды из Бирмингема в сериале «Острые козырьки»", "Thomas Shelby", "Leader of the Peaky Blinders",
     "010001010110010100010000010100000110"},
    {"Терминатор (T-800)", "Киборг-убийца из будущего в исполнении Арнольда Шварцнеггера", "The Terminator (T-800)", "Cybernetic organism played by Arnold Schwarzenegger",
     "010001000100010001010010000000101000"},
    {"Оптимус Прайм", "Лидер автоботов, трансформирующийся в красно-синий грузовик («Трансформеры»)", "Optimus Prime", "Leader of the Autobots from Transformers",
     "010011100100111001000100000000100000"},
    {"Гэндальф", "Могущественный старый маг с посохом из «Властелина колец»", "Gandalf", "Wizard of Middle-earth from The Lord of the Rings",
     "010001000101100010100000000000000100"},
    {"Шерлок Холмс", "Знаменитый лондонский сыщик с Бейкер-стрит, мастер дедукции", "Sherlock Holmes", "World-famous consulting detective from Baker Street",
     "010001000100010100010000010000000110"},
    {"Уэнсдей Аддамс", "Мрачная девочка с двумя чёрными косичками из семейки Аддамс", "Wednesday Addams", "Dark pigtail-wearing daughter of the Addams Family",
     "001001000101100100010001000000000010"},
    {"Данила Багров", "Главный герой культовых российских фильмов «Брат» и «Брат 2»", "Danila Bagrov", "Main protagonist of the Russian films Brother and Brother 2",
     "010001001100010100100000000000001010"},
    {"Саша Белый", "Главный герой российского криминального сериала «Бригада»", "Sasha Bely", "Main character of the Russian crime series Brigada",
     "010001001110010100110000000100000110"},

    // ── Мультфильмы и анимация ───────────────────────────────────────────────
    {"Шрек", "Зелёный великан-огр, живущий на болоте («Шрек»)", "Shrek", "Green ogre living in a swamp",
     "010000110100000010000100000000000000"},
    {"Кот в сапогах", "Рыжий кот-фехтовальщик в шляпе и сапогах из «Шрека»", "Puss in Boots", "Swashbuckling orange cat from Shrek",
     "010000100100100010001000000000000100"},
    {"Губка Боб Квадратные Штаны", "Жёлтая морская губка, повар в «Красти Краб» из Бикини Боттом", "SpongeBob SquarePants", "Yellow sea sponge from Bikini Bottom",
     "010000110100000010100100000000000000"},
    {"Патрик Стар", "Розовая морская звезда и лучший друг Губки Боба", "Patrick Star", "Pink starfish and SpongeBob's best friend",
     "010000110000000010000100000000000000"},
    {"Рик Санчез", "Гениальный безумный учёный с портальной пушкой («Рик и Морти»)", "Rick Sanchez", "Mad scientist with a portal gun from Rick and Morty",
     "010000100100010100101000010000000101"},
    {"Гомер Симпсон", "Лысоватый любитель пончиков и глава семьи из «Симпсонов»", "Homer Simpson", "Donut-loving patriarch of The Simpsons",
     "010000100100000100000100000000000000"},
    {"Молния Маккуин (Кachow)", "Красная гоночная машина под номером 95 из мультфильма «Тачки»", "Lightning McQueen", "Red race car #95 from Pixar's Cars",
     "010000110100000011001100001000000000"},
    {"Крош (Смешарики)", "Весёлый голубой кролик с длинными ушами из мультсериала «Смешарики»", "Krosh (Kikoriki)", "Energetic blue rabbit from Kikoriki / Smeshariki",
     "010000101100000010001101000000000000"},
    {"Чебурашка", "Неизвестный науке милый зверёк с большими ушами, друг Крокодила Гены", "Cheburashka", "Big-eared furry creature from Soviet animation",
     "010000101100000010010001000000000000"},
    {"Винни-Пух", "Плюшевый медвежонок, обожающий мёд", "Winnie the Pooh", "Honey-loving teddy bear",
     "010000101100000010010000000000000000"},

    // ── Мемы и интернет-культура ─────────────────────────────────────────────
    {"Большой Шлёпа (Big Floppa)", "Каракал Гоша (русский кот), ставший главным мемом интернета с пельменями", "Big Floppa", "Caracal cat Gosha, iconic internet meme",
     "010000011100000010010000000000000000"},
    {"Лягушонок Пепе (Peepo / Pepe)", "Зелёная антропоморфная лягушка — самый популярный мем интернета и Twitch", "Pepe the Frog", "Iconic green frog internet meme",
     "010000010100000010000100000000000000"},
    {"Гигачад (Эрнест Халимов)", "Черно-белый ультра-мускулистый мужчина с квадратной челюстью из мемов", "GigaChad", "Muscular jawline meme icon",
     "110000011100000100010000001000000000"},
    {"Рикардо Милос", "Бразильский танцор в красной бандане из легендарных интернет-мемов", "Ricardo Milos", "Brazilian dancer in a red bandana from memes",
     "110000010100000100010000101000000000"},
    {"Билли Херрингтон (Aniki / Gachi)", "Легенда гачимучи-мемов и «босс качалки»", "Billy Herrington", "Gachimuchi meme legend (Aniki)",
     "110000010100000100010000001000001000"},
    {"Скибиди Туалет (Skibidi Toilet)", "Голова, торчащая из унитаза, из вирусного YouTube-сериала DaFuq!?Boom!", "Skibidi Toilet", "Viral YouTube animation character by DaFuq!?Boom!",
     "010000111110000010010000000000000000"},
    {"Хагги Вагги (Huggy Wuggy)", "Синий зубастый монстр с длинными лапами из игры Poppy Playtime", "Huggy Wuggy", "Blue furry monster from Poppy Playtime",
     "010010010010000010001000000000000000"},
    {"Импостер (Предатель / Among Us)", "Разноцветный космонавт-предатель из игры Among Us (Sus / Amogus)", "The Impostor (Among Us)", "Crewmate impostor from Among Us",
     "010010010110111010000100000000000000"},
    {"Шайлушай (Кот-смурфик)", "Синий кот-гриб в белой шапочке смурфа с улиткой на палочке («We live, we love, we lie»)", "Smurf Cat (Shailushai)", "Blue mushroom smurf cat meme",
     "010000011100000010001000000000000000"},

    // ── Стримеры, ютуберы и блогеры ──────────────────────────────────────────
    {"Папич (Виталий Цаль / Arthas)", "Легендарный стример по Dota 2 и казино («Величайший»)", "Papich (Arthas)", "Legendary Dota 2 streamer Vitaliy Tsal",
     "110010011100000100010000000010010000"},
    {"Мистер Бист (MrBeast / Джимми Дональдсон)", "Самый популярный ютубер в мире с масштабными челленджами и раздачами денег", "MrBeast (Jimmy Donaldson)", "World's most-subscribed YouTuber",
     "110000010100000100010000000110000000"},
    {"Пьюдипай (PewDiePie / Феликс Чельберг)", "Шведский ютубер и летсплейщик, бывший топ-1 канала YouTube", "PewDiePie (Felix Kjellberg)", "Legendary Swedish YouTuber",
     "110000010100000100100000000010000000"},
    {"Куплинов (Дмитрий Куплинов / Kuplinov Play)", "Самый известный российский летсплейщик и ютубер", "Dmitry Kuplinov", "Popular Russian gaming YouTuber (Kuplinov Play)",
     "110010011100000100010010000010000000"},
    {"Братишкин (Владимир Братишкин / bratishkinoff)", "Популярный российский Twitch-стример и глава 89-го сквада", "Bratishkin", "Popular Russian Twitch streamer",
     "110000011100000100110000000010000000"},
    {"Меллстрой (Андрей Бурим / Mellstroy)", "Скандальный треш-стример и блогер, прославившийся вирусными нарезками и донатами", "Mellstroy", "Viral streamer Andrey Burim",
     "110000011100000100010000000110000000"},
    {"Влад А4 (Влад Бумага)", "Самый популярный русскоязычный детский ютубер со светлыми волосами", "Vlad A4 (Vlad Bumaga)", "Popular Russian-speaking YouTuber",
     "110000011100000100100000000010000000"},
    {"Мармок (Марин Мокану / Marmok)", "Популярный русскоязычный ютубер по смешным моментам и багам в играх", "Marmok", "Popular gaming comedy YouTuber",
     "110010011100000100010000000010000000"},
    {"Бустер (Вячеслав Леонтьев / Buster)", "Популярный российский Twitch-стример («Полковник Бустеренко»)", "Buster", "Popular Russian Twitch streamer",
     "110000011100000100010000000010000000"},
    {"Ивангай (Иван Рудской / EeOneGuy)", "Легендарный русскоязычный ютубер золотой эпохи YouTube 2014–2016 годов", "EeOneGuy (Ivangai)", "Pioneering Russian-speaking YouTuber",
     "110000011100000100110000000010000000"},
    {"Александр Зубарев", "Популярный стример и блогер («Отец пельменей»), смотрящий фильмы на Twitch", "Alexander Zubarev", "Popular streamer and blogger",
     "110000011100000100000100000010000000"},
    {"IShowSpeed (Даррен Уоткинс)", "Экспрессивный американский стример, фанат Криштиану Роналду", "IShowSpeed", "Energetic American streamer and Ronaldo fan",
     "110000010100000100010001001010000000"},
    {"Пятёрка (Кирилл Баранов / 5opka)", "Популярный российский Twitch-стример и ютубер по Minecraft в маске/пакете", "5opka", "Popular Russian Twitch streamer in a mask",
     "110000011100001100010000000010010000"},
    {"Mazellovvv (Илья Коряков)", "Популярный российский Twitch-стример и рэпер, участник сквада ХАЗЯЕВА", "Mazellovvv", "Russian Twitch streamer and rapper from KHAZYAEVA",
     "110000011100000100010000100010000000"},
    {"Хесус (Алексей Губанов / JesusAVGN)", "Популярный российский Twitch-стример и летсплейщик, живущий в США", "JesusAVGN", "Popular Russian Twitch streamer",
     "110000011100000100010000000010000000"},
    {"Tenderlybae (Амина Мирзоева)", "Популярная российская стримерша и певица, носившая чёрную маску на лице", "Tenderlybae", "Popular streamer and singer known for her black mask",
     "101000011100001100010000100010000000"},
    {"Deepins (Иван / deepins)", "Популярный стример и тиктокер, прославившийся стримами в маске и неоновыми анимациями", "Deepins", "Popular streamer known for face mask and neon effects",
     "110000011100001100010000000010000000"},
    {"Стинт (Максим Шабанов / Stint)", "Популярный стример и ютубер по мемам и смешным видео", "Stint", "Popular meme streamer and YouTuber",
     "110000011100000100010000000010000000"},

    // ── Реальные люди: бизнес, IT, музыка, спорт, кино, история ──────────────
    {"Павел Дуров", "Создатель Telegram и ВКонтакте, предприниматель и программист", "Pavel Durov", "Founder of Telegram and VK",
     "110000011100000100010000000100000000"},
    {"Илон Маск", "Миллиардер, глава SpaceX, Tesla и X (Twitter)", "Elon Musk", "CEO of SpaceX and Tesla",
     "110000010100000100010000010100000000"},
    {"Гейб Ньюэлл (Габен)", "Глава компании Valve, создатель Steam, Half-Life, Dota 2 и Counter-Strike", "Gabe Newell (Gaben)", "Co-founder and president of Valve (Steam)",
     "110010010100000100100010000100110000"},
    {"Стив Джобс", "Сооснователь корпорации Apple в чёрной водолазке и круглых очках", "Steve Jobs", "Co-founder of Apple Inc.",
     "110000000100000100010110000100001000"},
    {"Билл Гейтс", "Основатель корпорации Microsoft и один из богатейших людей мира", "Bill Gates", "Co-founder of Microsoft",
     "110000000100000100100010000100000000"},
    {"Криштиану Роналду (CR7)", "Легендарный португальский футболист («Siuuu!»)", "Cristiano Ronaldo", "Legendary Portuguese footballer (CR7)",
     "110000010100000100010000001000000000"},
    {"Лионель Месси", "Легендарный аргентинский футболист, чемпион мира и обладатель 8 «Золотых мячей»", "Lionel Messi", "Argentine football legend and World Cup champion",
     "110000000100000100010000001000000000"},
    {"Хабиб Нурмагомедов", "Непобеждённый российский чемпион UFC в лёгком весе (29-0)", "Khabib Nurmagomedov", "Undefeated UFC Lightweight Champion (29-0)",
     "110000001100000100010000001000000000"},
    {"Майк Тайсон", "Легендарный американский боксёр-тяжеловес («Железный Майк»)", "Mike Tyson", "Legendary heavyweight boxing champion",
     "110000000100000100010100001000000000"},
    {"Виктор Цой", "Лидер культовой рок-группы «Кино», легенда русского рока", "Viktor Tsoi", "Frontman of the legendary Soviet rock band Kino",
     "110000001100000100010000100000001000"},
    {"Михаил Горшенёв (Горшок)", "Лидер панк-рок-группы «Король и Шут»", "Mikhail Gorshenyov (Gorshok)", "Frontman of Russian punk band Korol i Shut",
     "110000001100000100010000100000001000"},
    {"Моргенштерн (Алишер Моргенштерн)", "Известный русскоязычный рэп-исполнитель и шоумен с татуировкой 666", "Morgenshtern", "Russian rap artist and showman",
     "110000011100000100010000100010000000"},
    {"Егор Крид (Егор Булаткин)", "Популярный российский поп- и рэп-исполнитель, бывший артист Black Star, стример", "Egor Kreed", "Popular Russian pop and rap artist",
     "110000011000000100100000100010000000"},
    {"Nkeeei (Никита Коробыко)", "Популярный русскоязычный рэпер, автор вирусных хитов «Гламур», «Толпы кричат»", "Nkeeei", "Popular Russian-speaking rap artist",
     "110000011000000100010001100000000000"},
    {"MACAN (Андрей Косолапов)", "Популярный российский рэп-исполнитель («Юг», «Асфальт», «Останься образом»)", "MACAN", "Russian rap artist",
     "110000011000000100010000100000000000"},
    {"OG Buda (Григорий Ляхов)", "Российский рэпер, участник объединения Melon Music", "OG Buda", "Russian rapper from Melon Music",
     "110000011000000100010000100000000000"},
    {"Big Baby Tape (Егор Ракитин)", "Популярный российский рэпер и битмейкер в очках («Dragonborn», «Bandana»)", "Big Baby Tape", "Popular Russian rapper and beatmaker",
     "110000011000000100010010100000000000"},
    {"Miyagi (Азамат Кудзаев)", "Владикавказский рэпер и музыкант, участник дуэта Miyagi & Эндшпиль", "Miyagi", "Russian hip-hop artist from Vladikavkaz",
     "110000001000000100010000100000000000"},
    {"Баста (Василий Вакуленко)", "Легендарный российский рэпер, основатель лейбла Gazgolder, Ноггано", "Basta", "Legendary Russian rapper and producer",
     "110000001000000100010000100000000000"},
    {"Шаман (Ярослав Дронов)", "Российский певец и автор песен с белыми дредами («Встанем», «Я русский»)", "SHAMAN", "Russian singer and songwriter",
     "110000001000000100100000100000000000"},
    {"Инстасамка (Дарья Зотеева)", "Скандальная рэп-исполнительница и блогерша («За деньги да»)", "Instasamka", "Controversial Russian rap artist and blogger",
     "101000011000000100010000100010000000"},
    {"ANNA ASTI (Анна Дзюба)", "Популярная поп-певица, бывшая солистка дуэта Artik & Asti («Царица»)", "Anna Asti", "Popular pop singer",
     "101000001000000100100000100000000000"},
    {"Скриптонит (Адиль Жалелов)", "Казахстанский рэпер, музыкант и битмейкер, основатель Musica36", "Scryptonite", "Kazakh rapper and producer",
     "110000001000000100010000100000000000"},
    {"Scally Milano (Даниил Дмитриев)", "Российский рэп-исполнитель в жанре скам-рэп", "Scally Milano", "Russian rap artist",
     "110000011000000100010001100000000000"},
    {"Эминем (Маршалл Мэтерс)", "Легендарный американский рэпер (Slim Shady)", "Eminem", "Legendary American rapper (Marshall Mathers)",
     "110000000100000100110000100000000000"},
    {"Майкл Джексон", "Король поп-музыки, создатель «лунной походки»", "Michael Jackson", "The King of Pop",
     "110000000100000100010000100000001000"},
    {"Райан Гослинг", "Голливудский актёр («Драйв», «Бегущий по лезвию 2049», «Барби») и герой мемов «Да не умер он»", "Ryan Gosling", "Actor from Drive and Blade Runner 2049",
     "110001010100000100100000000000000000"},
    {"Киану Ривз", "Голливудский актёр («Матрица», «Джон Уик», Джонни Сильверхенд в Cyberpunk 2077)", "Keanu Reeves", "Actor of The Matrix, John Wick, and Cyberpunk 2077",
     "110011010100000100010000000000000000"},
    {"Альберт Эйнштейн", "Великий физик-теоретик, автор теории относительности ($E=mc^2$)", "Albert Einstein", "Theoretical physicist who developed the theory of relativity",
     "110000000100000100100000010000001000"},
    {"Александр Сергеевич Пушкин", "Великий русский поэт, «солнце русской поэзии»", "Alexander Pushkin", "Greatest Russian poet and founder of modern Russian literature",
     "110000001100000100010000010000001000"},
    {"Юрий Гагарин", "Первый человек в мировой истории, совершивший полёт в космос", "Yuri Gagarin", "First human to journey into outer space",
     "110000001100001100010000010000001001"}
};

constexpr int NUM_CHARACTERS = static_cast<int>(sizeof(CHARACTERS) / sizeof(CHARACTERS[0]));

// ── Состояние игры ───────────────────────────────────────────────────────────
struct QAEntry {
    int q_idx = -1;
    std::string q_text;
    int ans = 2; // 0=Да, 1=Нет, 2=Не знаю, 3=Возможно, 4=Скорее нет
};

struct AkiState {
    std::string lang = "ru";
    std::string session; // "hybrid" или ID сессии прокси
    std::string step = "1";
    std::string prog = "0";
    std::string slp;
    int num = 1;
    bool child_mode = false;
    int cur_q_idx = 0;
    std::string last_q;
    std::string guess_name;
    std::string guess_desc;
    std::string guess_photo;
    long long chat_id = 0;
    long long msg_id = 0;
    long long owner_id = 0;
    std::vector<QAEntry> history;
    std::vector<std::string> excluded;
};

static std::mutex g_aki_mu;
static std::unordered_map<long long, AkiState> g_mem_states;

std::string state_db_key(long long id) {
    return "aki_state:" + std::to_string(id);
}

void save_state(koto_ctx* ctx, const koto_api* api, long long chat_id, const AkiState& st) {
    json j;
    j["lang"] = st.lang;
    j["session"] = st.session;
    j["step"] = st.step;
    j["prog"] = st.prog;
    j["slp"] = st.slp;
    j["num"] = st.num;
    j["child_mode"] = st.child_mode;
    j["cur_q_idx"] = st.cur_q_idx;
    j["last_q"] = st.last_q;
    j["guess_name"] = st.guess_name;
    j["guess_desc"] = st.guess_desc;
    j["guess_photo"] = st.guess_photo;
    j["chat_id"] = st.chat_id != 0 ? st.chat_id : chat_id;
    j["msg_id"] = st.msg_id;
    j["owner_id"] = st.owner_id;

    json j_hist = json::array();
    for (const auto& h : st.history) {
        j_hist.push_back({{"q_idx", h.q_idx}, {"q_text", h.q_text}, {"ans", h.ans}});
    }
    j["history"] = j_hist;
    j["excluded"] = st.excluded;

    std::string s = j.dump();
    long long real_cid = st.chat_id != 0 ? st.chat_id : chat_id;
    if (api->db_set) {
        if (real_cid != 0) {
            api->db_set(ctx, state_db_key(real_cid).c_str(), s.c_str());
            api->db_set(ctx, state_db_key(normalize_cid(real_cid)).c_str(), s.c_str());
            api->db_set(ctx, state_db_key(bare_cid(real_cid)).c_str(), s.c_str());
        }
        if (st.owner_id != 0) {
            api->db_set(ctx, ("aki_act:" + std::to_string(st.owner_id)).c_str(), s.c_str());
        }
    }
    std::lock_guard<std::mutex> lk(g_aki_mu);
    if (real_cid != 0) {
        g_mem_states[real_cid] = st;
        g_mem_states[normalize_cid(real_cid)] = st;
        g_mem_states[bare_cid(real_cid)] = st;
    }
    if (st.owner_id != 0) {
        g_mem_states[st.owner_id] = st;
    }
}

bool parse_state_json(const std::string& raw, AkiState& st) {
    try {
        json j = json::parse(raw);
        st.lang = j.value("lang", "ru");
        st.session = j.value("session", "");
        st.step = j.value("step", "1");
        st.prog = j.value("prog", "0");
        st.slp = j.value("slp", "");
        st.num = j.value("num", 1);
        st.child_mode = j.value("child_mode", false);
        st.cur_q_idx = j.value("cur_q_idx", 0);
        st.last_q = j.value("last_q", "");
        st.guess_name = j.value("guess_name", "");
        st.guess_desc = j.value("guess_desc", "");
        st.guess_photo = j.value("guess_photo", "");
        st.chat_id = j.value("chat_id", 0LL);
        st.msg_id = j.value("msg_id", 0LL);
        st.owner_id = j.value("owner_id", 0LL);
        st.history.clear();
        if (j.contains("history") && j["history"].is_array()) {
            for (const auto& item : j["history"]) {
                QAEntry e;
                e.q_idx = item.value("q_idx", -1);
                e.q_text = item.value("q_text", "");
                e.ans = item.value("ans", 2);
                st.history.push_back(e);
            }
        }
        st.excluded.clear();
        if (j.contains("excluded") && j["excluded"].is_array()) {
            for (const auto& ex : j["excluded"]) {
                if (ex.is_string()) st.excluded.push_back(ex.get<std::string>());
            }
        }
        return !st.session.empty();
    } catch (...) {
        return false;
    }
}

AkiState load_state(koto_ctx* ctx, const koto_api* api, long long chat_id, long long sender_id) {
    {
        std::lock_guard<std::mutex> lk(g_aki_mu);
        if (chat_id != 0) {
            auto it = g_mem_states.find(chat_id);
            if (it == g_mem_states.end()) it = g_mem_states.find(normalize_cid(chat_id));
            if (it == g_mem_states.end()) it = g_mem_states.find(bare_cid(chat_id));
            if (it != g_mem_states.end() && !it->second.session.empty()) return it->second;
        }
        if (sender_id != 0) {
            auto it = g_mem_states.find(sender_id);
            if (it != g_mem_states.end() && !it->second.session.empty()) return it->second;
        }
    }
    AkiState st;
    if (!api->db_get) return st;
    char buf[32768] = {0};
    if (chat_id != 0) {
        int n = api->db_get(ctx, state_db_key(chat_id).c_str(), buf, sizeof(buf));
        if (n <= 0) n = api->db_get(ctx, state_db_key(normalize_cid(chat_id)).c_str(), buf, sizeof(buf));
        if (n <= 0) n = api->db_get(ctx, state_db_key(bare_cid(chat_id)).c_str(), buf, sizeof(buf));
        if (n > 0 && parse_state_json(std::string(buf, (size_t)n), st)) return st;
    }
    if (sender_id != 0) {
        int n = api->db_get(ctx, ("aki_act:" + std::to_string(sender_id)).c_str(), buf, sizeof(buf));
        if (n > 0 && parse_state_json(std::string(buf, (size_t)n), st)) return st;
    }
    return st;
}

void clear_state(koto_ctx* ctx, const koto_api* api, long long chat_id, long long sender_id, const AkiState& st) {
    long long real_cid = st.chat_id != 0 ? st.chat_id : chat_id;
    long long real_owner = st.owner_id != 0 ? st.owner_id : sender_id;
    if (api->db_del) {
        if (real_cid != 0) {
            api->db_del(ctx, state_db_key(real_cid).c_str());
            api->db_del(ctx, state_db_key(normalize_cid(real_cid)).c_str());
            api->db_del(ctx, state_db_key(bare_cid(real_cid)).c_str());
        }
        if (real_owner != 0) {
            api->db_del(ctx, ("aki_act:" + std::to_string(real_owner)).c_str());
        }
    }
    std::lock_guard<std::mutex> lk(g_aki_mu);
    if (real_cid != 0) {
        g_mem_states.erase(real_cid);
        g_mem_states.erase(normalize_cid(real_cid));
        g_mem_states.erase(bare_cid(real_cid));
    }
    if (real_owner != 0) {
        g_mem_states.erase(real_owner);
    }
}

// ── Математическое ядро Байесовского Акинатора + AI ──────────────────────────
bool is_excluded(const AkiState& st, const CharacterDef& c) {
    for (const auto& ex : st.excluded) {
        if (ex == c.name_ru || ex == c.name_en) return true;
    }
    return false;
}

void compute_posteriors(const AkiState& st, std::vector<double>& probs, int& best_idx, double& max_prob, int& best_contradictions) {
    probs.assign(NUM_CHARACTERS, 1.0);
    std::vector<int> contradictions(NUM_CHARACTERS, 0);

    for (int i = 0; i < NUM_CHARACTERS; ++i) {
        if (is_excluded(st, CHARACTERS[i])) {
            probs[i] = 0.0;
            contradictions[i] = 999;
            continue;
        }
        const char* t = CHARACTERS[i].traits;
        int t_len = (int)std::strlen(t);
        for (const auto& qa : st.history) {
            if (qa.q_idx < 0 || qa.q_idx >= NUM_TRAITS || qa.q_idx >= t_len) continue;
            char tv = t[qa.q_idx];
            if (tv == '?') continue;
            bool has = (tv == '1');
            double mult = 1.0;
            switch (qa.ans) {
                case 0: // Да
                    mult = has ? 0.92 : 0.06;
                    if (!has) contradictions[i]++;
                    break;
                case 1: // Нет
                    mult = has ? 0.06 : 0.92;
                    if (has) contradictions[i]++;
                    break;
                case 3: // Возможно
                    mult = has ? 0.76 : 0.24;
                    break;
                case 4: // Скорее нет
                    mult = has ? 0.24 : 0.76;
                    break;
                default: // Не знаю
                    mult = 1.0;
                    break;
            }
            probs[i] *= mult;
        }
    }

    double sum = 0.0;
    for (double p : probs) sum += p;
    if (sum > 0.0) {
        for (double& p : probs) p /= sum;
    }

    best_idx = 0;
    max_prob = 0.0;
    for (int i = 0; i < NUM_CHARACTERS; ++i) {
        if (probs[i] > max_prob) {
            max_prob = probs[i];
            best_idx = i;
        }
    }
    best_contradictions = contradictions[best_idx];
}

// Проверяем, не является ли вопрос избыточным с учётом предыдущих ответов
bool is_question_redundant(const AkiState& st, int q_idx) {
    for (const auto& qa : st.history) {
        if (qa.q_idx == q_idx) return true;
    }
    auto ans_of = [&](int idx) -> int {
        for (const auto& qa : st.history) {
            if (qa.q_idx == idx) return qa.ans;
        }
        return -1;
    };

    int a_real = ans_of(0);
    int a_male = ans_of(1);
    int a_fem  = ans_of(2);
    int a_ani  = ans_of(3);
    int a_game = ans_of(4);
    int a_live = ans_of(5);
    int a_cart = ans_of(6);
    int a_hum  = ans_of(15);
    int a_anim = ans_of(16);
    int a_robo = ans_of(17);
    int a_blnd = ans_of(18);
    int a_dark = ans_of(19);
    int a_colo = ans_of(20);
    int a_bald = ans_of(21);

    // Пол взаимоисключающий
    if (q_idx == 2 && a_male == 0) return true;
    if (q_idx == 1 && a_fem == 0) return true;

    // Если реальный человек -> не аниме, не мультфильм, не животное, не робот, не супергерой, не магия/полёт
    if (a_real == 0) {
        if (q_idx == 3 || q_idx == 6 || q_idx == 11 || q_idx == 15 || q_idx == 16 || q_idx == 17 || q_idx == 29 || q_idx == 35) {
            return true;
        }
    }
    // Если нереальный персонаж -> не реальные профессии (IT-миллиардер, стример, политик/учёный, спортсмен)
    if (a_real == 1) {
        if (q_idx == 25 || q_idx == 27 || q_idx == 28) return true;
    }
    // Если из аниме -> не реальный человек, не мультфильм (не аниме), не фильм с живыми актёрами
    if (a_ani == 0) {
        if (q_idx == 0 || q_idx == 5 || q_idx == 6 || q_idx == 25 || q_idx == 27 || q_idx == 28 || q_idx == 29) return true;
    }
    // Если не из видеоигры -> не спрашиваем про жанры игр
    if (a_game == 1 && (q_idx == 30 || q_idx == 31)) return true;
    // Если из мультфильма -> не реальный человек и не аниме
    if (a_cart == 0 && (q_idx == 0 || q_idx == 3 || q_idx == 5)) return true;
    // Если из фильма с живыми актёрами -> не аниме и не мультфильм
    if (a_live == 0 && (q_idx == 3 || q_idx == 6)) return true;

    // Раса/существо взаимоисключающие
    if (a_hum == 0 && (q_idx == 16 || q_idx == 17)) return true;
    if ((a_anim == 0 || a_robo == 0) && q_idx == 15) return true;

    // Цвет волос взаимоисключающий
    if (a_bald == 0 && (q_idx == 18 || q_idx == 19 || q_idx == 20)) return true;
    if ((a_blnd == 0 || a_dark == 0 || a_colo == 0) && q_idx == 21) return true;
    if (a_blnd == 0 && (q_idx == 19 || q_idx == 20)) return true;
    if (a_dark == 0 && (q_idx == 18 || q_idx == 20)) return true;
    if (a_colo == 0 && (q_idx == 18 || q_idx == 19)) return true;

    return false;
}

// Выбор лучшего следующего вопроса по максимуму информационной энтропии Шеннона
int select_next_question(const AkiState& st, const std::vector<double>& probs) {
    int best_q = -1;
    double best_score = -1.0;

    for (int q = 0; q < NUM_TRAITS; ++q) {
        if (is_question_redundant(st, q)) continue;

        double p_yes = 0.0;
        double p_no = 0.0;
        for (int i = 0; i < NUM_CHARACTERS; ++i) {
            if (probs[i] <= 1e-9) continue;
            const char* t = CHARACTERS[i].traits;
            char tv = (q < (int)std::strlen(t)) ? t[q] : '?';
            if (tv == '1') p_yes += probs[i];
            else if (tv == '0') p_no += probs[i];
            else {
                p_yes += 0.5 * probs[i];
                p_no += 0.5 * probs[i];
            }
        }
        double total = p_yes + p_no;
        if (total <= 1e-9) continue;
        double r = p_yes / total;
        // Оценка близости разбиения к идеальному 50/50 (энтропия)
        double entropy = 0.0;
        if (r > 1e-6 && r < 1.0 - 1e-6) {
            entropy = -r * std::log2(r) - (1.0 - r) * std::log2(1.0 - r);
        }
        // Небольшой приоритет фундаментальным вопросам на первых шагах
        if (st.num <= 4 && (q <= 7 || q == 9 || q == 15)) {
            entropy += 0.05;
        }
        if (entropy > best_score) {
            best_score = entropy;
            best_q = q;
        }
    }
    return best_q;
}

std::string ans_label_ru(int ans) {
    switch (ans) {
        case 0: return "Да";
        case 1: return "Нет";
        case 3: return "Возможно";
        case 4: return "Скорее нет";
        default: return "Не знаю";
    }
}

// Запрос к нейросети (Pollinations OpenAI) для дедукции любого персонажа в мире,
// если загадан редкий персонаж вне локальной базы или после отклонения гипотезы.
bool query_ai_guess(koto_ctx* ctx, const koto_api* api, const AkiState& st, std::string& out_name, std::string& out_desc) {
    if (!api->http_post) return false;

    std::ostringstream hist_ss;
    for (size_t i = 0; i < st.history.size(); ++i) {
        hist_ss << (i + 1) << ". " << st.history[i].q_text << " -> " << ans_label_ru(st.history[i].ans) << "\n";
    }
    if (!st.excluded.empty()) {
        hist_ss << "Исключённые персонажи (ЭТО ТОЧНО НЕ ОНИ, НЕ НАЗЫВАЙ ИХ): ";
        for (size_t i = 0; i < st.excluded.size(); ++i) {
            if (i > 0) hist_ss << ", ";
            hist_ss << st.excluded[i];
        }
        hist_ss << "\n";
    }

    bool is_ru = (st.lang == "ru");
    json req;
    req["model"] = "openai";
    req["reasoning_effort"] = "low";
    req["max_tokens"] = 450;
    req["messages"] = json::array({
        {
            {"role", "system"},
            {"content", is_ru
                ? "Ты Акинатор. Угадай загаданного персонажа по ответам игрока. Ответь СТРОГО одной строкой в формате: ИМЯ ПЕРСОНАЖА | КРАТКОЕ ОПИСАНИЕ (кто это и откуда). Без кавычек и без лишнего текста."
                : "You are Akinator. Guess the character from the player's answers. Reply STRICTLY on a single line in format: CHARACTER NAME | SHORT DESCRIPTION (who and from where). No quotes or extra text."}
        },
        {
            {"role", "user"},
            {"content", hist_ss.str()}
        }
    });

    std::string body = req.dump();
    std::string headers = "Content-Type: application/json; charset=utf-8\r\nAccept: application/json\r\n";
    std::vector<unsigned char> buf(65536, 0);
    long long n = api->http_post(ctx, "https://text.pollinations.ai/openai", body.c_str(), headers.c_str(), buf.data(), (int)buf.size());
    if (n <= 0) return false;

    try {
        std::string raw(reinterpret_cast<const char*>(buf.data()), (size_t)n);
        json j = json::parse(raw);
        if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
            std::string content = trim(j["choices"][0]["message"].value("content", ""));
            size_t pipe = content.find('|');
            if (pipe != std::string::npos) {
                std::string nm = trim(content.substr(0, pipe));
                std::string ds = trim(content.substr(pipe + 1));
                if (!nm.empty() && nm.size() < 120) {
                    out_name = nm;
                    out_desc = ds;
                    return true;
                }
            }
        }
    } catch (...) {}
    return false;
}

// ── Заголовки для прямого подключения к ru.akinator.com ─────────────────────
std::string aki_form_headers(const std::string& lang, const std::string& referer) {
    return "Content-Type: application/x-www-form-urlencoded\r\n"
           "User-Agent: Mozilla/5.0 (Linux; Android 14; K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.6367.113 Mobile Safari/537.36\r\n"
           "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
           "Accept-Language: ru-RU,ru;q=0.9,en-US;q=0.8,en;q=0.7\r\n"
           "Origin: https://" + lang + ".akinator.com\r\n"
           "Referer: " + referer + "\r\n";
}

std::string aki_ajax_headers(const std::string& lang) {
    return "Content-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n"
           "X-Requested-With: XMLHttpRequest\r\n"
           "Accept: application/json, text/javascript, */*; q=0.01\r\n"
           "User-Agent: Mozilla/5.0 (Linux; Android 14; K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.6367.113 Mobile Safari/537.36\r\n"
           "Accept-Language: ru-RU,ru;q=0.9,en-US;q=0.8,en;q=0.7\r\n"
           "Origin: https://" + lang + ".akinator.com\r\n"
           "Referer: https://" + lang + ".akinator.com/game\r\n";
}

std::string get_proxy(koto_ctx* ctx, const koto_api* api) {
    char buf[512] = {0};
    if (api->db_get && api->db_get(ctx, "aki_proxy", buf, sizeof(buf)) > 0 && buf[0]) {
        return trim(buf);
    }
    if (api->setting_get && api->setting_get(ctx, "proxy", buf, sizeof(buf)) > 0 && buf[0]) {
        return trim(buf);
    }
    return "";
}

std::string build_aki_url(koto_ctx* ctx, const koto_api* api, const std::string& lang, const std::string& endpoint) {
    std::string proxy = get_proxy(ctx, api);
    if (!proxy.empty()) {
        while (!proxy.empty() && proxy.back() == '/') proxy.pop_back();
        if (proxy.find("?url=") != std::string::npos) {
            return proxy + url_encode("https://" + lang + ".akinator.com/" + endpoint);
        }
        if (proxy.find('?') != std::string::npos) {
            return proxy + "&url=" + url_encode("https://" + lang + ".akinator.com/" + endpoint);
        }
        return proxy + "/" + endpoint;
    }
    return "https://" + lang + ".akinator.com/" + endpoint;
}

std::string http_post_req(koto_ctx* ctx, const koto_api* api, const std::string& url, const std::string& body, const std::string& headers) {
    if (!api->http_post) return "";
    std::vector<unsigned char> buf(256 * 1024, 0);
    long long n = api->http_post(ctx, url.c_str(), body.c_str(), headers.c_str(), buf.data(), (int)buf.size());
    if (n <= 0) return "";
    return std::string(reinterpret_cast<const char*>(buf.data()), (size_t)n);
}

// ── Отрисовка кнопок и сообщений ─────────────────────────────────────────────
void add_question_buttons(koto_ctx* ctx, const koto_api* api, const std::string& lang, bool show_back = false, const std::string& session = "") {
    bool is_ru = (lang == "ru");
    if (api->c_button_ex) {
        api->c_button_ex(ctx, is_ru ? "✅ Да" : "✅ Yes", "aki:ans:0", 1, KOTO_BTN_SUCCESS, 0);
        api->c_button_ex(ctx, is_ru ? "❌ Нет" : "❌ No", "aki:ans:1", 0, KOTO_BTN_DANGER, 0);
        api->c_button_ex(ctx, is_ru ? "🤷 Не знаю" : "🤷 Don't know", "aki:ans:2", 0, KOTO_BTN_DEFAULT, 0);

        api->c_button_ex(ctx, is_ru ? "🤔 Возможно" : "🤔 Probably", "aki:ans:3", 1, KOTO_BTN_PRIMARY, 0);
        api->c_button_ex(ctx, is_ru ? "👎 Скорее нет" : "👎 Probably not", "aki:ans:4", 0, KOTO_BTN_DEFAULT, 0);
        if (show_back) {
            api->c_button_ex(ctx, is_ru ? "⬅️ Назад" : "⬅️ Back", "aki:back", 0, KOTO_BTN_DEFAULT, 0);
        }
        api->c_button_ex(ctx, is_ru ? "🛑 Стоп" : "🛑 Stop", "aki:stop", 1, KOTO_BTN_DANGER, 0);
        if (session == "ai") {
            api->c_button_ex(ctx, is_ru ? "🌐 Официальный режим" : "🌐 Official mode", "aki:mode:off", 1, KOTO_BTN_DEFAULT, 0);
        } else {
            api->c_button_ex(ctx, is_ru ? "🤖 Режим ИИ (GPT-4o)" : "🤖 AI Mode (GPT-4o)", "aki:mode:ai", 1, KOTO_BTN_DEFAULT, 0);
        }
    } else {
        api->c_button(ctx, is_ru ? "✅ Да" : "✅ Yes", "aki:ans:0", 1);
        api->c_button(ctx, is_ru ? "❌ Нет" : "❌ No", "aki:ans:1", 0);
        api->c_button(ctx, is_ru ? "🤷 Не знаю" : "🤷 Don't know", "aki:ans:2", 0);

        api->c_button(ctx, is_ru ? "🤔 Возможно" : "🤔 Probably", "aki:ans:3", 1);
        api->c_button(ctx, is_ru ? "👎 Скорее нет" : "👎 Probably not", "aki:ans:4", 0);
        if (show_back) {
            api->c_button(ctx, is_ru ? "⬅️ Назад" : "⬅️ Back", "aki:back", 0);
        }
        api->c_button(ctx, is_ru ? "🛑 Стоп" : "🛑 Stop", "aki:stop", 1);
    }
}

void add_guess_buttons(koto_ctx* ctx, const koto_api* api, const std::string& lang) {
    bool is_ru = (lang == "ru");
    if (api->c_button_ex) {
        api->c_button_ex(ctx, is_ru ? "✅ Да, это он!" : "✅ Yes, that's him!", "aki:win", 1, KOTO_BTN_SUCCESS, 0);
        api->c_button_ex(ctx, is_ru ? "❌ Это не он" : "❌ Not him", "aki:rej", 0, KOTO_BTN_DANGER, 0);
        api->c_button_ex(ctx, is_ru ? "🛑 Стоп" : "🛑 Stop", "aki:stop", 1, KOTO_BTN_DEFAULT, 0);
    } else {
        api->c_button(ctx, is_ru ? "✅ Да, это он!" : "✅ Yes, that's him!", "aki:win", 1);
        api->c_button(ctx, is_ru ? "❌ Это не он" : "❌ Not him", "aki:rej", 0);
        api->c_button(ctx, is_ru ? "🛑 Стоп" : "🛑 Stop", "aki:stop", 1);
    }
}

void render_question(koto_ctx* ctx, const koto_api* api, const AkiState& st) {
    api->c_reset(ctx);
    std::string heading = (st.lang == "ru" ? "🔮 Акинатор · Вопрос #" : "🔮 Akinator · Question #") + std::to_string(st.num);
    api->c_fmt(ctx, heading.c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, "\n\n");
    api->c_fmt(ctx, st.last_q.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n\n");
    std::string mode_str;
    if (st.session == "hybrid") mode_str = " [offline]";
    else if (st.session == "ai") mode_str = " [AI • GPT-4o]";
    else mode_str = " [official]";
    std::string hint = (st.lang == "ru" ? "📊 Прогресс: " : "📊 Progress: ") + st.prog + "%" + mode_str + " · by kote";
    api->c_fmt(ctx, hint.c_str(), KOTO_ENT_ITALIC);
    add_question_buttons(ctx, api, st.lang, st.num > 1, st.session);
}

void render_guess(koto_ctx* ctx, const koto_api* api, const AkiState& st) {
    api->c_reset(ctx);
    std::string header = (st.lang == "ru") ? "🔮 Акинатор предполагает:\n\n" : "🔮 Akinator thinks of:\n\n";
    api->c_fmt(ctx, header.c_str(), KOTO_ENT_BOLD);
    api->c_fmt(ctx, st.guess_name.c_str(), KOTO_ENT_CODE);
    if (!st.guess_desc.empty()) {
        api->c_text(ctx, "\n");
        api->c_fmt(ctx, st.guess_desc.c_str(), KOTO_ENT_ITALIC);
    }
    api->c_text(ctx, st.lang == "ru" ? "\n\nЯ угадал?" : "\n\nDid I guess right?");
    add_guess_buttons(ctx, api, st.lang);
}

// ── Command: .aki / .akinator ────────────────────────────────────────────────
void cmd_akinator(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";

    if (args.rfind("proxy", 0) == 0) {
        std::string p_arg = trim(args.substr(5));
        api->c_reset(ctx);
        if (p_arg.empty() || p_arg == "off" || p_arg == "none" || p_arg == "0") {
            if (api->db_del) api->db_del(ctx, "aki_proxy");
            api->c_fmt(ctx, "🛑 Прокси/Cloudflare Worker для Акинатора отключён (используется встроенный движок).", KOTO_ENT_BOLD);
        } else {
            if (api->db_set) api->db_set(ctx, "aki_proxy", p_arg.c_str());
            api->c_fmt(ctx, "✅ Прокси/Cloudflare Worker успешно установлен:\n", KOTO_ENT_BOLD);
            api->c_fmt(ctx, p_arg.c_str(), KOTO_ENT_CODE);
        }
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    char lang_buf[32] = {0};
    std::string lang = "ru";
    if (api->setting_get && api->setting_get(ctx, "lang", lang_buf, sizeof(lang_buf)) > 0 && lang_buf[0]) {
        lang = lang_buf;
    }
    if (args.find("en") != std::string::npos) lang = "en";
    else if (args.find("ru") != std::string::npos) lang = "ru";

    bool child = (args.find("--child") != std::string::npos || args.find("-c") != std::string::npos);

    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id ? api->sender_id(ctx) : 0;
    if (cid == 0) cid = sender;
    long long cmd_mid = api->msg_id ? api->msg_id(ctx) : 0;

    AkiState st;
    st.lang = lang;
    st.child_mode = child;
    st.chat_id = cid;
    st.owner_id = sender;
    st.msg_id = (cmd_mid > 0) ? (cmd_mid + 1) : 0;
    st.num = 1;
    st.prog = "0";

    // 1. Попытка прямого запуска официального Акинатора (ru.akinator.com)
    std::string game_url = build_aki_url(ctx, api, lang, "game");
    std::string game_body = "cm=" + std::string(child ? "true" : "false") + "&sid=1";
    std::string game_headers = aki_form_headers(lang, "https://" + lang + ".akinator.com/");
    std::string resp = http_post_req(ctx, api, game_url, game_body, game_headers);
    bool official_started = false;

    if (!resp.empty() && resp.find("403 Forbidden") == std::string::npos && resp.find("Attention Required") == std::string::npos && resp.find("cf-turnstile") == std::string::npos) {
        try {
            std::smatch m;
            std::regex ses_re1(R"(localStorage\.setItem\(['"]session['"],\s*['"]([^'"]+)['"]\))");
            std::regex ses_re2(R"(session['"]?\s*:\s*['"]([^'"]+)['"])");
            std::regex ses_re3(R"(session['"]?\)\.val\(['"]([^'"]+)['"]\))");
            std::regex ses_re4(R"(name=['"]session['"]\s+value=['"]([^'"]+)['"])");
            if (std::regex_search(resp, m, ses_re1) || std::regex_search(resp, m, ses_re2) || std::regex_search(resp, m, ses_re3) || std::regex_search(resp, m, ses_re4)) {
                st.session = m[1].str();
            }
            std::regex step_re(R"(localStorage\.setItem\(['"]step['"],\s*['"](\d+)['"]\))");
            if (std::regex_search(resp, m, step_re)) {
                st.step = m[1].str();
            }
            std::regex q_re(R"(class="question-text"[^>]*>([^<]+)</p>)");
            if (std::regex_search(resp, m, q_re)) {
                st.last_q = html_unescape(trim(m[1].str()));
            }
            if (!st.session.empty() && !st.last_q.empty()) {
                official_started = true;
            }
        } catch (...) {}
    }

    if (!official_started) {
        // Fallback: запуск встроенного гибридного движка
        st.session = "hybrid";
        st.step = "1";
        st.cur_q_idx = 0; // Вопрос #1: Реальный человек?
        st.last_q = (lang == "ru") ? QUESTIONS[0].ru : QUESTIONS[0].en;
    }

    save_state(ctx, api, cid, st);
    render_question(ctx, api, st);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Command: .akiproxy / .akistop ────────────────────────────────────────────
void cmd_akiproxy(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    const char* raw_args = api->cmd_args(ctx);
    std::string p_arg = raw_args ? trim(raw_args) : "";
    api->c_reset(ctx);
    if (p_arg.empty() || p_arg == "off" || p_arg == "none" || p_arg == "0") {
        if (api->db_del) api->db_del(ctx, "aki_proxy");
        api->c_fmt(ctx, "🛑 Прокси/Cloudflare Worker для Акинатора отключён (работает встроенный движок).", KOTO_ENT_BOLD);
    } else {
        if (api->db_set) api->db_set(ctx, "aki_proxy", p_arg.c_str());
        std::string msg = "✅ Прокси/Cloudflare Worker успешно установлен:\n<code>" + p_arg + "</code>";
        api->c_text(ctx, msg.c_str());
    }
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

void cmd_akistop(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id ? api->sender_id(ctx) : 0;
    AkiState st = load_state(ctx, api, cid, sender);
    clear_state(ctx, api, cid, sender, st);
    api->c_reset(ctx);
    api->c_fmt(ctx, "🛑 Игра в Акинатора завершена.", KOTO_ENT_BOLD);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Логика шага для встроенного гибридного движка ────────────────────────────
void handle_hybrid_step(koto_ctx* ctx, const koto_api* api, AkiState& st, long long target_cid, long long target_mid, bool force_continue_questions) {
    std::vector<double> probs;
    int best_idx = 0;
    double max_prob = 0.0;
    int best_contradictions = 0;
    compute_posteriors(st, probs, best_idx, max_prob, best_contradictions);

    // Прогресс от 5% до 97%
    int prog_val = std::min(97, std::max((int)st.history.size() * 7, (int)std::round(max_prob * 100.0)));
    st.prog = std::to_string(prog_val);

    int last_guess_step = st.slp.empty() ? 0 : std::atoi(st.slp.c_str());
    int steps_since_guess = (int)st.history.size() - last_guess_step;

    bool ready_to_guess = false;
    if (!force_continue_questions) {
        if (st.history.size() >= 5 && max_prob >= 0.58 && best_contradictions <= 1 && steps_since_guess >= 3) {
            ready_to_guess = true;
        } else if (st.history.size() >= 10 && max_prob >= 0.32 && steps_since_guess >= 4) {
            ready_to_guess = true;
        } else if (st.history.size() >= 14 && steps_since_guess >= 4) {
            ready_to_guess = true;
        }
    }

    int next_q = select_next_question(st, probs);
    if (next_q < 0) {
        ready_to_guess = true;
    }

    if (ready_to_guess) {
        st.slp = std::to_string(st.history.size());
        bool is_ru = (st.lang == "ru");

        // Если лучший кандидат в локальной базе имеет противоречия с ответами игрока (или уже было отклонение),
        // обращаемся к AI для дедукции любого персонажа в мире!
        std::string ai_name, ai_desc;
        if ((best_contradictions >= 2 || !st.excluded.empty()) && query_ai_guess(ctx, api, st, ai_name, ai_desc)) {
            st.guess_name = ai_name;
            st.guess_desc = ai_desc;
        } else {
            const auto& ch = CHARACTERS[best_idx];
            st.guess_name = is_ru ? ch.name_ru : ch.name_en;
            st.guess_desc = is_ru ? ch.desc_ru : ch.desc_en;
        }
        st.excluded.push_back(st.guess_name);
        save_state(ctx, api, target_cid, st);
        render_guess(ctx, api, st);
        api->c_edit_message(ctx, target_cid, target_mid, 0);
        return;
    }

    st.cur_q_idx = next_q;
    st.last_q = (st.lang == "ru") ? QUESTIONS[next_q].ru : QUESTIONS[next_q].en;
    st.num = (int)st.history.size() + 1;
    save_state(ctx, api, target_cid, st);
    render_question(ctx, api, st);
    api->c_edit_message(ctx, target_cid, target_mid, 0);
}

// ── Callback Handler ─────────────────────────────────────────────────────────

// ── Command: .aiaki (Режим ИИ на базе GPT-4o-mini / FreeGPT) ────────────────
void cmd_aiaki(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id ? api->sender_id(ctx) : 0;
    long long cmd_mid = api->msg_id ? api->msg_id(ctx) : 0;

    AkiState st;
    st.lang = "ru";
    st.session = "ai";
    st.chat_id = cid;
    st.owner_id = sender;
    st.msg_id = (cmd_mid > 0) ? (cmd_mid + 1) : 0;
    st.num = 1;
    st.prog = "0";
    st.cur_q_idx = 0;
    st.last_q = QUESTIONS[0].ru;

    save_state(ctx, api, cid, st);
    render_question(ctx, api, st);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* raw_data = api->cb_data(ctx);
    if (!raw_data || std::strncmp(raw_data, "aki:", 4) != 0) return;

    std::string data = raw_data;
    long long cid = api->chat_id(ctx);
    long long mid = api->cb_message_id(ctx);
    long long sender = api->sender_id ? api->sender_id(ctx) : 0;

    AkiState st = load_state(ctx, api, cid, sender);
    if (st.session.empty()) {
        api->cb_answer(ctx, "⚠️ Игра не найдена. Запустите новую командой .aki", 1);
        return;
    }

    if (st.owner_id != 0 && sender != 0 && sender != st.owner_id) {
        api->cb_answer(ctx, st.lang == "ru" ? "⛔ Это игра другого игрока!" : "⛔ This is another player's game!", 1);
        return;
    }

    long long target_cid = (cid != 0) ? cid : st.chat_id;
    long long target_mid = (mid != 0) ? mid : st.msg_id;
    if (mid != 0 && st.msg_id != mid) {
        st.msg_id = mid;
    }

    
    if (data == "aki:mode:ai") {
        st.session = "ai";
        api->cb_answer(ctx, st.lang == "ru" ? "🤖 Переключено на ИИ (GPT-4o)" : "🤖 Switched to AI (GPT-4o)", 0);
        save_state(ctx, api, target_cid, st);
        render_question(ctx, api, st);
        api->c_edit_message(ctx, target_cid, target_mid, 0);
        return;
    }

    if (data == "aki:mode:off") {
        api->cb_answer(ctx, st.lang == "ru" ? "🌐 Перезапуск в официальном режиме…" : "🌐 Restarting in official mode…", 0);
        clear_state(ctx, api, target_cid, sender, st);
        cmd_akinator(ctx, api);
        return;
    }

    if (data == "aki:stop") {
        clear_state(ctx, api, target_cid, sender, st);
        api->cb_answer(ctx, st.lang == "ru" ? "Игра завершена" : "Game ended", 0);
        api->c_reset(ctx);
        api->c_fmt(ctx, st.lang == "ru" ? "🛑 Игра в Акинатора завершена." : "🛑 Akinator game ended.", KOTO_ENT_BOLD);
        api->c_edit_message(ctx, target_cid, target_mid, 0);
        return;
    }

    if (data == "aki:back") {
        if (st.session == "hybrid" || st.session == "ai") {
            if (!st.history.empty()) {
                st.history.pop_back();
                st.num = std::max(1, (int)st.history.size() + 1);
                handle_hybrid_step(ctx, api, st, target_cid, target_mid, false);
            } else {
                api->cb_answer(ctx, st.lang == "ru" ? "Вы на первом вопросе" : "Already at first question", 0);
            }
            return;
        }

        api->cb_answer(ctx, st.lang == "ru" ? "Шаг назад…" : "Going back…", 0);
        std::string url = build_aki_url(ctx, api, st.lang, "cancel_answer");
        std::string body = "step=" + url_encode(st.step) +
                           "&progression=" + url_encode(st.prog) +
                           "&sid=1&cm=" + std::string(st.child_mode ? "true" : "false") +
                           "&session=" + url_encode(st.session);
        std::string headers = aki_ajax_headers(st.lang);
        std::string resp = http_post_req(ctx, api, url, body, headers);
        try {
            json j = json::parse(resp);
            if (j.contains("question") && !j["question"].is_null()) {
                st.last_q = html_unescape(j.value("question", ""));
                if (j.contains("progression")) {
                    double p = j["progression"].is_number() ? j["progression"].get<double>() : std::stod(j.value("progression", "0"));
                    st.prog = std::to_string((int)std::round(p));
                }
                if (j.contains("step")) {
                    st.step = j["step"].is_string() ? j["step"].get<std::string>() : std::to_string(j["step"].get<int>());
                }
                if (st.num > 1) st.num--;
                save_state(ctx, api, target_cid, st);
                render_question(ctx, api, st);
                api->c_edit_message(ctx, target_cid, target_mid, 0);
                return;
            }
        } catch (...) {}
        api->cb_answer(ctx, st.lang == "ru" ? "Нельзя вернуться назад" : "Cannot go back", 0);
        return;
    }

    if (data == "aki:win") {
        clear_state(ctx, api, target_cid, sender, st);
        api->cb_answer(ctx, st.lang == "ru" ? "Победа Акинатора!" : "Akinator won!", 0);
        api->c_reset(ctx);
        std::string win_text = (st.lang == "ru") ? "🎉 Ура! Я снова угадал персонажа!\n\n" : "🎉 Hurray! I guessed your character again!\n\n";
        api->c_fmt(ctx, win_text.c_str(), KOTO_ENT_BOLD);
        api->c_fmt(ctx, st.guess_name.c_str(), KOTO_ENT_CODE);
        if (!st.guess_desc.empty()) {
            api->c_text(ctx, "\n");
            api->c_fmt(ctx, st.guess_desc.c_str(), KOTO_ENT_ITALIC);
        }
        api->c_text(ctx, "\n\n");
        api->c_fmt(ctx, "by kote", KOTO_ENT_ITALIC);
        api->c_edit_message(ctx, target_cid, target_mid, 0);
        return;
    }

    // Встроенный гибридный движок
    if (st.session == "hybrid" || st.session == "ai") {
        if (data == "aki:rej") {
            api->cb_answer(ctx, st.lang == "ru" ? "Продолжаем поиск…" : "Continuing search…", 0);
            if (st.history.size() >= 22) {
                clear_state(ctx, api, target_cid, sender, st);
                api->c_reset(ctx);
                api->c_fmt(ctx, st.lang == "ru" ? "🏆 Сдаюсь! Вы загадали слишком редкого персонажа и победили Акинатора!\n\n" : "🏆 I give up! You beat Akinator!\n\n", KOTO_ENT_BOLD);
                api->c_fmt(ctx, "by kote", KOTO_ENT_ITALIC);
                api->c_edit_message(ctx, target_cid, target_mid, 0);
                return;
            }
            handle_hybrid_step(ctx, api, st, target_cid, target_mid, true);
            return;
        }

        if (data.rfind("aki:ans:", 0) == 0) {
            int ans_id = std::atoi(data.substr(8).c_str());
            api->cb_answer(ctx, st.lang == "ru" ? "Ответ принят" : "Answer received", 0);

            QAEntry entry;
            entry.q_idx = st.cur_q_idx;
            entry.q_text = st.last_q;
            entry.ans = ans_id;
            st.history.push_back(entry);

            handle_hybrid_step(ctx, api, st, target_cid, target_mid, false);
            return;
        }
    }

    // Режим внешнего прокси (если был запущен через .akiproxy)
    if (data == "aki:rej") {
        api->cb_answer(ctx, st.lang == "ru" ? "Продолжаем поиск…" : "Continuing search…", 0);
        std::string url = build_aki_url(ctx, api, st.lang, "exclude");
        std::string body = "step=" + url_encode(st.step) +
                           "&progression=" + url_encode(st.prog) +
                           "&sid=1&cm=" + std::string(st.child_mode ? "true" : "false") +
                           "&session=" + url_encode(st.session) +
                           "&forward_answer=1";
        std::string headers = aki_ajax_headers(st.lang);
        std::string resp = http_post_req(ctx, api, url, body, headers);

        try {
            json j = json::parse(resp);
            if (j.contains("id_proposition") && !j["id_proposition"].is_null()) {
                st.guess_name = html_unescape(j.value("name_proposition", ""));
                st.guess_desc = html_unescape(j.value("description_proposition", ""));
                st.guess_photo = j.value("photo", "");
                save_state(ctx, api, target_cid, st);
                render_guess(ctx, api, st);
                api->c_edit_message(ctx, target_cid, target_mid, 0);
                return;
            } else if (j.contains("question") && !j["question"].is_null()) {
                st.last_q = html_unescape(j.value("question", ""));
                if (j.contains("progression")) {
                    double p = j["progression"].is_number() ? j["progression"].get<double>() : std::stod(j.value("progression", "0"));
                    st.prog = std::to_string((int)std::round(p));
                }
                if (j.contains("step")) {
                    st.step = j["step"].is_string() ? j["step"].get<std::string>() : std::to_string(j["step"].get<int>());
                }
                st.num++;
                save_state(ctx, api, target_cid, st);
                render_question(ctx, api, st);
                api->c_edit_message(ctx, target_cid, target_mid, 0);
                return;
            }
        } catch (...) {}

        clear_state(ctx, api, target_cid, sender, st);
        api->c_reset(ctx);
        api->c_fmt(ctx, st.lang == "ru" ? "😔 Не удалось угадать персонажа. Вы победили Акинатора!" : "😔 Couldn't guess the character. You won!", KOTO_ENT_BOLD);
        api->c_edit_message(ctx, target_cid, target_mid, 0);
        return;
    }

    if (data.rfind("aki:ans:", 0) == 0) {
        std::string ans_id = data.substr(8);
        api->cb_answer(ctx, st.lang == "ru" ? "Ответ принят" : "Answer received", 0);

        std::string url = build_aki_url(ctx, api, st.lang, "answer");
        std::string body = "step=" + url_encode(st.step) +
                           "&progression=" + url_encode(st.prog) +
                           "&sid=1&cm=" + std::string(st.child_mode ? "true" : "false") +
                           "&answer=" + url_encode(ans_id) +
                           "&step_last_proposition=" + url_encode(st.slp) +
                           "&num_question=" + std::to_string(st.num) +
                           "&session=" + url_encode(st.session);
        std::string headers = aki_ajax_headers(st.lang);
        std::string resp = http_post_req(ctx, api, url, body, headers);

        try {
            json j = json::parse(resp);
            if (j.value("completion", "") == "KO") {
                clear_state(ctx, api, target_cid, sender, st);
                api->c_reset(ctx);
                api->c_fmt(ctx, st.lang == "ru" ? "😔 Не удалось угадать персонажа. Вы победили Акинатора!" : "😔 Couldn't guess the character. You beat Akinator!", KOTO_ENT_BOLD);
                api->c_edit_message(ctx, target_cid, target_mid, 0);
                return;
            }

            if (j.contains("id_proposition") && !j["id_proposition"].is_null()) {
                st.guess_name = html_unescape(j.value("name_proposition", ""));
                st.guess_desc = html_unescape(j.value("description_proposition", ""));
                st.guess_photo = j.value("photo", "");
                st.slp = std::to_string(st.num);
                if (j.contains("step")) {
                    st.step = j["step"].is_string() ? j["step"].get<std::string>() : std::to_string(j["step"].get<int>());
                }
                save_state(ctx, api, target_cid, st);
                render_guess(ctx, api, st);
                api->c_edit_message(ctx, target_cid, target_mid, 0);
                return;
            }

            if (j.contains("question") && !j["question"].is_null()) {
                st.last_q = html_unescape(j.value("question", ""));
                if (j.contains("progression")) {
                    double p = j["progression"].is_number() ? j["progression"].get<double>() : std::stod(j.value("progression", "0"));
                    st.prog = std::to_string((int)std::round(p));
                }
                if (j.contains("step")) {
                    st.step = j["step"].is_string() ? j["step"].get<std::string>() : std::to_string(j["step"].get<int>());
                }
                st.num++;
                save_state(ctx, api, target_cid, st);
                render_question(ctx, api, st);
                api->c_edit_message(ctx, target_cid, target_mid, 0);
                return;
            }
        } catch (...) {}

        // Если прокси отвалился во время игры — переключаемся на гибридный движок без потери игры
        st.session = "hybrid";
        handle_hybrid_step(ctx, api, st, target_cid, target_mid, false);
    }
}

// ── Settings & Commands ──────────────────────────────────────────────────────
const koto_setting_field SETTINGS[] = {
    {"lang", "Язык игры (ru / en)", KOTO_FIELD_CHOICE, "ru", "ru,en"},
    {"proxy", "Опциональный Cloudflare Worker URL (необязательно)", KOTO_FIELD_STR, "", nullptr}
};

const koto_command COMMANDS[] = {
    {"aki", cmd_akinator, KOTO_LEVEL_TRUSTED, "[ru] Запустить Официальный Акинатор (ru.akinator.com) [en] Start Official Akinator.", "[ru|en|--child|proxy <url>]"},
    {"akinator", cmd_akinator, KOTO_LEVEL_TRUSTED, "[ru] Запустить Официальный Акинатор (ru.akinator.com) [en] Start Official Akinator.", "[ru|en|--child|proxy <url>]"},
    {"aiaki", cmd_aiaki, KOTO_LEVEL_TRUSTED, "[ru] Запустить ИИ-Акинатор на базе GPT-4o-mini (без задержек и капчи) [en] Start AI Akinator (GPT-4o-mini).", "[ru|en]"},
    {"akiproxy", cmd_akiproxy, KOTO_LEVEL_TRUSTED, "[ru] Задать опциональный прокси для Акинатора [en] Set optional proxy for Akinator.", "<url|off>"},
    {"akistop", cmd_akistop, KOTO_LEVEL_TRUSTED, "[ru] Остановить текущую игру Акинатор [en] Stop current Akinator game.", nullptr}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "akinator",
    "[ru] Акинатор (Официальный сайт + ИИ GPT-4o) угадает любого персонажа по вопросам (by kote). [en] Akinator character guessing game (Official + AI) (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    nullptr, // on_watch
    0,       // watch_flags
    on_callback,
    SETTINGS,
    static_cast<int>(sizeof(SETTINGS) / sizeof(SETTINGS[0])),
    "https://github.com/AresUser1/KoteModules/blob/main/src/akinator.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
