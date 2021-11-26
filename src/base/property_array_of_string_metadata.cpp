/****************************************************************************
** Copyright (c) 2021, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "property_array_of_string_metadata.h"

namespace Mayo {

const char PropertyArrayOfStringMetaData::TypeName[] = "Mayo::PropertyArrayOfStringMetaData";

PropertyArrayOfStringMetaData::PropertyArrayOfStringMetaData(PropertyGroup* grp, const TextId& name)
    : Property(grp, name)
{
}

int PropertyArrayOfStringMetaData::indexOf(std::string_view name) const
{
    auto it = std::find_if(m_vecStringMetaData.begin(), m_vecStringMetaData.end(), [=](const StringMetaData& metadata) {
        return metadata.name == name;
    });
    return it != m_vecStringMetaData.end() ? int(it - m_vecStringMetaData.begin()) : -1;
}

const std::string& PropertyArrayOfStringMetaData::valueOf(std::string_view name) const
{
    static const std::string null;
    const int index = this->indexOf(name);
    return index != -1 ? m_vecStringMetaData.at(index).value : null;
}

void PropertyArrayOfStringMetaData::add(StringMetaData data)
{
    const int index = this->indexOf(data.name);
    if (index == -1) {
        this->notifyAboutToChange();
        m_vecStringMetaData.push_back(std::move(data));
        this->notifyChanged();
    }
    else {
        this->changeValue(index, data.value);
    }
}

void PropertyArrayOfStringMetaData::add(std::string_view name, std::string_view value)
{
    StringMetaData metaData;
    metaData.name = name;
    metaData.value = value;
    m_vecStringMetaData.push_back(std::move(metaData));
}

void PropertyArrayOfStringMetaData::changeValue(int index, std::string_view value)
{
    if (0 <= index && index <m_vecStringMetaData.size()) {
        this->notifyAboutToChange();
        m_vecStringMetaData[index].value = value;
        this->notifyChanged();
    }
}

void PropertyArrayOfStringMetaData::changeValue(std::string_view name, std::string_view value)
{
    this->changeValue(this->indexOf(name), value);
}

void PropertyArrayOfStringMetaData::erase(int index)
{
    if (0 <= index && index <m_vecStringMetaData.size()) {
        this->notifyAboutToChange();
        m_vecStringMetaData.erase(m_vecStringMetaData.begin() + index);
        this->notifyChanged();
    }
}

void PropertyArrayOfStringMetaData::erase(std::string_view name)
{
    this->erase(this->indexOf(name));
}

void PropertyArrayOfStringMetaData::clear()
{
    this->notifyAboutToChange();
    m_vecStringMetaData.clear();
    this->notifyChanged();
}

} // namespace Mayo
