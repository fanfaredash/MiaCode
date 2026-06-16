#pragma once

#include <QLayout>
#include <QList>
#include <QRect>
#include <QSize>
#include <QStyle>

class QLayoutItem;
class QWidget;

namespace miacode::ui {

// A left-packing, line-wrapping layout: items flow horizontally and wrap to a
// new row when the current row runs out of width (height-for-width). Used for
// the export page's difficulty badge row so 7 pills fold to a second row
// instead of clipping (horizontal scrolling is forbidden there). Standard Qt
// "Flow Layout" pattern — no stretch items; just append widgets in order.
class FlowLayout : public QLayout
{
    Q_OBJECT

public:
    explicit FlowLayout(QWidget* parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
    explicit FlowLayout(int margin = -1, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override;
    QLayoutItem* takeAt(int index) override;

private:
    int doLayout(const QRect& rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem*> itemList_;
    int hSpace_;
    int vSpace_;
};

}  // namespace miacode::ui
