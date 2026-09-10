#include "domain/target-language.h"

#include <utility>

namespace mnce {

TargetLanguageCatalog::TargetLanguageCatalog(QVector<TargetLanguage> languages)
    : languages_(std::move(languages))
{
}

TargetLanguageCatalog TargetLanguageCatalog::builtIn()
{
    return TargetLanguageCatalog({
        {QStringLiteral("en"), QStringLiteral("英语"), true},
        {QStringLiteral("ja"), QStringLiteral("日语"), false},
    });
}

const QVector<TargetLanguage>& TargetLanguageCatalog::languages() const
{
    return languages_;
}

bool TargetLanguageCatalog::contains(const QString& id) const
{
    for (const auto& language : languages_) {
        if (language.id == id) {
            return true;
        }
    }
    return false;
}

QString TargetLanguageCatalog::defaultLanguageId() const
{
    for (const auto& language : languages_) {
        if (language.isDefault) {
            return language.id;
        }
    }
    return languages_.isEmpty() ? QString{} : languages_.constFirst().id;
}

} // namespace mnce
