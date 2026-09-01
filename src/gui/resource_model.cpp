#include "resource_model.hpp"

#include <QColor>
#include <QPainter>
#include <QStyle>
#include <algorithm>

namespace sxpe::gui {
namespace {

QString hex32(std::uint32_t v) { return QString("%1").arg(v, 8, 16, QLatin1Char('0')).toUpper(); }
QString hex64(std::uint64_t v) { return QString("%1").arg(v, 16, 16, QLatin1Char('0')).toUpper(); }

}  // namespace

ResourceModel::ResourceModel(QObject* parent) : QAbstractTableModel(parent) {}

void ResourceModel::set_rows(std::vector<sxpe::commands::UiRow> rows) {
    beginResetModel();
    all_.clear();
    all_.reserve(rows.size());
    for (auto& u : rows) {
        DisplayRow d;
        d.index = u.index;
        d.type = u.type;
        d.group = u.group;
        d.instance = u.instance;
        d.ordinal = u.ordinal;
        d.file_size = u.file_size;
        d.mem_size = u.mem_size;
        d.id_s = QString::number(u.index);
        d.tag = QString::fromStdString(u.tag);
        d.name = QString::fromStdString(u.name);
        d.type_h = hex32(u.type);
        d.group_h = hex32(u.group);
        d.inst_h = hex64(u.instance);
        d.ord_s = QString::number(u.ordinal);
        d.size_s = QString::number(u.mem_size);
        d.cmp_s = u.compressed ? QStringLiteral("Y") : QString();
        d.compressed = u.compressed;
        d.deleted = u.deleted;
        all_.push_back(std::move(d));
    }
    visible_.resize(static_cast<int>(all_.size()));
    for (int i = 0; i < visible_.size(); ++i) {
        visible_[i] = i;
    }
    if (sort_col_ >= 0) {
        apply_sort(visible_);
    }
    endResetModel();
}

void ResourceModel::set_visible(QVector<int> visible) {
    if (sort_col_ >= 0) {
        apply_sort(visible);
    }
    emit layoutAboutToBeChanged();
    visible_ = std::move(visible);
    emit layoutChanged();
}

void ResourceModel::apply_sort(QVector<int>& vis) const {
    const int col = sort_col_;
    const bool asc = sort_order_ == Qt::AscendingOrder;
    std::sort(vis.begin(), vis.end(), [&](int ia, int ib) {
        const auto& a = all_[static_cast<size_t>(ia)];
        const auto& b = all_[static_cast<size_t>(ib)];
        int cmp = 0;
        switch (col) {
            case Id:
                cmp = (a.index > b.index) - (a.index < b.index);
                break;
            case Tag:
                cmp = QString::compare(a.tag, b.tag, Qt::CaseInsensitive);
                break;
            case Name:
                cmp = QString::compare(a.name, b.name, Qt::CaseInsensitive);
                break;
            case Type:
                cmp = (a.type > b.type) - (a.type < b.type);
                break;
            case Group:
                cmp = (a.group > b.group) - (a.group < b.group);
                break;
            case Instance:
                cmp = (a.instance > b.instance) - (a.instance < b.instance);
                break;
            case Ordinal:
                cmp = (a.ordinal > b.ordinal) - (a.ordinal < b.ordinal);
                break;
            case Size:
                cmp = (a.mem_size > b.mem_size) - (a.mem_size < b.mem_size);
                break;
            case Compressed:
                cmp = (a.compressed > b.compressed) - (a.compressed < b.compressed);
                break;
            default:
                break;
        }
        if (cmp == 0) {
            cmp = (a.index > b.index) - (a.index < b.index);
        }
        return asc ? cmp < 0 : cmp > 0;
    });
}

void ResourceModel::sort(int column, Qt::SortOrder order) {
    sort_col_ = (column >= 0 && column < Count_) ? column : -1;
    sort_order_ = order;
    emit layoutAboutToBeChanged();
    if (sort_col_ >= 0) {
        apply_sort(visible_);
    } else {
        std::sort(visible_.begin(), visible_.end());
    }
    emit layoutChanged();
}

int ResourceModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : visible_.size();
}
int ResourceModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : Count_;
}

const DisplayRow* ResourceModel::row_at(int view_row) const {
    if (view_row < 0 || view_row >= visible_.size()) {
        return nullptr;
    }
    const int i = visible_[view_row];
    if (i < 0 || static_cast<size_t>(i) >= all_.size()) {
        return nullptr;
    }
    return &all_[static_cast<size_t>(i)];
}

const QString& ResourceModel::cell_text(const DisplayRow& r, int column) {
    static const QString kEmpty;
    switch (column) {
        case Id:
            return r.id_s;
        case Tag:
            return r.tag;
        case Name:
            return r.name;
        case Type:
            return r.type_h;
        case Group:
            return r.group_h;
        case Instance:
            return r.inst_h;
        case Ordinal:
            return r.ord_s;
        case Size:
            return r.size_s;
        case Compressed:
            return r.cmp_s;
        default:
            return kEmpty;
    }
}

int ResourceModel::hint_width(int column, const QFontMetrics& fm) const {
    int w = fm.horizontalAdvance(headerData(column, Qt::Horizontal, Qt::DisplayRole).toString());
    const int n = visible_.isEmpty() ? static_cast<int>(all_.size()) : visible_.size();
    for (int i = 0; i < n; ++i) {
        const int idx = visible_.isEmpty() ? i : visible_[i];
        if (idx < 0 || static_cast<size_t>(idx) >= all_.size()) {
            continue;
        }
        w = std::max(w, fm.horizontalAdvance(cell_text(all_[static_cast<size_t>(idx)], column)));
    }
    return w + 16;
}

QVariant ResourceModel::data(const QModelIndex& index, int role) const {
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
        return {};
    }
    const auto* r = row_at(index.row());
    if (!r) {
        return {};
    }
    if (role == Qt::ToolTipRole) {
        return r->type_h + QLatin1Char('-') + r->group_h + QLatin1Char('-') + r->inst_h +
               QLatin1String(" #") + r->ord_s;
    }
    return cell_text(*r, index.column());
}

void ResourcePaintDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const {
    const auto* model = static_cast<const ResourceModel*>(index.model());
    const auto* r = model->row_at(index.row());
    if (!r) {
        return;
    }
    const bool sel = option.state.testFlag(QStyle::State_Selected);
    const QPalette& pal = option.palette;
    const QColor bg = sel ? pal.color(QPalette::Highlight)
                          : ((index.row() & 1) ? pal.color(QPalette::AlternateBase)
                                               : pal.color(QPalette::Base));
    painter->save();
    painter->setClipRect(option.rect);
    painter->fillRect(option.rect, bg);
    if (sel) {
        painter->setPen(pal.color(QPalette::HighlightedText));
    } else if (r->deleted) {
        painter->setPen(QColor(128, 128, 128));
    } else {
        painter->setPen(pal.color(QPalette::Text));
    }
    painter->setFont(option.font);
    painter->drawText(option.rect.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                      ResourceModel::cell_text(*r, index.column()));
    painter->restore();
}

QSize ResourcePaintDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return {80, 22};
}

QVariant ResourceModel::headerData(int section, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    static const char* k[] = {"ID", "Tag", "Name", "Type", "Group", "Instance", "#", "Size", "Cmp"};
    return section >= 0 && section < Count_ ? QString::fromLatin1(k[section]) : QVariant{};
}

QVector<int> filter_rows(const std::vector<DisplayRow>& all, const QString& text,
                         const QString& tag) {
    QVector<int> out;
    out.reserve(static_cast<int>(all.size()));
    const auto needle = text.trimmed();
    const auto want = tag.trimmed();
    const bool any_tag = want.isEmpty() || want == QLatin1String("All");
    for (int i = 0; i < static_cast<int>(all.size()); ++i) {
        const auto& r = all[static_cast<size_t>(i)];
        if (!any_tag) {
            if (r.tag.compare(want, Qt::CaseInsensitive) != 0 &&
                !(want == QLatin1String("IMG") && r.tag == QLatin1String("_IMG"))) {
                continue;
            }
        }
        if (!needle.isEmpty()) {
            if (!r.name.contains(needle, Qt::CaseInsensitive) &&
                !r.type_h.contains(needle, Qt::CaseInsensitive) &&
                !r.tag.contains(needle, Qt::CaseInsensitive) &&
                !r.inst_h.contains(needle, Qt::CaseInsensitive) &&
                !r.id_s.contains(needle, Qt::CaseInsensitive)) {
                continue;
            }
        }
        out.push_back(i);
    }
    return out;
}

}  // namespace sxpe::gui
