/****************************************************************************
** Copyright (c) 2021, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#pragma once

#include "property.h"
#include "span.h"
#include "string_metadata.h"

#include <string_view>
#include <vector>

namespace Mayo {

class PropertyArrayOfStringMetaData : public Property {
public:
    PropertyArrayOfStringMetaData(PropertyGroup* grp, const TextId& name);

    Span<const StringMetaData> get() const { return m_vecStringMetaData; }
    int indexOf(std::string_view name) const;
    const std::string& valueOf(std::string_view name) const;

    void add(StringMetaData data);
    void add(std::string_view name, std::string_view value);
    void changeValue(int index, std::string_view value);
    void changeValue(std::string_view name, std::string_view value);
    void erase(int index);
    void erase(std::string_view name);
    void clear();

    const char* dynTypeName() const override { return PropertyArrayOfStringMetaData::TypeName; }
    static const char TypeName[];

private:

    std::vector<StringMetaData> m_vecStringMetaData;
};

} // namespace Mayo
