#include "domain/natural-sort.h"

#include <QString>

namespace mnce {
namespace {

int compareText(QStringView left, QStringView right)
{
    return QString::compare(left.toString().toCaseFolded(),
                            right.toString().toCaseFolded(), Qt::CaseSensitive);
}

int compareNumber(QStringView left, QStringView right)
{
    qsizetype leftNonZero = 0;
    while (leftNonZero < left.size() && left.at(leftNonZero).digitValue() == 0) {
        ++leftNonZero;
    }
    qsizetype rightNonZero = 0;
    while (rightNonZero < right.size() && right.at(rightNonZero).digitValue() == 0) {
        ++rightNonZero;
    }
    const auto leftSignificant = left.sliced(leftNonZero);
    const auto rightSignificant = right.sliced(rightNonZero);
    if (leftSignificant.size() != rightSignificant.size()) {
        return leftSignificant.size() < rightSignificant.size() ? -1 : 1;
    }
    for (qsizetype index = 0; index < leftSignificant.size(); ++index) {
        const int leftDigit = leftSignificant.at(index).digitValue();
        const int rightDigit = rightSignificant.at(index).digitValue();
        if (leftDigit != rightDigit) {
            return leftDigit < rightDigit ? -1 : 1;
        }
    }
    return 0;
}

} // namespace

int compareNatural(QStringView left, QStringView right)
{
    qsizetype leftIndex = 0;
    qsizetype rightIndex = 0;
    while (leftIndex < left.size() && rightIndex < right.size()) {
        const bool leftIsDigit = left.at(leftIndex).isDigit();
        const bool rightIsDigit = right.at(rightIndex).isDigit();
        if (leftIsDigit && rightIsDigit) {
            qsizetype leftEnd = leftIndex;
            while (leftEnd < left.size() && left.at(leftEnd).isDigit()) {
                ++leftEnd;
            }
            qsizetype rightEnd = rightIndex;
            while (rightEnd < right.size() && right.at(rightEnd).isDigit()) {
                ++rightEnd;
            }
            const int result = compareNumber(left.sliced(leftIndex, leftEnd - leftIndex),
                                             right.sliced(rightIndex, rightEnd - rightIndex));
            if (result != 0) {
                return result;
            }
            leftIndex = leftEnd;
            rightIndex = rightEnd;
            continue;
        }

        qsizetype leftEnd = leftIndex;
        while (leftEnd < left.size() && !left.at(leftEnd).isDigit()) {
            ++leftEnd;
        }
        qsizetype rightEnd = rightIndex;
        while (rightEnd < right.size() && !right.at(rightEnd).isDigit()) {
            ++rightEnd;
        }
        const int result = compareText(left.sliced(leftIndex, leftEnd - leftIndex),
                                       right.sliced(rightIndex, rightEnd - rightIndex));
        if (result != 0) {
            return result;
        }
        leftIndex = leftEnd;
        rightIndex = rightEnd;
    }
    if (leftIndex == left.size() && rightIndex == right.size()) {
        return 0;
    }
    return leftIndex == left.size() ? -1 : 1;
}

} // namespace mnce
