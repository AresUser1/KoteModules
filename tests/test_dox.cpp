#include "module_abi.h"
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <regex>
#include <cassert>
#include <cctype>

// Вспомогательная функция для перевода UTF-8 (включая кириллицу) и ASCII в нижний регистр
static std::string utf8_tolower(const std::string& str) {
    std::string res;
    res.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if (c < 0x80) {
            res += static_cast<char>(tolower(c));
        } else if (c == 0xD0 && i + 1 < str.size()) {
            unsigned char c2 = static_cast<unsigned char>(str[i + 1]);
            if (c2 >= 0x90 && c2 <= 0x9F) {
                res += static_cast<char>(0xD0);
                res += static_cast<char>(c2 + 0x20);
                i++;
            } else if (c2 >= 0xA0 && c2 <= 0xAF) {
                res += static_cast<char>(0xD1);
                res += static_cast<char>(c2 - 0x20);
                i++;
            } else if (c2 == 0x81) {
                res += static_cast<char>(0xD1);
                res += static_cast<char>(0x91);
                i++;
            } else {
                res += str[i];
            }
        } else {
            res += str[i];
        }
    }
    return res;
}

static std::string extract_phone(const std::string& text, long long target_id) {
    std::string phone_number;
    std::string target_id_str = std::to_string(target_id);

    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        std::string llower = utf8_tolower(line);

        bool has_key = (
            llower.find("телефон") != std::string::npos ||
            llower.find("phone") != std::string::npos ||
            llower.find("тел:") != std::string::npos ||
            llower.find("тел.") != std::string::npos ||
            line.find("📞") != std::string::npos ||
            line.find("📱") != std::string::npos ||
            llower.find("номер телефона") != std::string::npos ||
            (llower.find("номер") != std::string::npos && llower.find("запрос") == std::string::npos)
        );

        if (has_key) {
            std::string digits_only;
            for (char c : line) {
                if (isdigit(static_cast<unsigned char>(c)) || c == '+') {
                    digits_only += c;
                }
            }
            std::regex reg(R"((\+?\d{7,15}))");
            std::smatch m;
            if (std::regex_search(digits_only, m, reg)) {
                std::string cand = m[1].str();
                std::string cand_digits;
                for (char ch : cand) if (isdigit((unsigned char)ch)) cand_digits += ch;
                if (cand_digits != target_id_str) {
                    phone_number = cand;
                    break;
                }
            }
        }
    }

    if (phone_number.empty()) {
        std::string clean_text;
        for (char c : text) {
            if (c != ' ' && c != '-' && c != '(' && c != ')' && c != '\r' && c != '\t') {
                clean_text += c;
            }
        }
        std::regex reg_all(R"((\+?\d{7,15}))");
        auto words_begin = std::sregex_iterator(clean_text.begin(), clean_text.end(), reg_all);
        auto words_end = std::sregex_iterator();
        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            std::string num = (*i)[1].str();
            std::string num_digits;
            for (char ch : num) if (isdigit((unsigned char)ch)) num_digits += ch;
            if (num != target_id_str && num_digits != target_id_str) {
                phone_number = num;
                break;
            }
        }
    }

    return phone_number;
}

int main() {
    int checks = 0;

    // 1. Извлечение номера из реального ответа бота Pribavkakzpbot
    std::string dossier = 
        "💬 ID: 6510232567\r\n"
        "\r\n"
        "📞 Телефон: 380662814396\r\n"
        "\r\n"
        "⏱️ История изменения имени:\r\n"
        "27.08.2026 -> @vlad1k_16_7, 6510232567\r\n";
    assert(extract_phone(dossier, 6510232567LL) == "380662814396");
    checks++;

    // 2. Промежуточное сообщение (меню направлений поиска) не должно давать номер
    std::string menu = "🔍 Обнаружен идентификатор: 6510232567\nВыберите направление для поиска:";
    assert(extract_phone(menu, 6510232567LL).empty());
    checks++;

    // 3. Промежуточное сообщение не должно считаться окончательным отказом (is_conclusive == false)
    std::string menu_lower = utf8_tolower(menu);
    bool conclusive_menu = (
        menu_lower.find("не найден") != std::string::npos ||
        menu_lower.find("ничего не найден") != std::string::npos ||
        menu_lower.find("нет данных") != std::string::npos ||
        menu_lower.find("информация отсутствует") != std::string::npos ||
        menu_lower.find("не удалось найти") != std::string::npos ||
        menu_lower.find("пользователь не найден") != std::string::npos ||
        menu_lower.find("ничего нет") != std::string::npos ||
        menu.find("История изменения") != std::string::npos ||
        menu.find("Интересовались") != std::string::npos ||
        menu.find("Контактные связи") != std::string::npos ||
        (menu.find("💬 ID:") != std::string::npos && menu.find("Телефон") == std::string::npos)
    );
    assert(!conclusive_menu);
    checks++;

    // 4. Явный отказ бота ("Ничего не найдено") корректно определяется как conclusive failure
    std::string not_found = "❌ По данному запросу ничего не найдено в базе.";
    std::string nf_lower = utf8_tolower(not_found);
    bool conclusive_nf = (
        nf_lower.find("не найден") != std::string::npos ||
        nf_lower.find("ничего не найден") != std::string::npos ||
        nf_lower.find("нет данных") != std::string::npos ||
        nf_lower.find("информация отсутствует") != std::string::npos ||
        nf_lower.find("не удалось найти") != std::string::npos ||
        nf_lower.find("пользователь не найден") != std::string::npos ||
        nf_lower.find("ничего нет") != std::string::npos ||
        not_found.find("История изменения") != std::string::npos ||
        not_found.find("Интересовались") != std::string::npos ||
        not_found.find("Контактные связи") != std::string::npos ||
        (not_found.find("💬 ID:") != std::string::npos && not_found.find("Телефон") == std::string::npos)
    );
    assert(conclusive_nf);
    checks++;

    // 5. Досье без телефона (только ID и история изменения)
    std::string dossier_no_phone = 
        "💬 ID: 99887766\n"
        "⏱️ История изменения имени:\n"
        "01.01.2025 -> @test, 99887766\n";
    assert(extract_phone(dossier_no_phone, 99887766LL).empty());
    bool conclusive_dossier_no_phone = (
        dossier_no_phone.find("История изменения") != std::string::npos ||
        (dossier_no_phone.find("💬 ID:") != std::string::npos && dossier_no_phone.find("Телефон") == std::string::npos)
    );
    assert(conclusive_dossier_no_phone);
    checks++;

    // 6. Форматированный номер с кодом страны и скобками
    std::string formatted_dossier = "📞 Телефон: +7 (999) 123-45-67\n";
    assert(extract_phone(formatted_dossier, 12345LL) == "+79991234567");
    checks++;

    std::cout << "Dox parser & state machine: " << checks << " checks passed" << std::endl;
    return 0;
}
