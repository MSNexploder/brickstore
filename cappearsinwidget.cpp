/* Copyright (C) 2004-2005 Robert Griebl.All rights reserved.
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
#include <QMenu>
#include <QVariant>
#include <QAction>
#include <QHeaderView>
#include <QDesktopServices>
#include <QToolTip>
#include <QApplication>
#include <QTimer>

#include "qtemporaryresource.h"

#include "cframework.h"
#include "cpicturewidget.h"

#include "cappearsinwidget.h"


namespace {


class AppearsInModel : public QAbstractTableModel {
    Q_OBJECT
public:
    AppearsInModel(const BrickLink::Item *item, const BrickLink::Color *color)
    {
        m_item = item;
        m_color = color;

        if (item)
            m_appearsin = item->appearsIn(color);

       foreach(const BrickLink::Item::AppearsInColor &vec, m_appearsin) {
            foreach(const BrickLink::Item::AppearsInItem &item, vec)
                m_items.append(const_cast<BrickLink::Item::AppearsInItem *>(&item));
       }

        connect(BrickLink::inst(), SIGNAL(pictureUpdated(BrickLink::Picture *)), this, SLOT(pictureUpdated(BrickLink::Picture *)));
    }

    ~AppearsInModel()
    {
    }

    QModelIndex AppearsInModel::index(int row, int column, const QModelIndex &parent) const
    {
        if (hasIndex(row, column, parent) && !parent.isValid())
            return createIndex(row, column, m_items.at(row));
        return QModelIndex();
    }

    const BrickLink::Item::AppearsInItem *AppearsInModel::appearsIn(const QModelIndex &idx) const
    {
        return idx.isValid() ? static_cast<const BrickLink::Item::AppearsInItem *>(idx.internalPointer()) : 0;
    }

    QModelIndex AppearsInModel::index(const BrickLink::Item::AppearsInItem *const_ai) const
    {
        BrickLink::Item::AppearsInItem *ai = const_cast<BrickLink::Item::AppearsInItem *>(const_ai);

        return ai ? createIndex(m_items.indexOf(ai), 0, ai) : QModelIndex();
    }

    virtual int rowCount(const QModelIndex & /*parent*/) const
    { return m_items.size(); }

    virtual int columnCount(const QModelIndex & /*parent*/) const
    { return 3; }

    virtual QVariant data(const QModelIndex &index, int role) const
    {
        QVariant res;
        const BrickLink::Item::AppearsInItem *appears = appearsIn(index);
        int col = index.column();

        if (!appears)
            return res;

        switch (role) {
        case Qt::DisplayRole:
            switch (col) {
            case 0: res = QString::number(appears->first); break;
            case 1: res = appears->second->id(); break;
            case 2: res = appears->second->name(); break;
            }
            break;

        case Qt::ToolTipRole: {
            BrickLink::Picture *pic = BrickLink::inst()->picture(appears->second, appears->second->defaultColor(), true);

            QTemporaryResource::registerResource("#/appears_in_set_tooltip_picture.png", (pic && pic->valid()) ? pic->image() : QImage());
            m_tooltip_pic = pic;

            foreach (QWidget *w, QApplication::topLevelWidgets()) {
                if (w->inherits("QTipLabel")) {
                    qobject_cast<QLabel *>(w)->clear();
                    break;
                }
            }
            res = createToolTip(appears->second, pic);
            break;
        }
        }

        return res;
    }

    QString createToolTip(const BrickLink::Item *item, BrickLink::Picture *pic) const
    {
        QString str = QLatin1String("<div class=\"appearsin\"><table><tr><td rowspan=\"2\">%1</td><td><b>%2</b></td></tr><tr><td>%3</td></tr></table></div>");
        QString img_left = QLatin1String("<img src=\"#/appears_in_set_tooltip_picture.png\" />");
        QString note_left = QLatin1String("<i>") + CAppearsInWidget::tr("[Image is loading]") + QLatin1String("</i>");

        if (pic && (pic->updateStatus() == BrickLink::Updating))
            return str.arg(note_left).arg(item->id()).arg(item->name());
        else
            return str.arg(img_left).arg(item->id()).arg(item->name());
    }

    virtual QVariant headerData(int section, Qt::Orientation orient, int role) const
    {
        if ((orient == Qt::Horizontal) && (role == Qt::DisplayRole)) {
            switch (section) {
            case 0: return tr("#");
            case 1: return tr("Set");
            case 2: return tr("Name");
            }
        }
        return QVariant();
    }

private slots:
    void pictureUpdated(BrickLink::Picture *pic)
    {
        if (!pic || pic != m_tooltip_pic)
            return;

        if (QToolTip::isVisible() && QToolTip::text().startsWith("<div class=\"appearsin\">")) {
            QTemporaryResource::registerResource("#/appears_in_set_tooltip_picture.png", pic->image());

            QPoint pos;
            foreach (QWidget *w, QApplication::topLevelWidgets()) {
                if (w->inherits("QTipLabel")) {
             //       pos = w->pos();
                    QSize extra = w->size() - w->sizeHint();
                    qobject_cast<QLabel *>(w)->clear();
                    qobject_cast<QLabel *>(w)->setText(createToolTip(pic->item(), pic));
                    w->resize(w->sizeHint() + extra);
                    break;
                }
            }
//            if (!pos.isNull()) {
  //              QToolTip::showText(pos, createToolTip(pic->item(), pic));
    //        }
        }
        m_tooltip_pic = 0;
    }

private:
    const BrickLink::Item *        m_item;
    const BrickLink::Color *       m_color;
    BrickLink::Item::AppearsIn     m_appearsin;
    QList<BrickLink::Item::AppearsInItem *> m_items;
    mutable BrickLink::Picture *   m_tooltip_pic;
};

#if 0
class AppearsInListItem : public CListViewItem {
public:
    AppearsInListItem(CListView *lv, int qty, const BrickLink::Item *item)
            : CListViewItem(lv), m_qty(qty), m_item(item), m_picture(0)
    {
    }

    virtual ~AppearsInListItem()
    {
        if (m_picture)
            m_picture->release();
    }

    virtual QString text(int col) const
    {
        switch (col) {
        case  0: return QString::number(m_qty);
        case  1: return m_item->id();
        case  2: return m_item->name();
        default: return QString();
        }
    }

    QString toolTip() const
    {
        QString str = "<table><tr><td rowspan=\"2\">%1</td><td><b>%2</b></td></tr><tr><td>%3</td></tr></table>";
        QString left_cell;

        BrickLink::Picture *pic = picture();

        if (pic && pic->valid()) {
            QMimeSourceFactory::defaultFactory()->setPixmap("appears_in_set_tooltip_picture", pic->pixmap());

            left_cell = "<img src=\"appears_in_set_tooltip_picture\" />";
        }
        else if (pic && (pic->updateStatus() == BrickLink::Updating)) {
            left_cell = "<i>" + CAppearsInWidget::tr("[Image is loading]") + "</i>";
        }

        return str.arg(left_cell).arg(m_item->id()).arg(m_item->name());
    }

    virtual int compare(QListViewItem *i, int col, bool ascending) const
    {
        if (col == 0)
            return (m_qty - static_cast <AppearsInListItem *>(i)->m_qty);
        else
            return CListViewItem::compare(i, col, ascending);
    }

    const BrickLink::Item *item() const
    { return m_item; }

    BrickLink::Picture *picture() const
    {
        if (!m_picture) {
            m_picture = BrickLink::inst()->picture(m_item, m_item->defaultColor(), true);

            if (m_picture)
                m_picture->addRef();
        }
        return m_picture;
    }

private:
    int                         m_qty;
    const BrickLink::Item *     m_item;
    mutable BrickLink::Picture *m_picture;
};


class AppearsInToolTip : public QObject, public QToolTip {
    Q_OBJECT

public:
    AppearsInToolTip(QWidget *parent, CAppearsInWidget *aiw)
            : QObject(parent), QToolTip(parent, new QToolTipGroup(parent)),
            m_aiw(aiw), m_tip_item(0)
    {
        connect(group(), SIGNAL(removeTip()), this, SLOT(tipHidden()));

        connect(BrickLink::inst(), SIGNAL(pictureUpdated(BrickLink::Picture *)), this, SLOT(pictureUpdated(BrickLink::Picture *)));
    }

    virtual ~AppearsInToolTip()
    { }

    void maybeTip(const QPoint &pos)
    {
        if (!parentWidget() || !m_aiw /*|| !m_aiw->showToolTips ( )*/)
            return;

        AppearsInListItem *item = static_cast <AppearsInListItem *>(m_aiw->itemAt(pos));

        if (item) {
            m_tip_item = item;
            tip(m_aiw->itemRect(item), item->toolTip());
        }
    }

    void hideTip()
    {
        QToolTip::hide();
        m_tip_item = 0; // tipHidden() doesn't get called immediatly
    }

private slots:
    void tipHidden()
    {
        m_tip_item = 0;
    }

    void pictureUpdated(BrickLink::Picture *pic)
    {
        if (m_tip_item && m_tip_item->picture() == pic)
            maybeTip(parentWidget()->mapFromGlobal(QCursor::pos()));
    }

private:
    CAppearsInWidget *m_aiw;
    AppearsInListItem *m_tip_item;
};
#endif


} // namespace


class CAppearsInWidgetPrivate {
public:
    const BrickLink::Item * m_item;
    const BrickLink::Color *m_color;
    QTimer *m_resize_timer;
};

CAppearsInWidget::CAppearsInWidget(QWidget *parent)
    : QTreeView(parent)
{
    d = new CAppearsInWidgetPrivate();
    d->m_resize_timer = new QTimer(this);

    setAlternatingRowColors(true);
    setAllColumnsShowFocus(true);
    setUniformRowHeights(true);
    setRootIsDecorated(false);
    setSortingEnabled(true);
    sortByColumn(0);
    header()->setSortIndicatorShown(false);
    setContextMenuPolicy(Qt::CustomContextMenu);

    QAction *a;
    a = new QAction(this);
    a->setObjectName("edit_partoutitems");
    a->setIcon(QIcon(":/images/22x22/edit_partoutitems"));
    connect(a, SIGNAL(triggered()), this, SLOT(partOut()));
    addAction(a);

    a = new QAction(this);
    a->setSeparator(true);
    addAction(a);

    a = new QAction(this);
    a->setObjectName("edit_magnify");
    a->setIcon(QIcon(":/images/22x22/viewmagp"));
    connect(a, SIGNAL(triggered()), this, SLOT(viewLargeImage()));
    addAction(a);

    a = new QAction(this);
    a->setSeparator(true);
    addAction(a);

    a = new QAction(this);
    a->setObjectName("edit_bl_catalog");
    a->setIcon(QIcon(":/images/22x22/edit_bl_catalog"));
    connect(a, SIGNAL(triggered()), this, SLOT(showBLCatalogInfo()));
    addAction(a);
    a = new QAction(this);
    a->setObjectName("edit_bl_priceguide");
    a->setIcon(QIcon(":/images/22x22/edit_bl_priceguide"));
    connect(a, SIGNAL(triggered()), this, SLOT(showBLPriceGuideInfo()));
    addAction(a);
    a = new QAction(this);
    a->setObjectName("edit_bl_lotsforsale");
    a->setIcon(QIcon(":/images/22x22/edit_bl_lotsforsale"));
    connect(a, SIGNAL(triggered()), this, SLOT(showBLLotsForSale()));
    addAction(a);

    connect(d->m_resize_timer, SIGNAL(timeout()), this, SLOT(resizeColumns()));
    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(showContextMenu(const QPoint &)));
    connect(this, SIGNAL(activated(const QModelIndex &)), this, SLOT(partOut()));

    languageChange();
    setItem(0, 0);
}

void CAppearsInWidget::languageChange()
{
    findChild<QAction *> ("edit_partoutitems")->setText(tr("Part out Item..."));
    findChild<QAction *> ("edit_magnify")->setText(tr("View large image..."));
    findChild<QAction *> ("edit_bl_catalog")->setText(tr("Show BrickLink Catalog Info..."));
    findChild<QAction *> ("edit_bl_priceguide")->setText(tr("Show BrickLink Price Guide Info..."));
    findChild<QAction *> ("edit_bl_lotsforsale")->setText(tr("Show Lots for Sale on BrickLink..."));
}

CAppearsInWidget::~CAppearsInWidget()
{
    delete d;
}

void CAppearsInWidget::showContextMenu(const QPoint &pos)
{
    if (appearsIn())
        QMenu::exec(actions(), viewport()->mapToGlobal(pos));
}


const BrickLink::Item::AppearsInItem *CAppearsInWidget::appearsIn() const
{
    AppearsInModel *m = static_cast<AppearsInModel *>(model());

    if (!selectionModel()->selectedIndexes().isEmpty())
        return m->appearsIn(selectionModel()->selectedIndexes().front());
    else
        return 0;
}

void CAppearsInWidget::partOut()
{
    const BrickLink::Item::AppearsInItem *ai = appearsIn();

    if (ai && ai->second)
        CFrameWork::inst()->fileImportBrickLinkInventory(ai->second);
}

QSize CAppearsInWidget::minimumSizeHint() const
{
    const QFontMetrics &fm = fontMetrics();

    return QSize(fm.width('m') * 20, fm.height() * 6);
}

QSize CAppearsInWidget::sizeHint() const
{
    return minimumSizeHint() * 2;
}

void CAppearsInWidget::setItem(const BrickLink::Item *item, const BrickLink::Color *color)
{
    d->m_item = item;
    d->m_color = color;

    setModel(new AppearsInModel(d->m_item , d->m_color));

    if (item) {
        d->m_resize_timer->start(100);
    }
    else {
        d->m_resize_timer->stop();
        resizeColumns();
    }
}

void CAppearsInWidget::resizeColumns()
{
    setUpdatesEnabled(false);
    resizeColumnToContents(0);
    resizeColumnToContents(1);
    setUpdatesEnabled(true);
}

void CAppearsInWidget::viewLargeImage()
{
    const BrickLink::Item::AppearsInItem *ai = appearsIn();

    if (ai && ai->second) {
        BrickLink::Picture *lpic = BrickLink::inst()->largePicture(ai->second, true);

        if (lpic) {
            CLargePictureWidget *l = new CLargePictureWidget(lpic, this);
            l->show();
            l->raise();
            l->activateWindow();
            l->setFocus();
        }
    }
}

void CAppearsInWidget::showBLCatalogInfo()
{
    const BrickLink::Item::AppearsInItem *ai = appearsIn();

    if (ai && ai->second)
        QDesktopServices::openUrl(BrickLink::inst()->url(BrickLink::URL_CatalogInfo, ai->second));
}

void CAppearsInWidget::showBLPriceGuideInfo()
{
    const BrickLink::Item::AppearsInItem *ai = appearsIn();

    if (ai && ai->second)
        QDesktopServices::openUrl(BrickLink::inst()->url(BrickLink::URL_PriceGuideInfo, ai->second, BrickLink::inst()->color(0)));
}

void CAppearsInWidget::showBLLotsForSale()
{
    const BrickLink::Item::AppearsInItem *ai = appearsIn();

    if (ai && ai->second)
        QDesktopServices::openUrl(BrickLink::inst()->url(BrickLink::URL_LotsForSale, ai->second, BrickLink::inst()->color(0)));
}

#include "cappearsinwidget.moc"
