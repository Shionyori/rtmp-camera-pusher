#pragma once

#include <QString>

namespace AppLocale {

enum class Language {
    English = 0,
    Chinese = 1,
};

void apply(Language language);
Language fromUiIndex(int index);
int toUiIndex(Language language);
QString localizeLogMessage(const QString& message, Language language);

} // namespace AppLocale
