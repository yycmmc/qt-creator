/****************************************************************************
**
** Copyright (C) 2020 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#pragma once

#include "utils_global.h"

#include <QString>

namespace Utils {

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
constexpr QString::SplitBehavior SkipEmptyParts = QString::SkipEmptyParts;
#else
constexpr Qt::SplitBehaviorFlags SkipEmptyParts = Qt::SkipEmptyParts;
#endif

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
using QHashValueType = uint;
#else
using QHashValueType = size_t;
#endif

// StringView - either QStringRef or QStringView
// Can be used where it is not possible to completely switch to QStringView
// For example where QString::splitRef / QStringView::split is essential.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
using StringView = QStringRef;
#else
using StringView = QStringView;
#endif
inline StringView make_stringview(const QString &s)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    return QStringRef(&s);
#else
    return QStringView(s);
#endif
}

} // namespace Utils
