/* Copyright (C) 2004-2021 Robert Griebl. All rights reserved.
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
#include <utility>
#include <algorithm>
#if defined(BS_HAS_PARALLEL_STL) && __has_include(<execution>)
#  include <execution>
#  if (__cpp_lib_execution >= 201603) && (__cpp_lib_parallel_algorithm >= 201603)
#    define AM_SORT_PARALLEL
#  endif
#endif

#include <QApplication>
#include <QCursor>
#include <QFileInfo>
#include <QDir>
#include <QStringBuilder>
#include <QtConcurrentFilter>
#include <QtAlgorithms>
#include <QStringListModel>

#if defined(MODELTEST)
#  include <QAbstractItemModelTester>
#  define MODELTEST_ATTACH(x)   { (void) new QAbstractItemModelTester(x, QAbstractItemModelTester::FailureReportingMode::Warning, x); }
#else
#  define MODELTEST_ATTACH(x)   ;
#endif

#include "utility/utility.h"
#include "config.h"
#include "utility/undo.h"
#include "utility/currency.h"
#include "utility/qparallelsort.h"
#include "document.h"
#include "document_p.h"
#include "utility/stopwatch.h"
#include "bricklink/bricklink_model.h"

using namespace std::chrono_literals;


template <auto G, auto S>
struct FieldOp
{
    template <typename Result> static Result returnType(Result (Lot::*)() const);
    typedef decltype(returnType(G)) R;

    static void copy(const Lot &from, Lot &to)
    {
        (to.*S)((from.*G)());
    }

    static bool merge(const Lot &from, Lot &to,
                      Document::MergeMode mergeMode = Document::MergeMode::Merge,
                      const R defaultValue = { })
    {
        if (mergeMode == Document::MergeMode::Ignore) {
            return false;
        } else if (mergeMode == Document::MergeMode::Copy) {
            copy(from, to);
            return true;
        } else {
            return mergeInternal(from, to, mergeMode, defaultValue);
        }
    }

private:
    template<class Q = R>
    static typename std::enable_if<!std::is_same<Q, double>::value && !std::is_same<Q, QString>::value, bool>::type
    mergeInternal(const Lot &from, Lot &to, Document::MergeMode mergeMode,
                  const R defaultValue)
    {
        Q_UNUSED(mergeMode)

        if (((from.*G)() != defaultValue) && ((to.*G)() == defaultValue)) {
            copy(from, to);
            return true;
        }
        return false;
    }

    template<class Q = R>
    static typename std::enable_if<std::is_same<Q, double>::value, bool>::type
    mergeInternal(const Lot &from, Lot &to, Document::MergeMode mergeMode,
                  const R defaultValue)
    {
        if (mergeMode == Document::MergeMode::MergeAverage) { // weighted by quantity
            int fromQty = std::abs(from.quantity());
            int toQty = std::abs(to.quantity());

            if ((fromQty == 0) && (toQty == 0))
                fromQty = toQty = 1;

            (to.*S)(((to.*G)() * toQty + (from.*G)() * fromQty) / (toQty + fromQty));
            return true;
        } else {
            if (!qFuzzyCompare((from.*G)(), defaultValue) && qFuzzyCompare((to.*G)(), defaultValue)) {
                copy(from, to);
                return true;
            }
        }
        return false;
    }

    template<class Q = R>
    static typename std::enable_if<std::is_same<Q, QString>::value, bool>::type
    mergeInternal(const Lot &from, Lot &to, Document::MergeMode mergeMode,
                  const R defaultValue)
    {
        if (mergeMode == Document::MergeMode::MergeText) { // add or remove "tags"
            QString f = (from.*G)();
            QString t = (to.*G)();

            if (!f.isEmpty() && !t.isEmpty() && (f != t)) {
                QRegularExpression fromRe { u"\\b" % QRegularExpression::escape(f) % u"\\b" };

                if (!fromRe.match(t).hasMatch()) {
                    QRegularExpression toRe { u"\\b" % QRegularExpression::escape(t) % u"\\b" };

                    if (toRe.match(f).hasMatch())
                        (to.*S)(f);
                    else
                        (to.*S)(t % u" " % f);
                    return true;
                }
            } else if (!f.isEmpty()) {
                (to.*S)(f);
                return true;
            }
        } else {
            if (((from.*G)() != defaultValue) && ((to.*G)() == defaultValue)) {
                copy(from, to);
                return true;
            }
        }
        return false;
    }
};


///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////


enum {
    CID_Change,
    CID_AddRemove,
    CID_Currency,
    CID_ResetDifferenceMode,
    CID_Sort,
    CID_Filter,

    // values starting at 0x00010000 are reserved for the view
};


AddRemoveCmd::AddRemoveCmd(Type t, Document *doc, const QVector<int> &positions,
                           const QVector<int> &sortedPositions,
                           const QVector<int> &filteredPositions, const LotList &lots)
    : QUndoCommand(genDesc(t == Add, qMax(lots.count(), positions.count())))
    , m_doc(doc)
    , m_positions(positions)
    , m_sortedPositions(sortedPositions)
    , m_filteredPositions(filteredPositions)
    , m_lots(lots)
    , m_type(t)
{
    // for add: specify lots and optionally also positions
    // for remove: specify lots only
}

AddRemoveCmd::~AddRemoveCmd()
{
    if (m_type == Add) {
        if (m_doc) {
            for (const auto lot : qAsConst(m_lots))
                m_doc->m_differenceBase.remove(lot);
        }
        qDeleteAll(m_lots);
    }
}

int AddRemoveCmd::id() const
{
    return CID_AddRemove;
}

void AddRemoveCmd::redo()
{
    if (m_type == Add) {
        // Document::insertLotsDirect() adds all m_lots at the positions given in
        // m_*positions (or append them to the document in case m_*positions is empty)
        m_doc->insertLotsDirect(m_lots, m_positions, m_sortedPositions, m_filteredPositions);
        m_positions.clear();
        m_sortedPositions.clear();
        m_filteredPositions.clear();
        m_type = Remove;
    }
    else {
        // Document::removeLotsDirect() removes all m_lots and records the positions
        // in m_*positions
        m_doc->removeLotsDirect(m_lots, m_positions, m_sortedPositions, m_filteredPositions);
        m_type = Add;
    }
}

void AddRemoveCmd::undo()
{
    redo();
}

QString AddRemoveCmd::genDesc(bool is_add, int count)
{
    if (is_add)
        return Document::tr("Added %n item(s)", nullptr, count);
    else
        return Document::tr("Removed %n item(s)", nullptr, count);
}


///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////


QTimer *ChangeCmd::s_eventLoopCounter = nullptr;

ChangeCmd::ChangeCmd(Document *doc, const std::vector<std::pair<Lot *, Lot>> &changes, Document::Field hint)
    : QUndoCommand()
    , m_doc(doc)
    , m_hint(hint)
    , m_changes(changes)
{
    std::sort(m_changes.begin(), m_changes.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });

    if (!s_eventLoopCounter) {
        s_eventLoopCounter = new QTimer(qApp);
        s_eventLoopCounter->setProperty("loopCount", uint(0));
        s_eventLoopCounter->setInterval(0);
        s_eventLoopCounter->setSingleShot(true);
        QObject::connect(s_eventLoopCounter, &QTimer::timeout,
                         s_eventLoopCounter, []() {
            uint lc = s_eventLoopCounter->property("loopCount").toUInt();
            s_eventLoopCounter->setProperty("loopCount", lc + 1);
        });
    }
    m_loopCount = s_eventLoopCounter->property("loopCount").toUInt();
    s_eventLoopCounter->start();

    updateText();
}

void ChangeCmd::updateText()
{
    //: Generic undo/redo text for table edits: %1 == column name (e.g. "Price")
    setText(QCoreApplication::translate("ChangeCmd", "Modified %1 on %Ln item(s)", nullptr,
                                        int(m_changes.size()))
            //: Generic undo/redo text for table edits: if more than one column was edited at once
            .arg((m_hint < Document::FieldCount) ? m_doc->headerData(m_hint, Qt::Horizontal).toString()
                                                 : QCoreApplication::translate("ChangeCmd", "multiple fields")));
}

int ChangeCmd::id() const
{
    return CID_Change;
}

bool ChangeCmd::mergeWith(const QUndoCommand *other)
{
    if (other->id() == id()) {
        auto *otherChange = static_cast<const ChangeCmd *>(other);
        if ((m_loopCount == otherChange->m_loopCount) && (m_hint == otherChange->m_hint)) {
            std::copy_if(otherChange->m_changes.cbegin(), otherChange->m_changes.cend(),
                         std::back_inserter(m_changes), [this](const auto &change) {
                return !std::binary_search(m_changes.begin(), m_changes.end(),
                                           std::make_pair(change.first, Lot()),
                                           [](const auto &a, const auto &b) {
                    return a.first < b.first;
                });
            });
            updateText();
            return true;
        }
    }
    return false;
}

void ChangeCmd::redo()
{
    m_doc->changeLotsDirect(m_changes);
}

void ChangeCmd::undo()
{
    redo();
}


///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////


CurrencyCmd::CurrencyCmd(Document *doc, const QString &ccode, qreal crate)
    : QUndoCommand(qApp->translate("CurrencyCmd", "Changed currency"))
    , m_doc(doc)
    , m_ccode(ccode)
    , m_crate(crate)
    , m_prices(nullptr)
{ }

CurrencyCmd::~CurrencyCmd()
{
    delete [] m_prices;
}

int CurrencyCmd::id() const
{
    return CID_Currency;
}

void CurrencyCmd::redo()
{
    Q_ASSERT(!m_prices);

    QString oldccode = m_doc->currencyCode();
    m_doc->changeCurrencyDirect(m_ccode, m_crate, m_prices);
    m_ccode = oldccode;
}

void CurrencyCmd::undo()
{
    Q_ASSERT(m_prices);

    QString oldccode = m_doc->currencyCode();
    m_doc->changeCurrencyDirect(m_ccode, m_crate, m_prices);
    m_ccode = oldccode;
}


///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////


ResetDifferenceModeCmd::ResetDifferenceModeCmd(Document *doc, const LotList &lots)
    : QUndoCommand(qApp->translate("ResetDifferenceModeCmd", "Reset difference mode base values"))
    , m_doc(doc)
    , m_differenceBase(doc->m_differenceBase)
{
    for (const Lot *lot : lots)
        m_differenceBase.insert(lot, *lot);
}

int ResetDifferenceModeCmd::id() const
{
    return CID_ResetDifferenceMode;
}

void ResetDifferenceModeCmd::redo()
{
    m_doc->resetDifferenceModeDirect(m_differenceBase);
}

void ResetDifferenceModeCmd::undo()
{
    redo();
}


///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////


SortCmd::SortCmd(Document *doc, const QVector<QPair<int, Qt::SortOrder>> &columns)
    : QUndoCommand(qApp->translate("SortCmd", "Sorted the view"))
    , m_doc(doc)
    , m_created(QDateTime::currentDateTime())
    , m_columns(columns)
{ }

int SortCmd::id() const
{
    return CID_Sort;
}

bool SortCmd::mergeWith(const QUndoCommand *other)
{
    return (other->id() == id());
}

void SortCmd::redo()
{
    auto oldColumns = m_doc->sortColumns();

    m_doc->sortDirect(m_columns, m_isSorted, m_unsorted);

    m_columns = oldColumns;
}

void SortCmd::undo()
{
    redo();
}


///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////


FilterCmd::FilterCmd(Document *doc, const QVector<Filter> &filterList)
    : QUndoCommand(qApp->translate("FilterCmd", "Filtered the view"))
    , m_doc(doc)
    , m_created(QDateTime::currentDateTime())
    , m_filterList(filterList)
{ }

int FilterCmd::id() const
{
    return CID_Filter;
}

bool FilterCmd::mergeWith(const QUndoCommand *other)
{
    return (other->id() == id());
}

void FilterCmd::redo()
{
    auto oldFilterList = m_doc->m_filter;

    m_doc->filterDirect(m_filterList, m_isFiltered, m_unfiltered);

    m_filterList = oldFilterList;
}

void FilterCmd::undo()
{
    redo();
}


///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////


Document::Statistics::Statistics(const Document *doc, const LotList &list, bool ignoreExcluded)
{
    m_lots = 0;
    m_items = 0;
    m_val = m_minval = m_cost = 0.;
    m_weight = .0;
    m_errors = 0;
    m_differences = 0;
    m_incomplete = 0;
    bool weight_missing = false;

    for (const Lot *lot : list) {
        if (ignoreExcluded && (lot->status() == BrickLink::Status::Exclude))
            continue;

        ++m_lots;

        int qty = lot->quantity();
        double price = lot->price();

        m_val += (qty * price);
        m_cost += (qty * lot->cost());

        for (int i = 0; i < 3; i++) {
            if (lot->tierQuantity(i) && !qFuzzyIsNull(lot->tierPrice(i)))
                price = lot->tierPrice(i);
        }
        m_minval += (qty * price * (1.0 - double(lot->sale()) / 100.0));
        m_items += qty;

        if (lot->totalWeight() > 0)
            m_weight += lot->totalWeight();
        else
            weight_missing = true;

        auto flags = doc->lotFlags(lot);
        if (flags.first)
            m_errors += qPopulationCount(flags.first);
        if (flags.second)
            m_differences += qPopulationCount(flags.second);

        if (lot->isIncomplete())
            m_incomplete++;
    }
    if (weight_missing)
        m_weight = qFuzzyIsNull(m_weight) ? -std::numeric_limits<double>::min() : -m_weight;
    m_ccode = doc->currencyCode();
}


// *****************************************************************************************
// *****************************************************************************************
// *****************************************************************************************
// *****************************************************************************************
// *****************************************************************************************
// *****************************************************************************************

QVector<Document *> Document::s_documents;

Document *Document::createTemporary(const LotList &list, const QVector<int> &fakeIndexes)
{
    auto *doc = new Document(1 /*dummy*/);
    LotList lots;

    // the caller owns the items, so we have to copy here
    for (const Lot *lot : list)
        lots.append(new Lot(*lot));

    doc->setLotsDirect(lots);
    doc->setFakeIndexes(fakeIndexes);
    return doc;
}

Document::Document(int /*is temporary*/)
    : m_filterParser(new Filter::Parser())
    , m_currencycode(Config::inst()->defaultCurrencyCode())
{
    initializeColumns();

    MODELTEST_ATTACH(this)

    connect(BrickLink::core(), &BrickLink::Core::pictureUpdated,
            this, &Document::pictureUpdated);

    languageChange();
}

// the caller owns the items
Document::Document()
    : Document(0)
{
    m_undo = new UndoStack(this);
    connect(m_undo, &QUndoStack::cleanChanged,
            this, [this](bool clean) {
        if (clean) {
            m_firstNonVisualIndex = 0;
            m_visuallyClean = true;
        } else if (m_undo->cleanIndex() < 0) {
            m_visuallyClean = false;
        }
        updateModified();
    });
    connect(m_undo, &QUndoStack::indexChanged,
            this, [this](int index) {
        bool oldVisuallyClean = m_visuallyClean;
        int cleanIndex = m_undo->cleanIndex();

        if ((cleanIndex < 0) || (index < cleanIndex)) {
            m_visuallyClean = false;
        } else if (index == 0 || (index == cleanIndex)) {
            m_firstNonVisualIndex = 0;
            m_visuallyClean = true;
        } else if (m_firstNonVisualIndex && (index < m_firstNonVisualIndex)) {
            m_firstNonVisualIndex = 0;
            m_visuallyClean = true;
        } else {
            const auto *lastCmd = m_undo->command(index - 1);
            if (lastCmd && (lastCmd->id() < CID_Sort)) {
                if (!m_firstNonVisualIndex || (index < m_firstNonVisualIndex)) {
                    m_firstNonVisualIndex = index;
                    m_visuallyClean = false;
                }
            }
        }
        if (m_visuallyClean != oldVisuallyClean)
            updateModified();
    });
    s_documents.append(this);
}

// the caller owns the items
Document::Document(const DocumentIO::BsxContents &bsx, bool forceModified)
    : Document()
{
    // we take ownership of the items
    setLotsDirect(bsx.lots);

    if (!bsx.currencyCode.isEmpty()) {
        if (bsx.currencyCode == "$$$"_l1) // legacy USD
            m_currencycode.clear();
        else
            m_currencycode = bsx.currencyCode;
    }

    if (forceModified)
        m_undo->resetClean();

    auto db = bsx.differenceModeBase; // get rid of const
    if (!db.isEmpty())
        resetDifferenceModeDirect(db);
}

Document::~Document()
{
    delete m_order;
    qDeleteAll(m_lots);
    delete m_undo;

    s_documents.removeAll(this);
}

const QVector<Document *> &Document::allDocuments()
{
    return s_documents;
}

const LotList &Document::lots() const
{
    return m_lots;
}

const LotList &Document::sortedLots() const
{
    return m_sortedLots;
}

const LotList &Document::filteredLots() const
{
    return m_filteredLots;
}

Document::Statistics Document::statistics(const LotList &list, bool ignoreExcluded) const
{
    return Statistics(this, list, ignoreExcluded);
}

void Document::beginMacro(const QString &label)
{
    m_undo->beginMacro(label);
}

void Document::endMacro(const QString &label)
{
    m_undo->endMacro(label);
}

QUndoStack *Document::undoStack() const
{
    return m_undo;
}


bool Document::clear()
{
    removeLots(m_lots);
    return true;
}

void Document::appendLot(Lot *lot)
{
    m_undo->push(new AddRemoveCmd(AddRemoveCmd::Add, this, { }, { }, { }, { lot }));
}

void Document::appendLots(const LotList &lots)
{
    m_undo->push(new AddRemoveCmd(AddRemoveCmd::Add, this, { }, { }, { }, lots));
}

void Document::insertLotsAfter(const Lot *afterLot, const LotList &lots)
{
    if (lots.isEmpty())
        return;

    int afterPos = m_lots.indexOf(const_cast<Lot *>(afterLot)) + 1;
    int afterSortedPos = m_sortedLots.indexOf(const_cast<Lot *>(afterLot)) + 1;
    int afterFilteredPos = m_filteredLots.indexOf(const_cast<Lot *>(afterLot)) + 1;

    Q_ASSERT((afterPos > 0) && (afterSortedPos > 0));
    if (afterFilteredPos == 0)
        afterFilteredPos = m_filteredLots.size();

    QVector<int> positions(lots.size());
    std::iota(positions.begin(), positions.end(), afterPos);
    QVector<int> sortedPositions(lots.size());
    std::iota(sortedPositions.begin(), sortedPositions.end(), afterSortedPos);
    QVector<int> filteredPositions(lots.size());
    std::iota(filteredPositions.begin(), filteredPositions.end(), afterFilteredPos);

    m_undo->push(new AddRemoveCmd(AddRemoveCmd::Add, this, positions, sortedPositions,
                                  filteredPositions, lots));
}

void Document::removeLot(Lot *lot)
{
    return removeLots({ lot });
}

void Document::removeLots(const LotList &lots)
{
    m_undo->push(new AddRemoveCmd(AddRemoveCmd::Remove, this, { }, { }, { }, lots));
}

void Document::changeLot(Lot *lot, const Lot &value, Document::Field hint)
{
    m_undo->push(new ChangeCmd(this, {{ lot, value }}, hint));
}

void Document::changeLots(const std::vector<std::pair<Lot *, Lot>> &changes, Field hint)
{
    if (!changes.empty())
        m_undo->push(new ChangeCmd(this, changes, hint));
}

void Document::setLotsDirect(const LotList &lots)
{
    if (lots.isEmpty())
        return;
    QVector<int> posDummy, sortedPosDummy, filteredPosDummy;
    insertLotsDirect(lots, posDummy, sortedPosDummy, filteredPosDummy);
}

void Document::insertLotsDirect(const LotList &lots, QVector<int> &positions,
                                 QVector<int> &sortedPositions, QVector<int> &filteredPositions)
{
    auto pos = positions.constBegin();
    auto sortedPos = sortedPositions.constBegin();
    auto filteredPos = filteredPositions.constBegin();
    bool isAppend = positions.isEmpty();

    Q_ASSERT((positions.size() == sortedPositions.size())
             && (positions.size() == filteredPositions.size()));
    Q_ASSERT(isAppend != (positions.size() == lots.size()));

    emit layoutAboutToBeChanged({ }, VerticalSortHint);
    QModelIndexList before = persistentIndexList();

    for (Lot *lot : qAsConst(lots)) {
        if (!isAppend) {
            m_lots.insert(*pos++, lot);
            m_sortedLots.insert(*sortedPos++, lot);
            m_filteredLots.insert(*filteredPos++, lot);
        } else {
            m_lots.append(lot);
            m_sortedLots.append(lot);
            m_filteredLots.append(lot);
        }

        // this is really a new lot, not just a redo - start with no differences
        if (!m_differenceBase.contains(lot))
            m_differenceBase.insert(lot, *lot);

        updateLotFlags(lot);
    }

    QModelIndexList after;
    foreach (const QModelIndex &idx, before)
        after.append(index(lot(idx), idx.column()));
    changePersistentIndexList(before, after);
    emit layoutChanged({ }, VerticalSortHint);

    emit lotCountChanged(m_lots.count());
    emitStatisticsChanged();

    if (isSorted())
        emit isSortedChanged(m_isSorted = false);
    if (isFiltered())
        emit isFilteredChanged(m_isFiltered = false);
}

void Document::removeLotsDirect(const LotList &lots, QVector<int> &positions,
                                 QVector<int> &sortedPositions, QVector<int> &filteredPositions)
{
    positions.resize(lots.count());
    sortedPositions.resize(lots.count());
    filteredPositions.resize(lots.count());

    emit layoutAboutToBeChanged({ }, VerticalSortHint);
    QModelIndexList before = persistentIndexList();

    for (int i = lots.count() - 1; i >= 0; --i) {
        Lot *lot = lots.at(i);
        int idx = m_lots.indexOf(lot);
        int sortIdx = m_sortedLots.indexOf(lot);
        int filterIdx = m_filteredLots.indexOf(lot);
        Q_ASSERT(idx >= 0 && sortIdx >= 0);
        positions[i] = idx;
        sortedPositions[i] = sortIdx;
        filteredPositions[i] = filterIdx;
        m_lots.removeAt(idx);
        m_sortedLots.removeAt(sortIdx);
        m_filteredLots.removeAt(filterIdx);
    }

    QModelIndexList after;
    foreach (const QModelIndex &idx, before)
        after.append(index(lot(idx), idx.column()));
    changePersistentIndexList(before, after);
    emit layoutChanged({ }, VerticalSortHint);

    emit lotCountChanged(m_lots.count());
    emitStatisticsChanged();

    //TODO: we should remember and re-apply the isSorted/isFiltered state
    if (isSorted())
        emit isSortedChanged(m_isSorted = false);
    if (isFiltered())
        emit isFilteredChanged(m_isFiltered = false);
}

void Document::changeLotsDirect(std::vector<std::pair<Lot *, Lot>> &changes)
{
    Q_ASSERT(!changes.empty());

    for (auto &change : changes) {
        Lot *lot = change.first;
        std::swap(*lot, change.second);

        QModelIndex idx1 = index(lot, 0);
        QModelIndex idx2 = idx1.siblingAtColumn(columnCount() - 1);
        updateLotFlags(lot);
        emitDataChanged(idx1, idx2);
    }

    emitStatisticsChanged();

    //TODO: we should remember and re-apply the isSorted/isFiltered state
    if (isSorted())
        emit isSortedChanged(m_isSorted = false);
    if (isFiltered())
        emit isFilteredChanged(m_isFiltered = false);
}

void Document::changeCurrencyDirect(const QString &ccode, qreal crate, double *&prices)
{
    m_currencycode = ccode;

    if (!qFuzzyCompare(crate, qreal(1)) || (ccode != m_currencycode)) {
        bool createPrices = (prices == nullptr);
        if (createPrices)
            prices = new double[5 * m_lots.count()];

        for (int i = 0; i < m_lots.count(); ++i) {
            Lot *lot = m_lots[i];
            if (createPrices) {
                prices[i * 5] = lot->cost();
                prices[i * 5 + 1] = lot->price();
                prices[i * 5 + 2] = lot->tierPrice(0);
                prices[i * 5 + 3] = lot->tierPrice(1);
                prices[i * 5 + 4] = lot->tierPrice(2);

                lot->setCost(prices[i * 5] * crate);
                lot->setPrice(prices[i * 5 + 1] * crate);
                lot->setTierPrice(0, prices[i * 5 + 2] * crate);
                lot->setTierPrice(1, prices[i * 5 + 3] * crate);
                lot->setTierPrice(2, prices[i * 5 + 4] * crate);
            } else {
                lot->setCost(prices[i * 5]);
                lot->setPrice(prices[i * 5 + 1]);
                lot->setTierPrice(0, prices[i * 5 + 2]);
                lot->setTierPrice(1, prices[i * 5 + 3]);
                lot->setTierPrice(2, prices[i * 5 + 4]);
            }
        }

        if (!createPrices) {
            delete [] prices;
            prices = nullptr;
        }

        emitDataChanged();
        emitStatisticsChanged();

        //TODO: we should remember and re-apply the isSorted/isFiltered state
        if (isSorted())
            emit isSortedChanged(m_isSorted = false);
        if (isFiltered())
            emit isFilteredChanged(m_isFiltered = false);
    }
    emit currencyCodeChanged(currencyCode());
}

void Document::emitDataChanged(const QModelIndex &tl, const QModelIndex &br)
{
    if (!m_delayedEmitOfDataChanged) {
        m_delayedEmitOfDataChanged = new QTimer(this);
        m_delayedEmitOfDataChanged->setSingleShot(true);
        m_delayedEmitOfDataChanged->setInterval(0);

        static auto resetNext = [](decltype(m_nextDataChangedEmit) &next) {
            next = {
                QPoint(std::numeric_limits<int>::max(), std::numeric_limits<int>::max()),
                QPoint(std::numeric_limits<int>::min(), std::numeric_limits<int>::min())
            };
        };

        resetNext(m_nextDataChangedEmit);

        connect(m_delayedEmitOfDataChanged, &QTimer::timeout,
                this, [this]() {

            emit dataChanged(index(m_nextDataChangedEmit.first.y(),
                                   m_nextDataChangedEmit.first.x()),
                             index(m_nextDataChangedEmit.second.y(),
                                   m_nextDataChangedEmit.second.x()));

            resetNext(m_nextDataChangedEmit);
        });
    }

    QModelIndex xtl = tl.isValid() ? tl : index(0, 0);
    QModelIndex xbr = br.isValid() ? br : index(rowCount() - 1, columnCount() - 1);

    if (xtl.row() < m_nextDataChangedEmit.first.y())
        m_nextDataChangedEmit.first.setY(xtl.row());
    if (xtl.column() < m_nextDataChangedEmit.first.x())
        m_nextDataChangedEmit.first.setX(xtl.column());
    if (xbr.row() > m_nextDataChangedEmit.second.y())
        m_nextDataChangedEmit.second.setY(xbr.row());
    if (xbr.column() > m_nextDataChangedEmit.second.x())
        m_nextDataChangedEmit.second.setX(xbr.column());

    m_delayedEmitOfDataChanged->start();
}

void Document::emitStatisticsChanged()
{
    if (!m_delayedEmitOfStatisticsChanged) {
        m_delayedEmitOfStatisticsChanged = new QTimer(this);
        m_delayedEmitOfStatisticsChanged->setSingleShot(true);
        m_delayedEmitOfStatisticsChanged->setInterval(0);

        connect(m_delayedEmitOfStatisticsChanged, &QTimer::timeout,
                this, &Document::statisticsChanged);
    }
    m_delayedEmitOfStatisticsChanged->start();
}

void Document::updateLotFlags(const Lot *lot)
{
    quint64 errors = 0;
    quint64 updated = 0;

    if (!lot->item())
        errors |= (1ULL << PartNo);
    if (lot->price() <= 0)
        errors |= (1ULL << Price);
    if (lot->quantity() <= 0)
        errors |= (1ULL << Quantity);
    if (!lot->color() || (lot->itemType() && ((lot->color()->id() != 0) && !lot->itemType()->hasColors())))
        errors |= (1ULL << Color);
    if (lot->tierQuantity(0) && ((lot->tierPrice(0) <= 0) || (lot->tierPrice(0) >= lot->price())))
        errors |= (1ULL << TierP1);
    if (lot->tierQuantity(1) && ((lot->tierPrice(1) <= 0) || (lot->tierPrice(1) >= lot->tierPrice(0))))
        errors |= (1ULL << TierP2);
    if (lot->tierQuantity(1) && (lot->tierQuantity(1) <= lot->tierQuantity(0)))
        errors |= (1ULL << TierQ2);
    if (lot->tierQuantity(2) && ((lot->tierPrice(2) <= 0) || (lot->tierPrice(2) >= lot->tierPrice(1))))
        errors |= (1ULL << TierP3);
    if (lot->tierQuantity(2) && (lot->tierQuantity(2) <= lot->tierQuantity(1)))
        errors |= (1ULL << TierQ3);
    if (lot->status() == BrickLink::Status::Exclude)
        errors = 0;

    if (auto base = differenceBaseLot(lot)) {
        static const quint64 ignoreMask = 0ULL
                | (1ULL << Index)
                | (1ULL << Status)
                | (1ULL << Picture)
                | (1ULL << Description)
                | (1ULL << QuantityOrig)
                | (1ULL << QuantityDiff)
                | (1ULL << PriceOrig)
                | (1ULL << PriceDiff)
                | (1ULL << Total)
                | (1ULL << Category)
                | (1ULL << ItemType)
                | (1ULL << LotId)
                | (1ULL << Weight)
                | (1ULL << YearReleased)
                | (1ULL << Marker);

        for (Field f = Index; f != FieldCount; f = Field(f + 1)) {
            quint64 fmask = (1ULL << f);
            if (fmask & ignoreMask)
                continue;
            if (dataForEditRole(lot, f) != dataForEditRole(base, f))
                updated |= fmask;
        }
    }

    setLotFlags(lot, errors, updated);
}

void Document::resetDifferenceMode(const LotList &lotList)
{
    m_undo->push(new ResetDifferenceModeCmd(this, lotList.isEmpty() ? lots() : lotList));
}

void Document::resetDifferenceModeDirect(QHash<const Lot *, Lot> &differenceBase)
{
    std::swap(m_differenceBase, differenceBase);

    for (const auto *lot : qAsConst(m_lots))
        updateLotFlags(lot);

    emitDataChanged();
}

const Lot *Document::differenceBaseLot(const Lot *lot) const
{
    if (!lot)
        return nullptr;

    auto it = m_differenceBase.constFind(lot);
    return (it != m_differenceBase.end()) ? &(*it) : nullptr;
}

bool Document::legacyCurrencyCode() const
{
    return m_currencycode.isEmpty();
}

QString Document::currencyCode() const
{
    return m_currencycode.isEmpty() ? QString::fromLatin1("USD") : m_currencycode;
}

void Document::setCurrencyCode(const QString &ccode, qreal crate)
{
    if (ccode != currencyCode())
        m_undo->push(new CurrencyCmd(this, ccode, crate));
}

void Document::setOrder(BrickLink::Order *order)
{
    if (m_order)
        delete m_order;
    m_order = order;
}

bool Document::canLotsBeMerged(const Lot &lot1, const Lot &lot2)
{
    return ((&lot1 != &lot2)
            && !lot1.isIncomplete()
            && !lot2.isIncomplete()
            && (lot1.item() == lot2.item())
            && (lot1.color() == lot2.color())
            && (lot1.condition() == lot2.condition())
            && (lot1.subCondition() == lot2.subCondition()));
}

bool Document::mergeLotFields(const Lot &from, Lot &to, MergeMode defaultMerge,
                               const QHash<Field, MergeMode> &fieldMerge)
{
    if (!canLotsBeMerged(from, to))
        return false;

    if (fieldMerge.isEmpty()) {
        if (defaultMerge == MergeMode::Ignore) {
            return false;
        } else if (defaultMerge == MergeMode::Copy) {
            to = from;
            return true;
        }
    }

    auto mergeModeFor = [&fieldMerge, defaultMerge](Field f) -> MergeMode {
        auto mergeIt = fieldMerge.constFind(f);
        return (mergeIt == fieldMerge.cend()) ? defaultMerge : mergeIt.value();
    };

    bool changed = false;

    if (FieldOp<&Lot::quantity, &Lot::setQuantity>::merge(from, to, mergeModeFor(Quantity)))
        changed = true;
    if (FieldOp<&Lot::price, &Lot::setPrice>::merge(from, to, mergeModeFor(Price)))
        changed = true;
    if (FieldOp<&Lot::cost, &Lot::setCost>::merge(from, to, mergeModeFor(Cost)))
        changed = true;
    if (FieldOp<&Lot::bulkQuantity, &Lot::setBulkQuantity>::merge(from, to, mergeModeFor(Bulk)))
        changed = true;
    if (FieldOp<&Lot::sale, &Lot::setSale>::merge(from, to, mergeModeFor(Sale)))
        changed = true;
    if (FieldOp<&Lot::comments, &Lot::setComments>::merge(from, to, mergeModeFor(Comments)))
        changed = true;
    if (FieldOp<&Lot::remarks, &Lot::setRemarks>::merge(from, to, mergeModeFor(Remarks)))
        changed = true;
    if (FieldOp<&Lot::tierQuantity0, &Lot::setTierQuantity0>::merge(from, to, mergeModeFor(TierQ1)))
        changed = true;
    if (FieldOp<&Lot::tierPrice0, &Lot::setTierPrice0>::merge(from, to, mergeModeFor(TierP1)))
        changed = true;
    if (FieldOp<&Lot::tierQuantity1, &Lot::setTierQuantity1>::merge(from, to, mergeModeFor(TierQ2)))
        changed = true;
    if (FieldOp<&Lot::tierPrice1, &Lot::setTierPrice1>::merge(from, to, mergeModeFor(TierP2)))
        changed = true;
    if (FieldOp<&Lot::tierQuantity2, &Lot::setTierQuantity2>::merge(from, to, mergeModeFor(TierQ3)))
        changed = true;
    if (FieldOp<&Lot::tierPrice2, &Lot::setTierPrice2>::merge(from, to, mergeModeFor(TierP3)))
        changed = true;
    if (FieldOp<&Lot::retain, &Lot::setRetain>::merge(from, to, mergeModeFor(Retain)))
        changed = true;
    if (FieldOp<&Lot::stockroom, &Lot::setStockroom>::merge(from, to, mergeModeFor(Stockroom)))
        changed = true;
    if (FieldOp<&Lot::reserved, &Lot::setReserved>::merge(from, to, mergeModeFor(Reserved)))
        changed = true;

    //TODO: merge markers

    return changed;
}

Filter::Parser *Document::filterParser()
{
    return m_filterParser.get();
}

void Document::setFakeIndexes(const QVector<int> &fakeIndexes)
{
    m_fakeIndexes = fakeIndexes;
}

QString Document::fileName() const
{
    return m_filename;
}

void Document::setFileName(const QString &str)
{
    if (str != m_filename) {
        m_filename = str;
        emit fileNameChanged(str);
    }
}

QString Document::fileNameOrTitle() const
{
    QFileInfo fi(m_filename);
    if (fi.exists())
        return QDir::toNativeSeparators(fi.absoluteFilePath());

    if (!m_title.isEmpty())
        return m_title;

    return m_filename;
}

QString Document::title() const
{
    return m_title;
}

void Document::setTitle(const QString &str)
{
    if (str != m_title) {
        m_title = str;
        emit titleChanged(m_title);
    }
}

bool Document::isModified() const
{
    bool modified = !m_undo->isClean();
    if (modified && !Config::inst()->visualChangesMarkModified())
        modified = !m_visuallyClean;

    return modified;
}

void Document::unsetModified()
{
    m_undo->setClean();
    updateModified();
}

QHash<const Lot *, Lot> Document::differenceBase() const
{
    return m_differenceBase;
}

void Document::updateModified()
{
    emit modificationChanged(isModified());
}

void Document::setLotFlagsMask(QPair<quint64, quint64> flagsMask)
{
    m_lotFlagsMask = flagsMask;
    emitStatisticsChanged();
    emitDataChanged();
}

QPair<quint64, quint64> Document::lotFlags(const Lot *lot) const
{
    auto flags = m_lotFlags.value(lot, { });
    flags.first &= m_lotFlagsMask.first;
    flags.second &= m_lotFlagsMask.second;
    return flags;
}

void Document::setLotFlags(const Lot *lot, quint64 errors, quint64 updated)
{
    if (!lot)
        return;

    auto oldFlags = m_lotFlags.value(lot, { });
    if (oldFlags.first != errors || oldFlags.second != updated) {
        if (errors || updated)
            m_lotFlags.insert(lot, qMakePair(errors, updated));
        else
            m_lotFlags.remove(lot);

        emit lotFlagsChanged(lot);
        emitStatisticsChanged();
    }
}

const BrickLink::Order *Document::order() const
{
    return m_order;
}


////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
// Itemviews API
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////


QModelIndex Document::index(int row, int column, const QModelIndex &parent) const
{
    if (!parent.isValid() && row >= 0 && column >= 0 && row < rowCount() && column < columnCount())
        return createIndex(row, column, m_filteredLots.at(row));
    return { };
}

Lot *Document::lot(const QModelIndex &idx) const
{
    return idx.isValid() ? static_cast<Lot *>(idx.internalPointer()) : nullptr;
}

QModelIndex Document::index(const Lot *lot, int column) const
{
    int row = m_filteredLots.indexOf(const_cast<Lot *>(lot));
    if (row >= 0)
        return createIndex(row, column, const_cast<Lot *>(lot));
    return { };
}

int Document::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_filteredLots.size();
}

int Document::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : FieldCount;
}

Qt::ItemFlags Document::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return { };

    Qt::ItemFlags ifs = QAbstractItemModel::flags(index);
    const auto &c = m_columns.value(index.column());
    if (c.editable)
        ifs |= Qt::ItemIsEditable;
    if (c.checkable)
        ifs |= Qt::ItemIsUserCheckable;
    return ifs;
}

bool Document::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || (role != Qt::EditRole))
        return false;

    Document::Field f = static_cast<Field>(index.column());
    Lot *lot = this->lot(index);
    Lot lotCopy = *lot;

    if (auto sdata = m_columns.value(f).setDataFn)
        sdata(lot, value);

    // this a bit awkward with all the copying, but setDataFn needs lot pointer that is valid in the model
    if (*lot != lotCopy) {
        std::swap(*lot, lotCopy);
        changeLot(lot, lotCopy, f);
        return true;
    }
    return false;
}

QVariant Document::data(const QModelIndex &index, int role) const
{
    if (index.isValid()) {
        const Lot *lot = this->lot(index);
        auto f = static_cast<Field>(index.column());

        switch (role) {
        case Qt::DisplayRole      : return dataForDisplayRole(lot, f);
        case BaseDisplayRole      : return dataForDisplayRole(differenceBaseLot(lot), f);
        case Qt::TextAlignmentRole: return headerData(index.column(), Qt::Horizontal, role);
        case Qt::EditRole         : return dataForEditRole(lot, f);
        case BaseEditRole         : return dataForEditRole(differenceBaseLot(lot), f);
        case FilterRole           : return dataForFilterRole(lot, f);
        case LotPointerRole       : return QVariant::fromValue(lot);
        case BaseLotPointerRole   : return QVariant::fromValue(differenceBaseLot(lot));
        case ErrorFlagsRole       : return lotFlags(lot).first;
        case DifferenceFlagsRole  : return lotFlags(lot).second;
        }
    }
    return QVariant();
}

QVariant Document::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return { };

    const auto &c = m_columns.value(section);

    switch (role) {
    case Qt::TextAlignmentRole : return c.alignment | Qt::AlignVCenter;
    case Qt::DisplayRole       : return tr(c.title);
    case HeaderDefaultWidthRole: return c.defaultWidth;
    case HeaderValueModelRole  : return QVariant::fromValue(c.valueModelFn ? c.valueModelFn() : nullptr);
    case HeaderFilterableRole  : return c.filterable;
    default                    : return { };
    }
}


QVariant Document::dataForEditRole(const Lot *lot, Field f) const
{
    auto data = m_columns.value(f).dataFn;
    return (data && lot) ? data(lot) : QVariant { };
}

QVariant Document::dataForDisplayRole(const Lot *lot, Field f) const
{
    const auto &c = m_columns.value(f);
    return c.displayFn ? c.displayFn(lot) : (c.dataFn ? c.dataFn(lot) : QVariant { });
}

QVariant Document::dataForFilterRole(const Lot *lot, Field f) const
{
    const auto &c = m_columns.value(f);
    if (c.filterFn) {
        return c.filterFn(lot);
    } else {
        QVariant v;
        if (c.dataFn)
            v = c.dataFn(lot);
        if ((v.isNull() || (v.userType() >= QMetaType::User)) && c.displayFn)
            v = c.displayFn(lot);
        return v;
    }
}

void Document::initializeColumns()
{
    if (!m_columns.isEmpty())
        return;

    static auto boolCompare = [](bool b1, bool b2) -> int
    { return (b1 ? 1 : 0) - (b2 ? 1 : 0); };
    static auto uintCompare = [](uint u1, uint u2) -> int
    { return (u1 == u2) ? 0 : ((u1 < u2) ? -1 : 1); };
    static auto doubleCompare = [](double d1, double d2) -> int
    { return fuzzyCompare(d1, d2) ? 0 : ((d1 < d2) ? -1 : 1); };

    auto C = [this](Field f, const Column &c) { m_columns.insert(f, c); };

    C(Index, {
          .defaultWidth = 3,
          .alignment = Qt::AlignRight,
          .editable = false,
          .title = QT_TR_NOOP("Index"),
          .displayFn = [&](const Lot *lot) {
              if (m_fakeIndexes.isEmpty()) {
                  return QString::number(m_lots.indexOf(const_cast<Lot *>(lot)) + 1);
              } else {
                  auto fi = m_fakeIndexes.at(m_lots.indexOf(const_cast<Lot *>(lot)));
                  return fi >= 0 ? QString::number(fi + 1) : QString::fromLatin1("+");
              }
          },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return m_lots.indexOf(const_cast<Lot *>(l1)) - m_lots.indexOf(const_cast<Lot *>(l2));
          },
      });

    C(Status, {
          .defaultWidth = 6,
          .alignment = Qt::AlignHCenter,
          .title = QT_TR_NOOP("Status"),
          .valueModelFn = [&]() { return new QStringListModel({ tr("Include"), tr("Exclude"), tr("Extra") }); },
          .dataFn = [&](const Lot *lot) { return QVariant::fromValue(lot->status()); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setStatus(v.value<BrickLink::Status>()); },
          .filterFn = [&](const Lot *lot) {
              switch (lot->status()) {
              case BrickLink::Status::Include: return tr("Include");
              case BrickLink::Status::Extra  : return tr("Extra");
              default:
              case BrickLink::Status::Exclude: return tr("Exclude");
              }
          },
          .compareFn = [&](const Lot *l1, const Lot *l2) -> bool {
              if (l1->counterPart() != l2->counterPart()) {
                  return boolCompare(l1->counterPart(), l2->counterPart());
              } else if (l1->alternateId() != l2->alternateId()) {
                  return uintCompare(l1->alternateId(), l2->alternateId());
              } else if (l1->alternate() != l2->alternate()) {
                  return boolCompare(l1->alternate(), l2->alternate());
              } else {
                  return int(l1->status()) - int(l2->status());
              }
          }
      });

    C(Picture, {
          .defaultWidth = -80,
          .alignment = Qt::AlignHCenter,
          .filterable = false,
          .title = QT_TR_NOOP("Image"),
          .dataFn = [&](const Lot *lot) { return QVariant::fromValue(lot->item()); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setItem(v.value<const BrickLink::Item *>()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return Utility::naturalCompare(QLatin1String(l1->itemId()), QLatin1String(l2->itemId()));
          },
      });
    C(PartNo, {
          .defaultWidth = 10,
          .title = QT_TR_NOOP("Part #"),
          .dataFn = [&](const Lot *lot) { return lot->itemId(); },
          .setDataFn = [&](Lot *lot, const QVariant &v) {
              char itid = lot->itemType() ? lot->itemType()->id() : 'P';
              if (auto newItem = BrickLink::core()->item(itid, v.toString().toLatin1()))
                  lot->setItem(newItem);
          },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return Utility::naturalCompare(QLatin1String(l1->itemId()), QLatin1String(l2->itemId()));
          },
      });
    C(Description, {
          .defaultWidth = 28,
          .title = QT_TR_NOOP("Description"),
          .dataFn = [&](const Lot *lot) { return QVariant::fromValue(lot->item()); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setItem(v.value<const BrickLink::Item *>()); },
          .displayFn = [&](const Lot *lot) { return lot->itemName(); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return Utility::naturalCompare(l1->itemName(), l2->itemName());
          },
      });
    C(Comments, {
          .title = QT_TR_NOOP("Comments"),
          .dataFn = [&](const Lot *lot) { return lot->comments(); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setComments(v.toString()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return l1->comments().localeAwareCompare(l2->comments());
          },
      });
    C(Remarks, {
          .title = QT_TR_NOOP("Remarks"),
          .dataFn = [&](const Lot *lot) { return lot->remarks(); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setRemarks(v.toString()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return l1->remarks().localeAwareCompare(l2->remarks());
          },
      });
    C(QuantityOrig, {
          .defaultWidth = 5,
          .alignment = Qt::AlignRight,
          .editable = false,
          .title = QT_TR_NOOP("Qty.Orig"),
          .displayFn = [&](const Lot *lot) {
              auto base = differenceBaseLot(lot);
              return base ? base->quantity() : 0;
          },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              auto base1 = differenceBaseLot(l1);
              auto base2 = differenceBaseLot(l2);
              return (base1 ? base1->quantity() : 0) - (base2 ? base2->quantity() : 0);
          },
      });
    C(QuantityDiff, {
          .defaultWidth = 5,
          .alignment = Qt::AlignRight,
          .title = QT_TR_NOOP("Qty.Diff"),
          .dataFn = [&](const Lot *lot) {
              auto base = differenceBaseLot(lot);
              return base ? lot->quantity() - base->quantity() : 0;
          },
          .setDataFn = [&](Lot *lot, const QVariant &v) {
              if (auto base = differenceBaseLot(lot))
                  lot->setQuantity(base->quantity() + v.toInt());
          },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              auto base1 = differenceBaseLot(l1);
              auto base2 = differenceBaseLot(l2);
              return (base1 ? l1->quantity() - base1->quantity() : 0) - (base2 ? l2->quantity() - base2->quantity() : 0);
          },
      });
    C(Quantity, {
          .defaultWidth = 5,
          .alignment = Qt::AlignRight,
          .title = QT_TR_NOOP("Qty."),
          .dataFn = [&](const Lot *lot) { return lot->quantity(); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setQuantity(v.toInt()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return l1->quantity() - l2->quantity();
          },
      });
    C(Bulk, {
          .defaultWidth = 5,
          .alignment = Qt::AlignRight,
          .title = QT_TR_NOOP("Bulk"),
          .dataFn = [&](const Lot *lot) { return lot->bulkQuantity(); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setBulkQuantity(v.toInt()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return l1->bulkQuantity() - l2->bulkQuantity();
          },
      });
    C(PriceOrig, {
          .alignment = Qt::AlignRight,
          .editable = false,
          .title = QT_TR_NOOP("Pr.Orig"),
          .displayFn = [&](const Lot *lot) {
              auto base = differenceBaseLot(lot);
              return base ? base->price() : 0;
          },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              auto base1 = differenceBaseLot(l1);
              auto base2 = differenceBaseLot(l2);
              return doubleCompare(base1 ? base1->price() : 0, base2 ? base2->price() : 0);
          },
      });
    C(PriceDiff, {
          .alignment = Qt::AlignRight,
          .title = QT_TR_NOOP("Pr.Diff"),
          .dataFn = [&](const Lot *lot) {
              auto base = differenceBaseLot(lot);
              return base ? lot->price() - base->price() : 0;
          },
          .setDataFn = [&](Lot *lot, const QVariant &v) {
              if (auto base = differenceBaseLot(lot))
                  lot->setPrice(base->price() + v.toDouble());
          },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              auto base1 = differenceBaseLot(l1);
              auto base2 = differenceBaseLot(l2);
              return doubleCompare(base1 ? l1->price() - base1->price() : 0, base2 ? l2->price() - base2->price() : 0);
          },
      });
    C(Cost, {
          .alignment = Qt::AlignRight,
          .title = QT_TR_NOOP("Cost"),
          .dataFn = [&](const Lot *lot) { return lot->cost(); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setCost(v.toDouble()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return doubleCompare(l1->cost(), l2->cost());
          },
      });
    C(Price, {
          .alignment = Qt::AlignRight,
          .title = QT_TR_NOOP("Price"),
          .dataFn = [&](const Lot *lot) { return lot->price(); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setPrice(v.toDouble()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return doubleCompare(l1->price(), l2->price());
          },
      });
    C(Total, {
          .alignment = Qt::AlignRight,
          .editable = false,
          .title = QT_TR_NOOP("Total"),
          .displayFn = [&](const Lot *lot) { return lot->total(); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return doubleCompare(l1->total(), l2->total());
          },
      });
    C(Sale, {
          .defaultWidth = 5,
          .alignment = Qt::AlignRight,
          .title = QT_TR_NOOP("Sale"),
          .dataFn = [&](const Lot *lot) { return lot->sale(); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setSale(v.toInt()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return l1->sale() - l2->sale();
          },
      });
    C(Condition, {
          .defaultWidth = 5,
          .alignment = Qt::AlignHCenter,
          .title = QT_TR_NOOP("Cond."),
          .valueModelFn = [&]() { return new QStringListModel({ tr("New"), tr("Used") }); },
          .dataFn = [&](const Lot *lot) { return QVariant::fromValue(lot->condition()); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setCondition(v.value<BrickLink::Condition>()); },
          .filterFn = [&](const Lot *lot) {
              return (lot->condition() == BrickLink::Condition::New) ? tr("New") : tr("Used");
          },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              int d = int(l1->condition()) - int(l2->condition());
              return d ? d : int(l1->subCondition()) - int(l2->subCondition());
          },
      });
    C(Color, {
          .defaultWidth = 15,
          .title = QT_TR_NOOP("Color"),
          .valueModelFn = [&]() {
              auto model = new BrickLink::ColorModel(nullptr);
              model->sort(0, Qt::AscendingOrder);
              QSet<const BrickLink::Color *> colors;
              for (const auto &lot : qAsConst(m_lots))
                  colors.insert(lot->color());
              model->setColorListFilter(QVector<const BrickLink::Color *>(colors.cbegin(), colors.cend()));
              return model;
          },
          .dataFn = [&](const Lot *lot) { return QVariant::fromValue(lot->color()); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setColor(v.value<const BrickLink::Color *>()); },
          .displayFn = [&](const Lot *lot) { return lot->colorName(); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return l1->colorName().localeAwareCompare(l2->colorName());
          },
      });
    C(Category, {
          .defaultWidth = 12,
          .editable = false,
          .title = QT_TR_NOOP("Category"),
          .valueModelFn = [&]() {
              QStringList sl;
              const auto &cats = BrickLink::core()->categories();
              std::for_each(cats.cbegin(), cats.cend(), [&](const auto &cat) { sl << cat.name(); });
              sl.sort(Qt::CaseInsensitive);
              return new QStringListModel(sl);
          },
          .displayFn = [&](const Lot *lot) { return lot->categoryName(); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return l1->categoryName().localeAwareCompare(l2->categoryName());
          },
      });
    C(ItemType, {
          .defaultWidth = 12,
          .editable = false,
          .title = QT_TR_NOOP("Item Type"),
          .valueModelFn = [&]() {
              QStringList sl;
              const auto &itts = BrickLink::core()->itemTypes();
              std::for_each(itts.cbegin(), itts.cend(), [&](const auto &itt) { sl << itt.name(); });
              return new QStringListModel(sl);
          },
          .displayFn = [&](const Lot *lot) { return lot->itemTypeName(); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return l1->itemTypeName().localeAwareCompare(l2->itemTypeName());
          },
      });
    C(TierQ1, {
          .defaultWidth = 5,
          .alignment = Qt::AlignRight,
          .title = QT_TR_NOOP("Tier Q1"),
          .dataFn = [&](const Lot *lot) { return lot->tierQuantity(0); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setTierQuantity(0, v.toInt()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return l1->tierQuantity(0) - l2->tierQuantity(0);
          },
      });
    C(TierP1, {
          .alignment = Qt::AlignRight,
          .title = QT_TR_NOOP("Tier P1"),
          .dataFn = [&](const Lot *lot) { return lot->tierPrice(0); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setTierPrice(0, v.toDouble()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return doubleCompare(l1->tierPrice(0), l2->tierPrice(0));
          },
      });
    C(TierQ2, {
          .defaultWidth = 5,
          .alignment = Qt::AlignRight,
          .title = QT_TR_NOOP("Tier Q2"),
          .dataFn = [&](const Lot *lot) { return lot->tierQuantity(1); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setTierQuantity(1, v.toInt()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return l1->tierQuantity(1) - l2->tierQuantity(1);
          },
      });
    C(TierP2, {
          .alignment = Qt::AlignRight,
          .title = QT_TR_NOOP("Tier P2"),
          .dataFn = [&](const Lot *lot) { return lot->tierPrice(1); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setTierPrice(1, v.toDouble()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return doubleCompare(l1->tierPrice(1), l2->tierPrice(1));
          },
      });
    C(TierQ3, {
          .defaultWidth = 5,
          .alignment = Qt::AlignRight,
          .title = QT_TR_NOOP("Tier Q3"),
          .dataFn = [&](const Lot *lot) { return lot->tierQuantity(2); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setTierQuantity(2, v.toInt()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return l1->tierQuantity(2) - l2->tierQuantity(2);
          },
      });
    C(TierP3, {
          .alignment = Qt::AlignRight,
          .title = QT_TR_NOOP("Tier P3"),
          .dataFn = [&](const Lot *lot) { return lot->tierPrice(2); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setTierPrice(2, v.toDouble()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return doubleCompare(l1->tierPrice(2), l2->tierPrice(2));
          },
      });
    C(LotId, {
          .alignment = Qt::AlignRight,
          .editable = false,
          .title = QT_TR_NOOP("Lot Id"),
          .displayFn = [&](const Lot *lot) { return lot->lotId(); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return uintCompare(l1->lotId(), l2->lotId());
          },
      });
    C(Retain, {
          .alignment = Qt::AlignHCenter,
          .checkable = true,
          .title = QT_TR_NOOP("Retain"),
          .valueModelFn = [&]() { return new QStringListModel({ tr("Yes"), tr("No") }); },
          .dataFn = [&](const Lot *lot) { return lot->retain(); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setRetain(v.toBool()); },
          .filterFn = [&](const Lot *lot) {
              return lot->retain() ? tr("Yes", "Filter>Retain>Yes") : tr("No", "Filter>Retain>No");
          },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return boolCompare(l1->retain(), l2->retain());
          },
      });
    C(Stockroom, {
          .alignment = Qt::AlignHCenter,
          .title = QT_TR_NOOP("Stockroom"),
          .valueModelFn = [&]() { return new QStringListModel({ "A"_l1, "B"_l1, "C"_l1, tr("None") }); },
          .dataFn = [&](const Lot *lot) { return QVariant::fromValue(lot->stockroom()); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setStockroom(v.value<BrickLink::Stockroom>()); },
          .filterFn = [&](const Lot *lot) {
              switch (lot->stockroom()) {
              case BrickLink::Stockroom::A: return QString("A"_l1);
              case BrickLink::Stockroom::B: return QString("B"_l1);
              case BrickLink::Stockroom::C: return QString("C"_l1);
              default                     : return tr("None", "Filter>Stockroom>None");
              }
          },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return int(l1->stockroom()) - int(l2->stockroom());
          },
      });
    C(Reserved, {
          .title = QT_TR_NOOP("Reserved"),
          .dataFn = [&](const Lot *lot) { return lot->reserved(); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setReserved(v.toString()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return l1->reserved().compare(l2->reserved());
          },
      });
    C(Weight, {
          .alignment = Qt::AlignRight,
          .title = QT_TR_NOOP("Weight"),
          .dataFn = [&](const Lot *lot) { return lot->totalWeight(); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setTotalWeight(v.toDouble()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return doubleCompare(l1->totalWeight(), l2->totalWeight());
          },
      });
    C(YearReleased, {
          .defaultWidth = 5,
          .alignment = Qt::AlignRight,
          .editable = false,
          .title = QT_TR_NOOP("Year"),
          .displayFn = [&](const Lot *lot) { return lot->itemYearReleased(); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              return l1->itemYearReleased() - l2->itemYearReleased();
          },
      });
    C(Marker, {
          .title = QT_TR_NOOP("Marker"),
          .dataFn = [&](const Lot *lot) { return lot->markerText(); },
          .setDataFn = [&](Lot *lot, const QVariant &v) { lot->setMarkerText(v.toString()); },
          .compareFn = [&](const Lot *l1, const Lot *l2) {
              int d = Utility::naturalCompare(l1->markerText(), l2->markerText());
              return d ? d : uintCompare(l1->markerColor().rgba(), l2->markerColor().rgba());
          },
      });
}

void Document::pictureUpdated(BrickLink::Picture *pic)
{
    if (!pic || !pic->item())
        return;

    for (const auto *lot : qAsConst(m_lots)) {
        if ((pic->item() == lot->item()) && (pic->color() == lot->color())) {
            QModelIndex idx = index(const_cast<Lot *>(lot), Picture);
            emitDataChanged(idx, idx);
        }
    }
}

bool Document::isSorted() const
{
    return m_isSorted;
}

QVector<QPair<int, Qt::SortOrder>> Document::sortColumns() const
{
    return m_sortColumns;
}

void Document::sort(int column, Qt::SortOrder order)
{
    return sort({ { column, order } });
}

void Document::sort(const QVector<QPair<int, Qt::SortOrder>> &columns)
{
    if (((columns.size() == 1) && (columns.at(0).first == -1)) || (columns == m_sortColumns))
        return;

    if (m_undo && !m_nextSortFilterIsDirect) {
        m_undo->push(new SortCmd(this, columns));
    } else {
        bool dummy1;
        LotList dummy2;
        sortDirect(columns, dummy1, dummy2);
        m_nextSortFilterIsDirect = false;
    }
}

bool Document::isFiltered() const
{
    return m_isFiltered;
}

const QVector<Filter> &Document::filter() const
{
    return m_filter;
}

void Document::setFilter(const QVector<Filter> &filter)
{
    if (filter == m_filter)
        return;

    if (m_undo && !m_nextSortFilterIsDirect) {
        m_undo->push(new FilterCmd(this, filter));
    } else {
        bool dummy1;
        LotList dummy2;
        filterDirect(filter, dummy1, dummy2);
        m_nextSortFilterIsDirect = false;
    }
}

void Document::nextSortFilterIsDirect()
{
    m_nextSortFilterIsDirect = true;
}

void Document::sortDirect(const QVector<QPair<int, Qt::SortOrder>> &columns, bool &sorted,
                          LotList &unsortedLots)
{
    bool emitSortColumnsChanged = (columns != m_sortColumns);
    bool wasSorted = isSorted();

    emit layoutAboutToBeChanged({ }, VerticalSortHint);
    QModelIndexList before = persistentIndexList();

    m_sortColumns = columns;

    if (!unsortedLots.isEmpty()) {
        m_isSorted = sorted;
        m_sortedLots = unsortedLots;
        unsortedLots.clear();

    } else {
        unsortedLots = m_sortedLots;
        sorted = m_isSorted;
        m_isSorted = true;
        m_sortedLots = m_lots;

        if ((columns.size() != 1) || (columns.at(0).first != -1)) {
            // make the sort deterministic
            auto columnsPlusIndex = columns;
            columnsPlusIndex.append(qMakePair(0, columns.constFirst().second));

            std::sort(
#ifdef AM_SORT_PARALLEL
                        // c++17 parallel + vectorized, but not supported everywhere yet
                        std::execution::par_unseq,
#endif
                        m_sortedLots.begin(), m_sortedLots.end(), [this, columnsPlusIndex](const auto *lot1, const auto *lot2) {
                int r = 0;
                for (const auto &sc : columnsPlusIndex) {
                    auto cmp = m_columns.value(sc.first).compareFn;
                    r = cmp ? cmp(lot1, lot2) : 0;
                    if (r) {
                        if (sc.second == Qt::DescendingOrder)
                            r = -r;
                        break;
                    }
                }
                return r < 0;
            });
        }
    }

    // we were filtered before, but we don't want to refilter: the solution is to
    // keep the old filtered lots, but use the order from m_sortedLots
    if (!m_filteredLots.isEmpty()
            && (m_filteredLots.size() != m_sortedLots.size())
            && (m_filteredLots != m_sortedLots)) {
        m_filteredLots = QtConcurrent::blockingFiltered(m_sortedLots, [this](auto *lot) {
            return m_filteredLots.contains(lot);
        });
    } else {
        m_filteredLots = m_sortedLots;
    }

    QModelIndexList after;
    foreach (const QModelIndex &idx, before)
        after.append(index(lot(idx), idx.column()));
    changePersistentIndexList(before, after);
    emit layoutChanged({ }, VerticalSortHint);

    if (emitSortColumnsChanged)
        emit sortColumnsChanged(columns);

    if (isSorted() != wasSorted)
        emit isSortedChanged(isSorted());
}

void Document::filterDirect(const QVector<Filter> &filter, bool &filtered,
                            LotList &unfilteredLots)
{
    bool emitFilterChanged = (filter != m_filter);
    bool wasFiltered = isFiltered();

    emit layoutAboutToBeChanged({ }, VerticalSortHint);
    QModelIndexList before = persistentIndexList();

    m_filter = filter;

    if (!unfilteredLots.isEmpty()) {
        m_isFiltered = filtered;
        m_filteredLots = unfilteredLots;
        unfilteredLots.clear();

    } else {
        unfilteredLots = m_filteredLots;
        filtered = m_isFiltered;
        m_isFiltered = true;
        m_filteredLots = m_sortedLots;

        if (!filter.isEmpty()) {
            m_filteredLots = QtConcurrent::blockingFiltered(m_sortedLots, [this](auto *lot) {
                return filterAcceptsLot(lot);
            });
        }
    }

    QModelIndexList after;
    foreach (const QModelIndex &idx, before)
        after.append(index(lot(idx), idx.column()));
    changePersistentIndexList(before, after);
    emit layoutChanged({ }, VerticalSortHint);

    if (emitFilterChanged)
        emit filterChanged(filter);

    if (isFiltered() != wasFiltered)
        emit isFilteredChanged(isFiltered());
}

QByteArray Document::saveSortFilterState() const
{
    QByteArray ba;
    QDataStream ds(&ba, QIODevice::WriteOnly);
    ds << QByteArray("SFST") << qint32(4);

    ds << qint8(m_sortColumns.size());
    for (int i = 0; i < m_sortColumns.size(); ++i)
        ds << qint8(m_sortColumns.at(i).first) << qint8(m_sortColumns.at(i).second);

    ds << qint8(m_filter.size());
    for (const auto &f : m_filter)
        ds << qint8(f.field()) << qint8(f.comparison()) << qint8(f.combination()) << f.expression();

    ds << qint32(m_sortedLots.size());
    for (int i = 0; i < m_sortedLots.size(); ++i) {
        auto *lot = m_sortedLots.at(i);
        qint32 row = m_lots.indexOf(lot);
        bool visible = m_filteredLots.contains(lot);

        ds << (visible ? row : (-row - 1)); // can't have -0
    }

    return ba;
}

bool Document::restoreSortFilterState(const QByteArray &ba)
{
    QDataStream ds(ba);
    QByteArray tag;
    qint32 version;
    ds >> tag >> version;
    if ((ds.status() != QDataStream::Ok) || (tag != "SFST") || (version < 2) || (version > 3))
        return false;
    QVector<QPair<int, Qt::SortOrder>> sortColumns;
    QVector<Filter> filter;
    qint32 viewSize;

    qint8 sortColumnsSize;
    if (version == 2)
        sortColumnsSize = 1;
    else
        ds >> sortColumnsSize;
    for (int i = 0; i < sortColumnsSize; ++i) {
        qint8 sortColumn, sortOrder;
        ds >> sortColumn >> sortOrder;
        sortColumns << qMakePair(sortColumn, Qt::SortOrder(sortOrder));
    }

    if (version <= 3) {
        QString filterString;
        ds >> filterString;
        filter = m_filterParser->parse(filterString);
    } else {
        qint8 filterSize;
        ds >> filterSize;
        for (int i = 0; i < filterSize; ++i) {
            qint8 filterField, filterComparison, filterCombination;
            QString filterExpression;
            ds >> filterField >> filterComparison >> filterCombination >> filterExpression;
            Filter f;
            f.setField(filterField);
            f.setComparison(static_cast<Filter::Comparison>(filterComparison));
            f.setCombination(static_cast<Filter::Combination>(filterCombination));
            f.setExpression(filterExpression);
            filter.append(f);
        }
    }
    ds >> viewSize;
    if ((ds.status() != QDataStream::Ok) || (viewSize != m_lots.size()))
        return false;

    LotList sortedLots;
    LotList filteredLots;
    int lotsSize = m_lots.size();
    while (viewSize--) {
        qint32 pos;
        ds >> pos;
        if ((pos < -lotsSize) || (pos >= lotsSize))
            return false;
        bool visible = (pos >= 0);
        pos = visible ? pos : (-pos - 1);
        auto *lot = m_lots.at(pos);
        sortedLots << lot;
        if (visible)
            filteredLots << lot;
    }

    if (ds.status() != QDataStream::Ok)
        return false;

    bool willBeSorted = true;
    sortDirect(sortColumns, willBeSorted, sortedLots);
    bool willBeFiltered = true;
    filterDirect(filter, willBeFiltered, filteredLots);
    return true;
}

QString Document::filterToolTip() const
{
    return m_filterParser->toolTip();
}

void Document::reSort()
{
    if (!isSorted())
        m_undo->push(new SortCmd(this, m_sortColumns));
}

void Document::reFilter()
{
    if (!isFiltered())
        m_undo->push(new FilterCmd(this, m_filter));
}


bool Document::filterAcceptsLot(const Lot *lot) const
{
    if (!lot)
        return false;
    else if (m_filter.isEmpty())
        return true;

    bool result = false;
    Filter::Combination nextcomb = Filter::Or;

    for (const Filter &f : m_filter) {
        int firstcol = f.field();
        int lastcol = firstcol;
        if (firstcol < 0) {
            firstcol = 0;
            lastcol = columnCount() - 1;
        }

        bool localresult = false;
        for (int col = firstcol; col <= lastcol && !localresult; ++col) {
            QVariant v = dataForFilterRole(lot, static_cast<Field>(col));
            localresult = f.matches(v);
        }
        if (nextcomb == Filter::And)
            result = result && localresult;
        else
            result = result || localresult;

        nextcomb = f.combination();
    }
    return result;
}

bool Document::event(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        languageChange();
    return QAbstractTableModel::event(e);
}

void Document::languageChange()
{
    m_filterParser->setStandardCombinationTokens(Filter::And | Filter::Or);
    m_filterParser->setStandardComparisonTokens(Filter::Matches | Filter::DoesNotMatch |
                                          Filter::Is | Filter::IsNot |
                                          Filter::Less | Filter::LessEqual |
                                          Filter::Greater | Filter::GreaterEqual |
                                          Filter::StartsWith | Filter::DoesNotStartWith |
                                          Filter::EndsWith | Filter::DoesNotEndWith);

    QVector<QPair<int, QString>> fields;
    fields.append({ -1, tr("Any") });
    QString str;
    for (int i = 0; i < columnCount(); ++i) {
        str = headerData(i, Qt::Horizontal, Qt::DisplayRole).toString();
        if (!str.isEmpty())
            fields.append({ i, str });
    }

    m_filterParser->setFieldTokens(fields);
}

LotList Document::sortLotList(const LotList &list) const
{
    LotList result(list);
    qParallelSort(result.begin(), result.end(), [this](const auto &i1, const auto &i2) {
        return index(i1).row() < index(i2).row();
    });
    return result;
}


///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////


const QString DocumentLotsMimeData::s_mimetype = "application/x-bricklink-invlots"_l1;

DocumentLotsMimeData::DocumentLotsMimeData(const LotList &lots)
    : QMimeData()
{
    setLots(lots);
}

void DocumentLotsMimeData::setLots(const LotList &lots)
{
    QByteArray data;
    QString text;

    QDataStream ds(&data, QIODevice::WriteOnly);

    ds << quint32(lots.count());
    for (const Lot *lot : lots) {
        lot->save(ds);
        if (!text.isEmpty())
            text.append("\n"_l1);
        text.append(QLatin1String(lot->itemId()));
    }
    setText(text);
    setData(s_mimetype, data);
}

LotList DocumentLotsMimeData::lots(const QMimeData *md)
{
    LotList lots;

    if (md) {
        QByteArray data = md->data(s_mimetype);
        QDataStream ds(data);

        if (!data.isEmpty()) {
            quint32 count = 0;
            ds >> count;

            for (; count && !ds.atEnd(); count--) {
                if (auto lot = Lot::restore(ds))
                    lots.append(lot);
            }
        }
    }
    return lots;
}

QStringList DocumentLotsMimeData::formats() const
{
    static QStringList sl;

    if (sl.isEmpty())
        sl << s_mimetype << "text/plain"_l1;

    return sl;
}

bool DocumentLotsMimeData::hasFormat(const QString &mimeType) const
{
    return mimeType.compare(s_mimetype) || mimeType.compare("text/plain"_l1);
}


#include "moc_document.cpp"
