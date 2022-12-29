/* Copyright (C) 2004-2022 Robert Griebl. All rights reserved.
**
** This file is part of BrickStore.
**
** This file may be distributed and/or modified under the terms of the GNU
** General Public License version 2 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://fsf.org/licensing/licenses/gpl.html for GPL licensing information.
*/
#include <QtCore/QBuffer>
#include <QtCore/QStringBuilder>
#include <QtCore/QThreadStorage>
#include <QtCore/QRegularExpression>
#include <QtCore/QBitArray>
#include <QtConcurrent/QtConcurrentMap>
#include <QtGui/QGuiApplication>
#include <QtGui/QFontMetrics>
#include <QtGui/QPixmap>
#include <QtGui/QImage>
#include <QtGui/QIcon>

#include "utility/utility.h"
#include "utility/stopwatch.h"
#include "bricklink/core.h"
#include "bricklink/category.h"
#include "bricklink/item.h"
#include "bricklink/picture.h"
#include "bricklink/model.h"
#include "bricklink/model_p.h"

#if defined(MODELTEST)
#  include <QAbstractItemModelTester>
#  define MODELTEST_ATTACH(x)   { (void) new QAbstractItemModelTester(x, QAbstractItemModelTester::FailureReportingMode::Warning, x); }
#else
#  define MODELTEST_ATTACH(x)   ;
#endif


namespace BrickLink {

/////////////////////////////////////////////////////////////
// COLORMODEL
/////////////////////////////////////////////////////////////

ColorModel::ColorModel(QObject *parent)
    : StaticPointerModel(parent)
{
    MODELTEST_ATTACH(this)
}

int ColorModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 1;
}

int ColorModel::pointerCount() const
{
    return int(core()->colors().size());
}

const void *ColorModel::pointerAt(int index) const
{
    return &core()->colors()[size_t(index)];
}

int ColorModel::pointerIndexOf(const void *pointer) const
{
    const auto &colors = core()->colors();
    auto d = static_cast<const Color *>(pointer) - colors.data();
    return (d >= 0 && d < int(colors.size())) ? int(d) : -1;
}

const Color *ColorModel::color(const QModelIndex &index) const
{
    return static_cast<const Color *>(pointer(index));
}

QModelIndex ColorModel::index(const Color *color) const
{
    return index(static_cast<const void *>(color));
}

QVariant ColorModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.column() != 0 || !color(index))
        return QVariant();

    QVariant res;
    const Color *c = color(index);

    if ((role == Qt::DisplayRole) || (role == Qt::EditRole)) {
        res = c->name();
    } else if (role == Qt::DecorationRole) {
        QFontMetricsF fm(QGuiApplication::font());
        QImage img = c->sampleImage(int(fm.height()) + 4, int(fm.height()) + 4);
        if (!img.isNull()) {
            QPixmap pix = QPixmap::fromImage(img);
            QIcon ico;
            ico.addPixmap(pix, QIcon::Normal);
            ico.addPixmap(pix, QIcon::Selected);
            res = ico;
        }
    } else if (role == ColorPointerRole) {
        res.setValue(c);
    }
    return res;
}

QVariant ColorModel::headerData(int section, Qt::Orientation orient, int role) const
{
    if ((orient == Qt::Horizontal) && (role == Qt::DisplayRole) && (section == 0))
        return tr("Color by %1").arg(sortOrder() == Qt::AscendingOrder ? tr("Name") : tr("Hue"));
    return QVariant();
}

bool ColorModel::isFiltered() const
{
    return m_colorTypeFilter || !qFuzzyIsNull(m_popularityFilter) || !m_colorListFilter.isEmpty();
}

void ColorModel::clearFilters()
{
    if (isFiltered()) {
        m_popularityFilter = 0;
        m_colorTypeFilter = Color::Type();
        m_colorListFilter.clear();
        emit colorTypeFilterChanged();
        emit popularityFilterChanged();
        emit colorListFilterChanged();
        invalidateFilter();
    }
}

Color::Type ColorModel::colorTypeFilter() const
{
    return m_colorTypeFilter;
}

void ColorModel::setColorTypeFilter(Color::Type type)
{
    if (type == m_colorTypeFilter)
        return;
    m_colorTypeFilter = type;
    emit colorTypeFilterChanged();
    invalidateFilter();
}

float ColorModel::popularityFilter() const
{
    return m_popularityFilter;
}

void ColorModel::setPopularityFilter(float p)
{
    if (qFuzzyCompare(p, m_popularityFilter))
        return;
    m_popularityFilter = p;
    emit popularityFilterChanged();
    invalidateFilter();
}

const QVector<const Color *> ColorModel::colorListFilter() const
{
    return m_colorListFilter;
}

void ColorModel::setColorListFilter(const QVector<const Color *> &colorList)
{
    if (colorList == m_colorListFilter)
        return;
    m_colorListFilter = colorList;
    emit colorListFilterChanged();
    invalidateFilter();
}

bool ColorModel::lessThan(const void *p1, const void *p2, int /*column*/) const
{
    const auto *c1 = static_cast<const Color *>(p1);
    const auto *c2 = static_cast<const Color *>(p2);

    if (!c1)
        return true;
    else if (!c2)
        return false;
    else {
        if (sortOrder() == Qt::AscendingOrder) {
            return (c1->name().localeAwareCompare(c2->name()) < 0);
        } else {
            int lh, rh, ls, rs, lv, rv, d;

            c1->color().getHsv(&lh, &ls, &lv);
            c2->color().getHsv(&rh, &rs, &rv);

            if (lh != rh)
                d = lh - rh;
            else if (ls != rs)
                d = ls - rs;
            else
                d = lv - rv;

            return d < 0;
        }
    }
}

bool ColorModel::filterAccepts(const void *pointer) const
{
    const auto *color = static_cast<const Color *>(pointer);

    if (m_colorTypeFilter && !(color->type() & m_colorTypeFilter))
        return false;
    if (!qFuzzyIsNull(m_popularityFilter) && (color->popularity() < m_popularityFilter))
        return false;
    if (!m_colorListFilter.isEmpty() && !m_colorListFilter.contains(color))
        return false;

    return true;
}



/////////////////////////////////////////////////////////////
// CATEGORYMODEL
/////////////////////////////////////////////////////////////

// this hack is needed since 0 means 'no selection at all'
const Category *CategoryModel::AllCategories = reinterpret_cast <const Category *>(-1);

CategoryModel::CategoryModel(QObject *parent)
    : StaticPointerModel(parent)
{
    MODELTEST_ATTACH(this)
}

int CategoryModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 1;
}

int CategoryModel::pointerCount() const
{
    return int(core()->categories().size() + 1);
}

const void *CategoryModel::pointerAt(int index) const
{
    return (index == 0) ? AllCategories : &core()->categories()[size_t(index) - 1];
}

int CategoryModel::pointerIndexOf(const void *pointer) const
{
    if (pointer == AllCategories) {
        return 0;
    } else {
        const auto &categories = core()->categories();
        auto d = static_cast<const Category *>(pointer) - categories.data();
        return (d >= 0 && d < int(categories.size())) ? int(d + 1) : -1;
    }
}

const Category *CategoryModel::category(const QModelIndex &index) const
{
    return static_cast<const Category *>(pointer(index));
}

QModelIndex CategoryModel::index(const Category *category) const
{
    return index(static_cast<const void *>(category));
}

QVariant CategoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.column() != 0 || !category(index))
        return QVariant();

    QVariant res;
    const Category *c = category(index);

    if (role == Qt::DisplayRole)
        res = c != AllCategories ? c->name() : tr("All Items");
    else if (role == CategoryPointerRole)
        res.setValue(c);
    return res;
}

QVariant CategoryModel::headerData(int section, Qt::Orientation orient, int role) const
{
    if ((orient == Qt::Horizontal) && (role == Qt::DisplayRole) && (section == 0))
        return tr("Category");
    return QVariant();
}

QHash<int, QByteArray> CategoryModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        { Qt::DisplayRole, "name" },
        { CategoryPointerRole, "categoryPointer" },
    };
    return roles;
}

bool CategoryModel::isFiltered() const
{
    return filterItemType() || filterWithoutInventory();
}

const ItemType *CategoryModel::filterItemType() const
{
    return m_itemtype_filter;
}

void CategoryModel::setFilterItemType(const ItemType *it)
{
    if (it == m_itemtype_filter)
        return;
    m_itemtype_filter = it;
    emit isFilteredChanged();
    invalidateFilter();
}

bool CategoryModel::filterWithoutInventory() const
{
    return m_inv_filter;
}

void CategoryModel::setFilterWithoutInventory(bool b)
{
    if (b == m_inv_filter)
        return;

    m_inv_filter = b;
    emit isFilteredChanged();
    invalidateFilter();
}

bool CategoryModel::lessThan(const void *p1, const void *p2, int /*column*/) const
{
    const auto *c1 = static_cast<const Category *>(p1);
    const auto *c2 = static_cast<const Category *>(p2);
    bool asc = (sortOrder() == Qt::AscendingOrder);

    if (!c1 || c1 == AllCategories)
        return asc;
    else if (!c2 || c2 == AllCategories)
        return !asc;
    else
        return c1->name().localeAwareCompare(c2->name()) < 0;
}

bool CategoryModel::filterAccepts(const void *pointer) const
{
    const auto *c = static_cast<const Category *>(pointer);

    if (!c)
        return false;
    else if (c == AllCategories)
        return true;
    else if (m_inv_filter && !c->hasInventories())
        return false;
    else if (m_itemtype_filter && !m_itemtype_filter->categories().contains(c))
        return false;
    else if (m_inv_filter && m_itemtype_filter && !c->hasInventories(m_itemtype_filter))
        return false;
    else
        return true;
}


/////////////////////////////////////////////////////////////
// ITEMTYPEMODEL
/////////////////////////////////////////////////////////////

ItemTypeModel::ItemTypeModel(QObject *parent)
    : StaticPointerModel(parent)
{
    MODELTEST_ATTACH(this)
}

int ItemTypeModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 1;
}

int ItemTypeModel::pointerCount() const
{
    return int(core()->itemTypes().size());
}

const void *ItemTypeModel::pointerAt(int index) const
{
    return &core()->itemTypes()[size_t(index)];
}

int ItemTypeModel::pointerIndexOf(const void *pointer) const
{
    const auto &itemTypes = core()->itemTypes();
    auto d = static_cast<const ItemType *>(pointer) - itemTypes.data();
    return (d >= 0 && d < int(itemTypes.size())) ? int(d) : -1;
}

const ItemType *ItemTypeModel::itemType(const QModelIndex &index) const
{
    return static_cast<const ItemType *>(pointer(index));
}

QModelIndex ItemTypeModel::index(const ItemType *itemtype) const
{
    return index(static_cast<const void *>(itemtype));
}

QVariant ItemTypeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.column() != 0 || !itemType(index))
        return QVariant();

    QVariant res;
    const ItemType *i = itemType(index);

    if (role == Qt::DisplayRole) {
        res = i->name();
    }
    else if (role == ItemTypePointerRole) {
        res.setValue(i);
    }
    return res;
}

QVariant ItemTypeModel::headerData(int section, Qt::Orientation orient, int role) const
{
    if ((orient == Qt::Horizontal) && (role == Qt::DisplayRole) && (section == 0))
        return tr("Name");
    return QVariant();
}

QHash<int, QByteArray> ItemTypeModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        { Qt::DisplayRole, "name" },
        { ItemTypePointerRole, "itemTypePointer" },
    };
    return roles;
}

bool ItemTypeModel::isFiltered() const
{
    return filterWithoutInventory();
}

bool ItemTypeModel::filterWithoutInventory() const
{
    return m_inv_filter;
}

void ItemTypeModel::setFilterWithoutInventory(bool b)
{
    if (b == m_inv_filter)
        return;

    m_inv_filter = b;
    emit isFilteredChanged();
    invalidateFilter();
}

bool ItemTypeModel::lessThan(const void *p1, const void *p2, int /*column*/) const
{
    const auto *i1 = static_cast<const ItemType *>(p1);
    const auto *i2 = static_cast<const ItemType *>(p2);

    return !i1 ? true : (!i2 ? false : i1->name().localeAwareCompare(i2->name()) < 0);
}

bool ItemTypeModel::filterAccepts(const void *pointer) const
{
    const auto *itemtype = static_cast<const ItemType *>(pointer);

    return !m_inv_filter || (itemtype && itemtype->hasInventories());
}



/////////////////////////////////////////////////////////////
// ITEMMODEL
/////////////////////////////////////////////////////////////

QString ItemModel::s_appearsInPrefix;
QString ItemModel::s_consistsOfPrefix;
QString ItemModel::s_idPrefix;


ItemModel::ItemModel(QObject *parent)
    : StaticPointerModel(parent)
{
    MODELTEST_ATTACH(this)

    if (s_consistsOfPrefix.isEmpty()) {
        s_consistsOfPrefix = tr("consists-of:", "Filter prefix");
        s_appearsInPrefix = tr("appears-in:", "Filter prefix");
        s_idPrefix = tr("id:", "Id prefix");
    }

    connect(core(), &Core::pictureUpdated, this, &ItemModel::pictureUpdated);
}

int ItemModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 3;
}

int ItemModel::pointerCount() const
{
    return int(core()->items().size());
}

const void *ItemModel::pointerAt(int index) const
{
    return &core()->items().at(size_t(index));
}

int ItemModel::pointerIndexOf(const void *pointer) const
{
    const auto &items = core()->items();
    auto d = static_cast<const Item *>(pointer) - items.data();
    return (d >= 0 && d < int(items.size())) ? int(d) : -1;
}

const Item *ItemModel::item(const QModelIndex &index) const
{
    return static_cast<const Item *>(pointer(index));
}

QModelIndex ItemModel::index(const Item *item) const
{
    return index(static_cast<const void *>(item));
}

QVariant ItemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !item(index))
        return QVariant();

    QVariant res;
    const Item *i = item(index);

    if (role == Qt::DisplayRole) {
        switch(index.column()) {
        case 1: res = QLatin1String(i->id()); break;
        case 2: res = i->name(); break;
        }
    } else if (role == Qt::TextAlignmentRole) {
        if (index.column() == 0) {
            return Qt::AlignCenter;
        }
    } else if (role == Qt::ToolTipRole || role == NameRole) {
        if (index.column() == 0)
            res = i->name();
    } else if (role == IdRole) {
        res = QLatin1String(i->id());
    } else if (role == ItemPointerRole) {
        res.setValue(i);
    } else if (role == ItemTypePointerRole) {
        res.setValue(i->itemType());
    } else if (role == CategoryPointerRole) {
        res.setValue(i->category());
    }
    return res;
}

QVariant ItemModel::headerData(int section, Qt::Orientation orient, int role) const
{
    if ((orient == Qt::Horizontal) && (role == Qt::DisplayRole)) {
        switch(section) {
        case 1: return tr("Item Id");
        case 2: return tr("Description");
        }
    }
    return QVariant();
}

QHash<int, QByteArray> ItemModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        { IdRole, "id" },
        { NameRole, "name" },
        { ItemPointerRole, "itemPointer" },
        { ItemTypePointerRole, "itemTypePointer" },
        { CategoryPointerRole, "categoryPointer" },
    };
    return roles;
}

void ItemModel::pictureUpdated(Picture *pic)
{
    if (!pic || !pic->item() || pic->color() != pic->item()->defaultColor())
        return;

    QModelIndex idx = index(pic->item());
    if (idx.isValid())
        emit dataChanged(idx, idx);
}

bool ItemModel::isFiltered() const
{
    return m_itemtype_filter || m_category_filter || m_color_filter
            || m_inv_filter || !m_text_filter.isEmpty();
}

const ItemType *ItemModel::filterItemType() const
{
    return m_itemtype_filter;
}

void ItemModel::setFilterItemType(const ItemType *it)
{
    if (it == m_itemtype_filter)
        return;

    m_itemtype_filter = it;
    emit isFilteredChanged();
    invalidateFilter();
}

const Category *ItemModel::filterCategory() const
{
    return m_category_filter;
}

void ItemModel::setFilterCategory(const Category *cat)
{
    if (cat == m_category_filter)
        return;
    m_category_filter = cat;
    emit isFilteredChanged();
    invalidateFilter();
}

const Color *ItemModel::filterColor() const
{
    return m_color_filter;
}

void ItemModel::setFilterColor(const Color *col)
{
    if (col == m_color_filter)
        return;
    m_color_filter = col;
    emit isFilteredChanged();
    invalidateFilter();
}

QString ItemModel::filterText() const
{
    return m_text_filter;
}

void ItemModel::setFilterText(const QString &filter)
{
    if (filter == m_text_filter)
        return;

    m_text_filter = filter;
    m_filter_text.clear();
    m_filter_appearsIn.clear();
    m_filter_consistsOf.clear();
    m_filter_ids.second.clear();
    m_filter_ids.first = false;

    const QStringList sl = filter.simplified().split(u' ');

    QString quoted;
    bool quotedNegate = false;

    for (const auto &s : sl) {
        if (s.isEmpty())
            continue;

        if (!quoted.isEmpty()) {
            quoted = quoted + u' ' + s;
            if (quoted.endsWith(u'"')) {
                quoted.chop(1);
                m_filter_text << qMakePair(quotedNegate, quoted);
                quoted.clear();
            }
        } else if (s.length() == 1) {
            // just a single character -> search for it literally
            m_filter_text << qMakePair(false, s);

        } else {
            const QChar first = s.at(0);
            const bool negate = (first == u'-');
            auto str = negate ? s.mid(1) : s;

            if (str.startsWith(s_consistsOfPrefix)) {
                str = str.mid(s_consistsOfPrefix.length());

                // contains either a minifig or a part, optionally with color-id
                const Color *color = nullptr;

                auto atPos = str.lastIndexOf(u'@');
                if (atPos != -1) {
                    color = core()->color(str.mid(atPos + 1).toUInt());
                    str = str.left(atPos);
                }

                if (auto item = core()->item("MP", str.toLatin1()))
                    m_filter_consistsOf << qMakePair(negate, qMakePair(item, color));

            } else if (str.startsWith(s_appearsInPrefix)) {
                str = str.mid(s_appearsInPrefix.length());

                // appears-in either a minifig or a set
                if (auto item = core()->item("MS", str.toLatin1()))
                    m_filter_appearsIn << qMakePair(negate, item);

            } else if (str.startsWith(s_idPrefix)) {
                str = str.mid(s_idPrefix.length());
                const auto ids = str.split(u","_qs);

                for (const auto &id : ids) {
                    if (auto item = core()->item("MPSG", id.toLatin1()))
                        m_filter_ids.second << item;
                }
                m_filter_ids.first = negate;

            } else {
                const bool firstIsQuote = str.startsWith(u"\"");
                const bool lastIsQuote = str.endsWith(u"\"");

                if (firstIsQuote && !lastIsQuote) {
                    quoted = str.mid(1);
                    quotedNegate = negate;
                } else if (firstIsQuote && lastIsQuote) {
                    m_filter_text << qMakePair(negate, str.mid(1, str.length() - 2));
                } else {
                    m_filter_text << qMakePair(negate, str);
                }
            }
        }
    }

    emit isFilteredChanged();
    invalidateFilter();
}

bool ItemModel::filterWithoutInventory() const
{
    return m_inv_filter;
}

void ItemModel::setFilterWithoutInventory(bool b)
{
    if (b == m_inv_filter)
        return;

    m_inv_filter = b;
    emit isFilteredChanged();
    invalidateFilter();
}

void ItemModel::setFilterYearRange(int minYear, int maxYear)
{
    if (m_year_min_filter == minYear && m_year_max_filter == maxYear)
        return;
    if (minYear && maxYear && minYear > maxYear)
        std::swap(minYear, maxYear);
    m_year_min_filter = minYear;
    m_year_max_filter = maxYear;
    invalidateFilter();
}

bool ItemModel::lessThan(const void *p1, const void *p2, int column) const
{
    const Item *i1 = static_cast<const Item *>(p1);
    const Item *i2 = static_cast<const Item *>(p2);

    return Utility::naturalCompare((column == 2) ? i1->name() : QLatin1String(i1->id()),
                                   (column == 2) ? i2->name() : QLatin1String(i2->id())) < 0;
}

bool ItemModel::filterAccepts(const void *pointer) const
{
    const Item *item = static_cast<const Item *>(pointer);

    if (!item)
        return false;
    else if (m_itemtype_filter && item->itemType() != m_itemtype_filter)
        return false;
    else if (m_category_filter && (m_category_filter != CategoryModel::AllCategories) && !item->additionalCategories(true).contains(m_category_filter))
        return false;
    else if (m_inv_filter && !item->hasInventory())
        return false;
    else if (m_color_filter && !item->hasKnownColor(m_color_filter))
        return false;
    else if (m_year_min_filter && (!item->yearReleased() || (item->yearReleased() < m_year_min_filter)))
        return false;
    else if (m_year_max_filter && (!item->yearLastProduced() || (item->yearLastProduced() > m_year_max_filter)))
        return false;
    else {
        const QString matchStr = QLatin1String(item->id()) + u' ' + item->name();

        // .first is always "bool negate"

        bool match = true;
        for (const auto &p : m_filter_text)
            match = match && (matchStr.contains(p.second, Qt::CaseInsensitive) == !p.first); // contains() xor negate

        bool idMatched = m_filter_ids.second.isEmpty();
        for (const auto &i : m_filter_ids.second) {
            if (i == item) {
                idMatched = true;
                break;
            }
        }
        match = match && (idMatched == !m_filter_ids.first); // found xor negate

        for (const auto &a : m_filter_appearsIn) {
            bool found = false;
            const auto appearsvec = item->appearsIn();
            for (const AppearsInColor &vec : appearsvec) {
                for (const AppearsInItem &ai : vec) {
                    if (ai.second == a.second) {
                        found = true;
                        break;
                    }
                }
                if (found)
                    break;
            }
            match = match && (found == !a.first); // found xor negate
        }
        for (const auto &c : m_filter_consistsOf) {
            bool found = false;
            const auto &containslist = item->consistsOf();
            for (const auto &ci : containslist) {
                if ((ci.item() == c.second.first)
                        && (!c.second.second || (ci.color() == c.second.second))) {
                    found = true;
                    break;
                }
            }
            match = match && (found == !c.first); // found xor negate
        }

        return match;
    }
}


/////////////////////////////////////////////////////////////
// INTERNAL INVENTORYMODEL
/////////////////////////////////////////////////////////////


InternalInventoryModel::InternalInventoryModel(Mode mode, const QVector<SimpleLot> &list, QObject *parent)
    : QAbstractTableModel(parent)
    , m_mode(mode)
{
    MODELTEST_ATTACH(this)

    switch (mode) {
        case Mode::ConsistsOf: fillConsistsOf(list); break;
        case Mode::AppearsIn:  fillAppearsIn(list); break;
        case Mode::CanBuild:   fillCanBuild(list); break;
    }
    connect(core(), &Core::pictureUpdated, this, [this](Picture *pic) {
        if (!pic || !pic->item() || pic->color() != pic->item()->defaultColor())
            return;

        for (int row = 0; row < m_items.size(); ++row) {
            if (m_items.at(row).m_item == pic->item()) {
                QModelIndex idx = index(row, InventoryModel::PictureColumn);
                emit dataChanged(idx, idx);
            }
        }
    });
}

void InternalInventoryModel::fillConsistsOf(const QVector<SimpleLot> &list)
{
    QHash<std::pair<const Item *, const Color *>, Entry> unique;

    for (const auto &p : list) {
        if (!p.m_item)
            continue;

        if (!p.m_item->hasInventory())
            continue;

        const auto consistsvec = p.m_item->consistsOf();

        for (const auto &coi : consistsvec) {
            if (coi.isExtra() || coi.isCounterPart() || coi.isAlternate())
                continue;

            const auto *partItem = coi.item();
            const auto *partColor = coi.color();

            if (p.m_color && p.m_color->id() && partItem->itemType()->hasColors()
                    && partColor && (partColor->id() == 0)) {
                partColor = p.m_color;
            }

            const auto key = std::make_pair(partItem, partColor);

            auto it = unique.find(key);
            if (it != unique.end())
                it.value().m_quantity += coi.quantity();
            else
                unique.emplace(key, partItem, partColor, coi.quantity());
        }
    }

    m_items = unique.values();
}

void InternalInventoryModel::fillAppearsIn(const QVector<SimpleLot> &list)
{
    QHash<std::pair<const Item *, const Color *>, Entry> unique;
    bool first_item = true;
    bool single_item = (list.count() == 1);

    for (const auto &p : list) {
        if (!p.m_item)
            continue;

        const auto appearsvec = p.m_item->appearsIn(p.m_color);
        for (const AppearsInColor &vec : appearsvec) {
            for (const AppearsInItem &aii : vec) {
                const auto key = std::make_pair(aii.second, nullptr);

                if (single_item) {
                    m_items.emplace_back(aii.second, nullptr, aii.first);
                } else {
                    auto it = unique.find(key);
                    if (it != unique.end())
                        ++it->m_quantity;
                    else if (first_item)
                        unique.emplace(key, aii.second, nullptr, 1);
                }
            }
        }
        first_item = false;
    }

    for (auto it = unique.begin(); it != unique.end(); ++it) {
        if (it->m_quantity >= list.count())
            m_items.emplace_back(it->m_item, nullptr, -1);
    }
}

void InternalInventoryModel::fillCanBuild(const QVector<SimpleLot> &lots)
{
    QVector<std::pair<quint32, qint32>> have;
    have.reserve(lots.size());
    for (const auto &lot : lots) {
        if (!lot.m_item || !lot.m_color || lot.m_quantity <= 0)
            continue;

        // squeeze the data as tightly as possible: this map/reduce is quite CPU intensive and
        // comparing 32 bits instead of 128 for each check helps a lot (plus is also keeps more
        // data in the cache, as each entry is only 64 bits instead of 192).
        have.append({ (quint32(lot.m_color->index()) << 20) | quint32(lot.m_item->index()),
                     lot.m_quantity });
    }

    static auto indexCompare = [](const std::pair<quint32, qint32> &a,
            const std::pair<quint32, qint32> &b) -> bool {
        return a.first < b.first;
    };

    // sort by colorindex | itemindex
    std::sort(have.begin(), have.end(), indexCompare);

    auto map = [=](const Item &set) -> const Item * {
        bool matched = false;
        if (set.hasInventory() && set.itemTypeId() == 'S') {
            const QVector<Item::ConsistsOf> &inv = set.consistsOf();

            // copy the have vector, as we need to modify it for counting down quantities
            auto checkHave = have;

            QBitArray alternatesMatched;

            for (const auto &co : inv) {
                if (co.isExtra() || co.isCounterPart())
                    continue;

                uint alternate = co.alternateId();
                if (alternate) {
                    if (alternatesMatched.size() < alternate)
                        alternatesMatched.resize(alternate);
                    else if (alternatesMatched.at(alternate - 1))
                        continue;
                }

                quint32 index = (co.colorIndex() << 20) | co.itemIndex();
                auto it = std::lower_bound(checkHave.begin(), checkHave.end(),
                                           std::make_pair(index, 0), indexCompare);
                if ((it != checkHave.end()) && (it->first == index)) {
                    it->second -= co.quantity();
                    if (it->second >= 0) {
                        matched = true;
                        if (alternate)
                            alternatesMatched.setBit(alternate - 1);
                        continue;
                    }
                }
                if (!alternate) {
                    matched = false;
                    break;
                }
            }

            // if we had alternatives, make sure all of them matched up
            if (matched && !alternatesMatched.isEmpty())
                matched = (alternatesMatched.count(true) == alternatesMatched.count());
        }
        return matched ? &set : nullptr;
    };

    const auto &sets = core()->items();
    QtConcurrent::mapped(sets.cbegin(), sets.cend(), map)
            .then(this, [this](QFuture<const BrickLink::Item *> future) {
        if (!m_items.isEmpty() || (m_mode != Mode::CanBuild))
            return;

        beginResetModel();
        for (const auto *match : std::as_const(future)) {
            if (match)
                m_items.emplace_back(match, nullptr, -1);
        }
        endResetModel();
    });
}

QModelIndex InternalInventoryModel::index(int row, int column, const QModelIndex &parent) const
{
    if (hasIndex(row, column, parent) && !parent.isValid())
        return createIndex(row, column, row);
    return {};
}

InternalInventoryModel::Entry InternalInventoryModel::entry(const QModelIndex &idx) const
{
    return idx.isValid() ? m_items.at(idx.row()) : Entry { };
}

int InternalInventoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

int InternalInventoryModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : InventoryModel::ColumnCount;
}

QVariant InternalInventoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return { };

    const Entry e = m_items.at(index.row());

    if (!e.m_item)
        return { };

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case InventoryModel::QuantityColumn:
            return (e.m_quantity < 0) ? u"-"_qs : QString::number(e.m_quantity);
        case InventoryModel::ItemIdColumn:
            return QString(QChar::fromLatin1(e.m_item->itemTypeId()) + u' ' + QString::fromLatin1(e.m_item->id()));
        case InventoryModel::ItemNameColumn:
            return e.m_item->name();
        case InventoryModel::ColorColumn:
            return e.m_color ? e.m_color->name() : u"-"_qs;
        default: return { };
        }
    case Qt::DecorationRole:
        switch (index.column()) { //TODO: cache and size
        case InventoryModel::ColorColumn:
            return e.m_color ? e.m_color->sampleImage(20, 20) : QImage { };
        default:
            return { };
        }
    case ItemPointerRole:
        return (index.column() != InventoryModel::ColorColumn)
                ? QVariant::fromValue(e.m_item) : QVariant { };
    case ColorPointerRole:
        return QVariant::fromValue((m_mode == Mode::ConsistsOf)
                                   ? e.m_color : e.m_item->defaultColor());
    case QuantityRole:
        return qMax(0, e.m_quantity);
    case NameRole:
        return e.m_item->name();
    case IdRole:
        return e.m_item->id();
    case ColorNameRole:
        return e.m_color ? e.m_color->name() : QString { };
    default:
        return { };
    }
}

QVariant InternalInventoryModel::headerData(int section, Qt::Orientation orient, int role) const
{
    if ((orient == Qt::Horizontal) && (role == Qt::DisplayRole)) {
        switch (section) {
        case InventoryModel::QuantityColumn: return tr("Qty.");
        case InventoryModel::ColorColumn:    return tr("Color");
        case InventoryModel::ItemIdColumn:   return tr("Item Id");
        case InventoryModel::ItemNameColumn: return tr("Description");
        }
    }
    return QVariant();
}

QHash<int, QByteArray> InternalInventoryModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        { IdRole, "id" },
        { NameRole, "name" },
        { ColorNameRole, "colorName" },
        { QuantityRole, "quantity" },
        { ItemPointerRole, "itemPointer" },
        { ColorPointerRole, "colorPointer" },
    };
    return roles;
}


/////////////////////////////////////////////////////////////
// INVENTORY MODEL
/////////////////////////////////////////////////////////////

InventoryModel::InventoryModel(Mode mode, const QVector<SimpleLot> &simpleLots, QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setSourceModel(new InternalInventoryModel(mode, simpleLots, this));
}

int InventoryModel::count() const
{
    return rowCount();
}

bool InventoryModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    // the indexes are from the source model, so the internal pointers are valid
    // this is faster than fetching the Category* via data()/QVariant marshalling
    const auto *iim = static_cast<const InternalInventoryModel *>(sourceModel());
    const auto e1 = iim->entry(left);
    const auto e2 = iim->entry(right);

    if (!e1.m_item)
        return true;
    else if (!e2.m_item)
        return false;
    else {
        switch (left.column()) {
        default:
        case InventoryModel::QuantityColumn:
            return e1.m_quantity < e2.m_quantity;
        case InventoryModel::ItemIdColumn:
            return (Utility::naturalCompare(QLatin1String(e1.m_item->id()),
                                            QLatin1String(e2.m_item->id())) < 0);
        case InventoryModel::ItemNameColumn:
            return (Utility::naturalCompare(e1.m_item->name(), e2.m_item->name()) < 0);
        case InventoryModel::ColorColumn:
            return !e1.m_color ? true : (!e2.m_color ? false : e1.m_color->name() < e2.m_color->name());
        }
    }
}

InternalInventoryModel::Entry::Entry(const Item *item, const Color *color, int quantity)
    : m_item(item)
    , m_color(color)
    , m_quantity(quantity)
{ }

} // namespace BrickLink

#include "moc_model.cpp"
