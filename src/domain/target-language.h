#pragma once

#include <QString>
#include <QVector>

namespace mnce {

struct TargetLanguage
{
    QString id;
    QString displayName;
    bool isDefault = false;
};

class TargetLanguageCatalog
{
public:
    explicit TargetLanguageCatalog(QVector<TargetLanguage> languages);

    static TargetLanguageCatalog builtIn();

    [[nodiscard]] const QVector<TargetLanguage>& languages() const;
    [[nodiscard]] bool contains(const QString& id) const;
    [[nodiscard]] QString defaultLanguageId() const;

private:
    QVector<TargetLanguage> languages_;
};

} // namespace mnce
